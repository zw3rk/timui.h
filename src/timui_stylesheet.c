/* ---- stylesheet parser/resolver --------------------------------------- *
 * A deliberately small TCSS-like layer over TimuiStyle. It parses one selector
 * per rule and resolves by simple specificity + source order. */
#define TIMUI_SS_NAME_MAX 63

struct TimuiStyleRule {
    TimuiWidgetKind kind;
    char id[TIMUI_SS_NAME_MAX + 1];
    char klass[TIMUI_SS_NAME_MAX + 1];
    uint32_t states;
    int specificity;
    int order;
    uint32_t props;
    TimuiStyle style;
    uint32_t attr_props;
    uint32_t attr_values;
    uint32_t border;
    int padding;
    int gap;
    uint32_t gradient_lo;
    uint32_t gradient_hi;
};

typedef struct {
    const char *s;
    size_t len;
    size_t pos;
} TimuiStyleParser;

static int ss_alloc_valid_(const TimuiAllocator *a){
    return a && a->alloc && a->realloc && a->free;
}
static int ss_is_space_(char c){
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
static int ss_is_alpha_(char c){
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int ss_is_name_(char c){
    return ss_is_alpha_(c) || (c >= '0' && c <= '9') || c == '-';
}
static void ss_skip_ws_(TimuiStyleParser *p){
    while(p->pos < p->len && ss_is_space_(p->s[p->pos])) p->pos++;
}
static int ss_at_(TimuiStyleParser *p, char c){
    ss_skip_ws_(p);
    return p->pos < p->len && p->s[p->pos] == c;
}
static int ss_take_(TimuiStyleParser *p, char c){
    if(!ss_at_(p, c)) return 0;
    p->pos++;
    return 1;
}
static int ss_ident_(TimuiStyleParser *p, char *out, size_t cap){
    size_t n = 0;
    ss_skip_ws_(p);
    if(p->pos >= p->len || !ss_is_alpha_(p->s[p->pos])) return 0;
    while(p->pos < p->len && ss_is_name_(p->s[p->pos])){
        if(n + 1 < cap) out[n++] = p->s[p->pos];
        else return 0;
        p->pos++;
    }
    out[n] = '\0';
    return 1;
}
static int ss_streq_(const char *a, const char *b){
    return strcmp(a ? a : "", b ? b : "") == 0;
}
static int ss_widget_kind_(const char *name, TimuiWidgetKind *out){
    if(ss_streq_(name, "label")) *out = TIMUI_WIDGET_LABEL;
    else if(ss_streq_(name, "panel")) *out = TIMUI_WIDGET_PANEL;
    else if(ss_streq_(name, "button")) *out = TIMUI_WIDGET_BUTTON;
    else if(ss_streq_(name, "input")) *out = TIMUI_WIDGET_INPUT;
    else if(ss_streq_(name, "textarea") || ss_streq_(name, "text-area")) *out = TIMUI_WIDGET_TEXT_AREA;
    else if(ss_streq_(name, "listbox")) *out = TIMUI_WIDGET_LISTBOX;
    else if(ss_streq_(name, "table")) *out = TIMUI_WIDGET_TABLE;
    else if(ss_streq_(name, "tree")) *out = TIMUI_WIDGET_TREE;
    else if(ss_streq_(name, "menu")) *out = TIMUI_WIDGET_MENU;
    else if(ss_streq_(name, "toast")) *out = TIMUI_WIDGET_TOAST;
    else if(ss_streq_(name, "split")) *out = TIMUI_WIDGET_SPLIT;
    else return 0;
    return 1;
}
static int ss_state_(const char *name, uint32_t *out){
    if(ss_streq_(name, "focused")) *out = TIMUI_STYLE_STATE_FOCUSED;
    else if(ss_streq_(name, "hovered") || ss_streq_(name, "hover")) *out = TIMUI_STYLE_STATE_HOVERED;
    else if(ss_streq_(name, "active") || ss_streq_(name, "pressed")) *out = TIMUI_STYLE_STATE_ACTIVE;
    else if(ss_streq_(name, "disabled")) *out = TIMUI_STYLE_STATE_DISABLED;
    else if(ss_streq_(name, "selected")) *out = TIMUI_STYLE_STATE_SELECTED;
    else return 0;
    return 1;
}
static int ss_hex_(char c){
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return 10 + c - 'a';
    if(c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}
static int ss_color_(TimuiStyleParser *p, uint32_t *out){
    uint32_t v = 0;
    int i;
    char word[TIMUI_SS_NAME_MAX + 1];
    ss_skip_ws_(p);
    if(p->pos < p->len && p->s[p->pos] == '#'){
        p->pos++;
        for(i = 0; i < 6; i++){
            int h;
            if(p->pos >= p->len) return 0;
            h = ss_hex_(p->s[p->pos++]);
            if(h < 0) return 0;
            v = (v << 4) | (uint32_t)h;
        }
        if(p->pos < p->len && ss_is_name_(p->s[p->pos])) return 0;
        *out = v;
        return 1;
    }
    if(!ss_ident_(p, word, sizeof word)) return 0;
    if(!ss_streq_(word, "default")) return 0;
    *out = TIMUI_COLOR_DEFAULT;
    return 1;
}
static int ss_bool_(TimuiStyleParser *p, int *out){
    char word[TIMUI_SS_NAME_MAX + 1];
    if(!ss_ident_(p, word, sizeof word)) return 0;
    if(ss_streq_(word, "true") || ss_streq_(word, "on") || ss_streq_(word, "yes")){
        *out = 1; return 1;
    }
    if(ss_streq_(word, "false") || ss_streq_(word, "off") || ss_streq_(word, "no")){
        *out = 0; return 1;
    }
    return 0;
}
static int ss_int_(TimuiStyleParser *p, int *out){
    long v = 0;
    int neg = 0, any = 0;
    ss_skip_ws_(p);
    if(p->pos < p->len && p->s[p->pos] == '-'){ neg = 1; p->pos++; }
    while(p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9'){
        any = 1;
        v = v * 10 + (p->s[p->pos] - '0');
        if(v > 1000000L) return 0;
        p->pos++;
    }
    if(!any || neg) return 0;
    *out = (int)v;
    return 1;
}
static int ss_border_(TimuiStyleParser *p, uint32_t *out){
    char word[TIMUI_SS_NAME_MAX + 1];
    if(!ss_ident_(p, word, sizeof word)) return 0;
    if(ss_streq_(word, "none")) *out = TIMUI_BORDER_NONE;
    else if(ss_streq_(word, "single")) *out = TIMUI_BORDER_SINGLE;
    else if(ss_streq_(word, "double")) *out = TIMUI_BORDER_DOUBLE;
    else if(ss_streq_(word, "round") || ss_streq_(word, "rounded")) *out = TIMUI_BORDER_ROUND;
    else if(ss_streq_(word, "ascii")) *out = TIMUI_BORDER_ASCII;
    else if(ss_streq_(word, "shadow")) *out = TIMUI_BORDER_SHADOW;
    else return 0;
    return 1;
}
static int ss_selector_(TimuiStyleParser *p, TimuiStyleRule *r){
    char name[TIMUI_SS_NAME_MAX + 1];
    int have = 0;
    ss_skip_ws_(p);
    r->kind = TIMUI_WIDGET_ANY;
    if(p->pos < p->len && p->s[p->pos] == '*'){
        p->pos++;
        have = 1;
    } else if(p->pos < p->len && ss_is_alpha_(p->s[p->pos])){
        if(!ss_ident_(p, name, sizeof name)) return 0;
        if(!ss_widget_kind_(name, &r->kind)) return 0;
        r->specificity += 1;
        have = 1;
    }
    for(;;){
        uint32_t st;
        ss_skip_ws_(p);
        if(p->pos >= p->len) return 0;
        if(p->s[p->pos] == '#'){
            p->pos++;
            if(r->id[0] || !ss_ident_(p, r->id, sizeof r->id)) return 0;
            r->specificity += 100; have = 1;
        } else if(p->s[p->pos] == '.'){
            p->pos++;
            if(r->klass[0] || !ss_ident_(p, r->klass, sizeof r->klass)) return 0;
            r->specificity += 10; have = 1;
        } else if(p->s[p->pos] == ':'){
            p->pos++;
            if(!ss_ident_(p, name, sizeof name) || !ss_state_(name, &st)) return 0;
            r->states |= st;
            r->specificity += 10; have = 1;
        } else break;
    }
    return have;
}
static int ss_class_matches_(const char *classes, const char *klass){
    size_t klen, i = 0;
    if(!klass || !klass[0]) return 1;
    if(!classes) return 0;
    klen = strlen(klass);
    while(classes[i]){
        while(classes[i] && ss_is_space_(classes[i])) i++;
        if(!classes[i]) break;
        { size_t start = i;
          while(classes[i] && !ss_is_space_(classes[i])) i++;
          if(i - start == klen && memcmp(classes + start, klass, klen) == 0) return 1; }
    }
    return 0;
}
static int ss_rule_matches_(const TimuiStyleRule *r, TimuiStyleQuery q){
    if(r->kind != TIMUI_WIDGET_ANY && r->kind != q.kind) return 0;
    if(r->id[0] && (!q.id || strcmp(r->id, q.id) != 0)) return 0;
    if(!ss_class_matches_(q.classes, r->klass)) return 0;
    if((q.states & r->states) != r->states) return 0;
    return 1;
}
static TimuiResult ss_push_rule_(TimuiStylesheet *ss, const TimuiStyleRule *r){
    if(ss->count == ss->cap){
        int ncap = ss->cap ? ss->cap * 2 : 8;
        TimuiStyleRule *nr;
        if(ncap < ss->cap) return TIMUI_ERR_OUT_OF_MEMORY;
        if(ss->rules)
            nr = (TimuiStyleRule *)ss->alloc.realloc(ss->alloc.userdata, ss->rules,
                                                     (size_t)ss->cap * sizeof *ss->rules,
                                                     (size_t)ncap * sizeof *ss->rules);
        else
            nr = (TimuiStyleRule *)ss->alloc.alloc(ss->alloc.userdata,
                                                   (size_t)ncap * sizeof *ss->rules);
        if(!nr) return TIMUI_ERR_OUT_OF_MEMORY;
        ss->rules = nr;
        ss->cap = ncap;
    }
    ss->rules[ss->count++] = *r;
    return TIMUI_OK;
}
static int ss_decl_(TimuiStyleParser *p, TimuiStyleRule *r){
    char prop[TIMUI_SS_NAME_MAX + 1];
    if(!ss_ident_(p, prop, sizeof prop)) return 0;
    if(!ss_take_(p, ':')) return 0;
    if(ss_streq_(prop, "fg")){
        if(!ss_color_(p, &r->style.fg)) return 0;
        r->props |= TIMUI_STYLE_PROP_FG;
    } else if(ss_streq_(prop, "bg")){
        if(!ss_color_(p, &r->style.bg)) return 0;
        r->props |= TIMUI_STYLE_PROP_BG;
    } else if(ss_streq_(prop, "bold") || ss_streq_(prop, "dim") || ss_streq_(prop, "reverse")){
        uint32_t bit = ss_streq_(prop, "bold") ? TIMUI_ATTR_BOLD :
                       ss_streq_(prop, "dim") ? TIMUI_ATTR_DIM : TIMUI_ATTR_REVERSE;
        int on;
        if(!ss_bool_(p, &on)) return 0;
        r->attr_props |= bit;
        if(on) r->attr_values |= bit;
        else r->attr_values &= ~bit;
        r->props |= TIMUI_STYLE_PROP_ATTRS;
    } else if(ss_streq_(prop, "border")){
        if(!ss_border_(p, &r->border)) return 0;
        r->props |= TIMUI_STYLE_PROP_BORDER;
    } else if(ss_streq_(prop, "padding")){
        if(!ss_int_(p, &r->padding)) return 0;
        r->props |= TIMUI_STYLE_PROP_PADDING;
    } else if(ss_streq_(prop, "gap")){
        if(!ss_int_(p, &r->gap)) return 0;
        r->props |= TIMUI_STYLE_PROP_GAP;
    } else if(ss_streq_(prop, "gradient-lo")){
        if(!ss_color_(p, &r->gradient_lo)) return 0;
        r->props |= TIMUI_STYLE_PROP_GRADIENT_LO;
    } else if(ss_streq_(prop, "gradient-hi")){
        if(!ss_color_(p, &r->gradient_hi)) return 0;
        r->props |= TIMUI_STYLE_PROP_GRADIENT_HI;
    } else return 0;
    return ss_take_(p, ';');
}
TIMUI_API void timui_stylesheet_free(TimuiStylesheet *ss){
    if(!ss) return;
    if(ss->rules && ss_alloc_valid_(&ss->alloc))
        ss->alloc.free(ss->alloc.userdata, ss->rules, (size_t)ss->cap * sizeof *ss->rules);
    ss->rules = NULL;
    ss->count = 0;
    ss->cap = 0;
    memset(&ss->alloc, 0, sizeof ss->alloc);
}
TIMUI_API TimuiResult timui_stylesheet_parse(TimuiStylesheet *out, const char *src,
                                             size_t len, const TimuiAllocator *alloc){
    TimuiStyleParser p;
    TimuiResult gr;
    TimuiAllocator al;
    if(!out || (!src && len > 0) || !ss_alloc_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
    al = *alloc;
    memset(out, 0, sizeof *out);
    out->alloc = al;
    p.s = src ? src : "";
    p.len = len;
    p.pos = 0;
    while(1){
        TimuiStyleRule r;
        ss_skip_ws_(&p);
        if(p.pos >= p.len) return TIMUI_OK;
        memset(&r, 0, sizeof r);
        r.order = out->count;
        if(!ss_selector_(&p, &r) || !ss_take_(&p, '{')) goto protocol;
        while(!ss_at_(&p, '}')){
            if(p.pos >= p.len) goto protocol;
            if(!ss_decl_(&p, &r)) goto protocol;
        }
        p.pos++;
        gr = ss_push_rule_(out, &r);
        if(gr != TIMUI_OK){ timui_stylesheet_free(out); return gr; }
    }
protocol:
    timui_stylesheet_free(out);
    return TIMUI_ERR_PROTOCOL;
}
static void ss_apply_style_(TimuiResolvedStyle *res, uint32_t prop, int spec, int *best,
                            const TimuiStyleRule *r){
    if(spec < *best) return;
    *best = spec;
    res->mask |= prop;
    if(prop == TIMUI_STYLE_PROP_FG) res->style.fg = r->style.fg;
    else if(prop == TIMUI_STYLE_PROP_BG) res->style.bg = r->style.bg;
    else if(prop == TIMUI_STYLE_PROP_BORDER) res->border = r->border;
    else if(prop == TIMUI_STYLE_PROP_PADDING) res->padding = r->padding;
    else if(prop == TIMUI_STYLE_PROP_GAP) res->gap = r->gap;
    else if(prop == TIMUI_STYLE_PROP_GRADIENT_LO) res->gradient_lo = r->gradient_lo;
    else if(prop == TIMUI_STYLE_PROP_GRADIENT_HI) res->gradient_hi = r->gradient_hi;
}
TIMUI_API TimuiResolvedStyle timui_stylesheet_resolve(const TimuiStylesheet *ss,
                                                      TimuiStyleQuery query){
    enum { P_FG, P_BG, P_BOLD, P_DIM, P_REV, P_BORDER, P_PADDING, P_GAP, P_GLO, P_GHI, P_COUNT };
    int best[P_COUNT];
    TimuiResolvedStyle res;
    int i;
    res.style = query.base;
    res.mask = 0;
    res.border = TIMUI_BORDER_NONE;
    res.padding = 0;
    res.gap = 0;
    res.gradient_lo = 0;
    res.gradient_hi = 0;
    for(i = 0; i < P_COUNT; i++) best[i] = -1;
    if(!ss || !ss->rules) return res;
    for(i = 0; i < ss->count; i++){
        const TimuiStyleRule *r = &ss->rules[i];
        int spec = r->specificity;
        (void)r->order;
        if(!ss_rule_matches_(r, query)) continue;
        if(r->props & TIMUI_STYLE_PROP_FG) ss_apply_style_(&res, TIMUI_STYLE_PROP_FG, spec, &best[P_FG], r);
        if(r->props & TIMUI_STYLE_PROP_BG) ss_apply_style_(&res, TIMUI_STYLE_PROP_BG, spec, &best[P_BG], r);
        if((r->attr_props & TIMUI_ATTR_BOLD) && spec >= best[P_BOLD]){
            best[P_BOLD] = spec; res.mask |= TIMUI_STYLE_PROP_ATTRS;
            if(r->attr_values & TIMUI_ATTR_BOLD) res.style.attrs |= TIMUI_ATTR_BOLD;
            else res.style.attrs &= ~TIMUI_ATTR_BOLD;
        }
        if((r->attr_props & TIMUI_ATTR_DIM) && spec >= best[P_DIM]){
            best[P_DIM] = spec; res.mask |= TIMUI_STYLE_PROP_ATTRS;
            if(r->attr_values & TIMUI_ATTR_DIM) res.style.attrs |= TIMUI_ATTR_DIM;
            else res.style.attrs &= ~TIMUI_ATTR_DIM;
        }
        if((r->attr_props & TIMUI_ATTR_REVERSE) && spec >= best[P_REV]){
            best[P_REV] = spec; res.mask |= TIMUI_STYLE_PROP_ATTRS;
            if(r->attr_values & TIMUI_ATTR_REVERSE) res.style.attrs |= TIMUI_ATTR_REVERSE;
            else res.style.attrs &= ~TIMUI_ATTR_REVERSE;
        }
        if(r->props & TIMUI_STYLE_PROP_BORDER) ss_apply_style_(&res, TIMUI_STYLE_PROP_BORDER, spec, &best[P_BORDER], r);
        if(r->props & TIMUI_STYLE_PROP_PADDING) ss_apply_style_(&res, TIMUI_STYLE_PROP_PADDING, spec, &best[P_PADDING], r);
        if(r->props & TIMUI_STYLE_PROP_GAP) ss_apply_style_(&res, TIMUI_STYLE_PROP_GAP, spec, &best[P_GAP], r);
        if(r->props & TIMUI_STYLE_PROP_GRADIENT_LO) ss_apply_style_(&res, TIMUI_STYLE_PROP_GRADIENT_LO, spec, &best[P_GLO], r);
        if(r->props & TIMUI_STYLE_PROP_GRADIENT_HI) ss_apply_style_(&res, TIMUI_STYLE_PROP_GRADIENT_HI, spec, &best[P_GHI], r);
    }
    return res;
}

TIMUI_API void timui_set_stylesheet(Timui *ui, const TimuiStylesheet *ss){
    if(ui) ui->stylesheet = ss;
}

static TimuiStyle timui_widget_style_(Timui *ui, TimuiWidgetKind kind,
                                      TimuiStyleSlot slot, uint32_t states){
    TimuiStyle base;
    TimuiStyleQuery q;
    if(!ui) return timui_style_make(0, 0, 0);
    base = timui_theme_style(&ui->theme, slot);
    if(!ui->stylesheet) return base;
    q.kind = kind;
    q.id = NULL;
    q.classes = NULL;
    q.states = states;
    q.base = base;
    return timui_stylesheet_resolve(ui->stylesheet, q).style;
}

#undef TIMUI_SS_NAME_MAX
