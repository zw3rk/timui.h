/*
 * timui_syntax.c — table-driven syntax highlighter + read-only code viewer (W4).
 *
 * Promoted from the chat/sqlite examples' header-only highlighter into a first-
 * class library section. It scans a source span and emits, in source order, the
 * NON-default token spans (keywords, types, strings, comments, numbers, …);
 * whatever it does not emit is plain text (TIMUI_HL_TEXT) that the consumer
 * paints with the default colour. Keeping the token stream sparse lets a renderer
 * walk "gap, token, gap, token, …" trivially.
 *
 * Languages: "c", "sh"/"bash", "python"/"py", "sql" (case-insensitive
 * keywords/types, double-dash line comments, C-style block comments), and a
 * NULL/"" generic mode (strings, # and // line comments, block comments, numbers
 * — no keywords). An unknown language name falls back to generic.
 *
 * Design:
 *   - Table-driven: each language is a small TimuiHlLang descriptor (feature bits
 *     + keyword/type tables). One shared scan loop drives them all.
 *   - Bounded & pure: every scan helper advances by at least one byte (no
 *     infinite loops), reads only within [0,len), and treats bytes as unsigned so
 *     non-ASCII input can never be misclassified or overrun.
 *   - Whole-word keyword matches: a full identifier is scanned and matched exact,
 *     so `iffy` never matches `if`.
 *   - No allocation, no globals, no I/O. The tables are const (read-only, shared).
 *
 * The static helpers are prefixed timui_hl_ so this section coexists in one TU
 * with the examples' chat_highlight.h (which uses hl_* names) — chat.c and
 * sqlite_tui.c include both.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */

/* ----------------------------------------------------------------------- */
/* Character predicates. Each takes an int already holding an unsigned-char  */
/* value (0..255) so behaviour is well-defined for non-ASCII bytes.          */
/* ----------------------------------------------------------------------- */

static int timui_hl_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f' || c == '\v';
}
static int timui_hl_is_digit(int c) { return c >= '0' && c <= '9'; }
static int timui_hl_is_hexdigit(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static int timui_hl_is_ident_start(int c)
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int timui_hl_is_ident(int c)
{
    return timui_hl_is_ident_start(c) || timui_hl_is_digit(c);
}

/* ASCII punctuation not otherwise consumed as a string/comment/number/ident.
 * NUL is excluded so strchr's terminator can't produce a false positive. */
static int timui_hl_is_punct(int c)
{
    return c != 0 &&
           strchr("+-*/%=<>!&|^~?:;,.()[]{}@$#`\\", c) != NULL;
}

/* ----------------------------------------------------------------------- */
/* Span scanners. Each returns the index one past the scanned span; on a     */
/* truncated/unterminated span it returns `len` (never reads past it).       */
/* ----------------------------------------------------------------------- */

/* A single-line quoted run starting at the opening quote s[i]. `esc` enables
 * backslash escaping (so \" does not close the string). A newline ends an
 * unterminated string; the closing quote is included when present. */
static int timui_hl_scan_quoted(const char *s, int len, int i, char quote, int esc)
{
    int j = i + 1;
    while (j < len) {
        char ch = s[j];
        if (esc && ch == '\\') { j += 2; continue; } /* skip the escaped byte */
        if (ch == quote) return j + 1;
        if (ch == '\n') return j;                    /* unterminated at EOL */
        j++;
    }
    return len;                                      /* unterminated at EOF */
}

/* A Python triple-quoted string starting at s[i] (s[i..i+2] are all `q`). */
static int timui_hl_scan_triple(const char *s, int len, int i, char q)
{
    int j = i + 3;
    while (j < len) {
        if (s[j] == '\\') { j += 2; continue; }
        if (s[j] == q && j + 2 < len && s[j + 1] == q && s[j + 2] == q)
            return j + 3;
        j++;
    }
    return len;                                      /* unterminated */
}

/* A C block comment starting at s[i] (s[i]=='/', s[i+1]=='*'). */
static int timui_hl_scan_block(const char *s, int len, int i)
{
    int j = i + 2;
    while (j + 1 < len) {
        if (s[j] == '*' && s[j + 1] == '/') return j + 2;
        j++;
    }
    return len;                                      /* unterminated */
}

/* A line comment: from s[i] up to (not including) the next newline. */
static int timui_hl_scan_line(const char *s, int len, int i)
{
    int j = i;
    while (j < len && s[j] != '\n') j++;
    return j;
}

/* A number: decimal / hex (0x…) / float (frac + e/E exponent) with integer
 * and float suffixes (u l f). Called only when s[i] begins a number. */
static int timui_hl_scan_number(const char *s, int len, int i)
{
    int j = i;
    if (s[j] == '0' && j + 1 < len && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
        j += 2;
        while (j < len && timui_hl_is_hexdigit((unsigned char)s[j])) j++;
    } else {
        while (j < len && timui_hl_is_digit((unsigned char)s[j])) j++;
        if (j < len && s[j] == '.') {
            j++;
            while (j < len && timui_hl_is_digit((unsigned char)s[j])) j++;
        }
        if (j < len && (s[j] == 'e' || s[j] == 'E')) {
            int k = j + 1;
            if (k < len && (s[k] == '+' || s[k] == '-')) k++;
            if (k < len && timui_hl_is_digit((unsigned char)s[k])) {
                j = k + 1;
                while (j < len && timui_hl_is_digit((unsigned char)s[j])) j++;
            }
        }
    }
    while (j < len && s[j] != 0 && strchr("uUlLfF", s[j]) != NULL) j++;
    return j;
}

/* An identifier: [A-Za-z_][A-Za-z0-9_]* starting at s[i]. */
static int timui_hl_scan_ident(const char *s, int len, int i)
{
    int j = i;
    while (j < len && timui_hl_is_ident((unsigned char)s[j])) j++;
    return j;
}

/* A shell $-expansion at s[i]=='$': ${...}, $name, or a special param
 * ($#, $@, $*, $?, $!, $$, $-, $0..$9). Returns i+1 for a bare '$'. */
static int timui_hl_scan_dollar(const char *s, int len, int i)
{
    int j = i + 1;
    if (j >= len) return j;                          /* trailing '$' */
    if (s[j] == '{') {
        j++;
        while (j < len && s[j] != '}' && s[j] != '\n') j++;
        if (j < len && s[j] == '}') j++;             /* include '}' */
        return j;
    }
    if (timui_hl_is_ident_start((unsigned char)s[j])) {
        while (j < len && timui_hl_is_ident((unsigned char)s[j])) j++;
        return j;
    }
    if (s[j] != 0 && (strchr("#@*?!$-", s[j]) != NULL ||
                      timui_hl_is_digit((unsigned char)s[j])))
        return j + 1;
    return i + 1;                                    /* bare '$' */
}

/* A C preprocessor directive from the '#' at s[i] to end of line. Line
 * continuations (\<nl>) extend it; a string inside is skipped whole (so a //
 * inside it is not a comment); a real trailing line- or block-comment start
 * ends the directive so the comment itself stays highlighted as a comment. */
static int timui_hl_scan_preproc(const char *s, int len, int i)
{
    int j = i;
    while (j < len) {
        char ch = s[j];
        if (ch == '\n') return j;                    /* end of directive */
        if (ch == '\\' && j + 1 < len) { j += 2; continue; } /* continuation */
        if (ch == '"' || ch == '\'') { j = timui_hl_scan_quoted(s, len, j, ch, 1); continue; }
        if (ch == '/' && j + 1 < len && s[j + 1] == '/') return j; /* // */
        if (ch == '/' && j + 1 < len && s[j + 1] == '*') return j; /* block */
        j++;
    }
    return len;
}

/* ----------------------------------------------------------------------- */
/* Language descriptors (keyword/type tables are const; shared read-only).   */
/* ----------------------------------------------------------------------- */

static const char *const timui_hl_c_kw[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while", "asm", "_Complex",
    "_Imaginary", "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
    "_Static_assert", "_Thread_local", NULL
};
static const char *const timui_hl_c_ty[] = {
    "int", "char", "short", "long", "float", "double", "bool", "_Bool", NULL
};
static const char *const timui_hl_sh_kw[] = {
    "if", "then", "elif", "else", "fi", "for", "while", "until", "do", "done",
    "case", "esac", "in", "function", "select", "return", "local", "export",
    NULL
};
static const char *const timui_hl_py_kw[] = {
    "def", "class", "if", "elif", "else", "for", "while", "return", "import",
    "from", "as", "with", "try", "except", "finally", "lambda", "None", "True",
    "False", "and", "or", "not", "in", "is", "pass", "break", "continue",
    "global", "nonlocal", "yield", "raise", "assert", "del", "async", "await",
    NULL
};
/* SQL (matched case-insensitively via TimuiHlLang.nocase — stored lowercase).
 * Covers common DML/DDL + clause + operator keywords; SQLite-flavoured. */
static const char *const timui_hl_sql_kw[] = {
    "select", "from", "where", "insert", "into", "values", "update", "set",
    "delete", "create", "table", "index", "view", "trigger", "drop", "alter",
    "add", "rename", "column", "join", "inner", "left", "right", "outer",
    "full", "cross", "natural", "on", "using", "group", "by", "order",
    "having", "limit", "offset", "as", "distinct", "all", "union", "intersect",
    "except", "and", "or", "not", "null", "is", "in", "like", "glob", "regexp",
    "match", "between", "exists", "case", "when", "then", "else", "end",
    "pragma", "begin", "commit", "rollback", "savepoint", "release",
    "transaction", "if", "primary", "key", "foreign", "references", "unique",
    "check", "default", "autoincrement", "constraint", "collate", "asc",
    "desc", "with", "recursive", "replace", "conflict", "abort", "fail",
    "ignore", "vacuum", "analyze", "reindex", "attach", "detach", "explain",
    "cast", "returning", "without", "rowid", "temp", "temporary", "escape",
    "nulls", "first", "last", "over", "partition", "window", "filter",
    NULL
};
/* SQL type / affinity names (SQLite is affinity-based, so these are advisory). */
static const char *const timui_hl_sql_ty[] = {
    "integer", "int", "smallint", "bigint", "tinyint", "text", "varchar",
    "char", "nchar", "nvarchar", "clob", "blob", "real", "double", "float",
    "numeric", "decimal", "boolean", "bool", "date", "datetime", "timestamp",
    "time", NULL
};

/* Feature bits + tables for one language. */
typedef struct {
    const char *const *kw;    /* keyword table (NULL-terminated) or NULL */
    const char *const *ty;    /* type table (NULL-terminated) or NULL */
    unsigned line_hash  : 1;  /* '#' starts a line comment (after ws/BOL) */
    unsigned line_slash : 1;  /* '//' starts a line comment */
    unsigned line_dash  : 1;  /* '--' starts a line comment (SQL) */
    unsigned block      : 1;  /* C-style block comments */
    unsigned preproc    : 1;  /* '#' at BOL = whole-line preprocessor */
    unsigned triple     : 1;  /* triple-quoted strings */
    unsigned dollar     : 1;  /* $VAR / ${…} expansions */
    unsigned sq_char    : 1;  /* single quote is a C char literal */
    unsigned sq_escape  : 1;  /* backslash escapes inside single-quoted strings */
    unsigned t_heur     : 1;  /* identifiers ending in _t are types */
    unsigned nocase     : 1;  /* keyword/type matching is case-insensitive (SQL) */
} TimuiHlLang;

static TimuiHlLang timui_hl_lang_for(const char *lang)
{
    TimuiHlLang L;
    memset(&L, 0, sizeof L);

    if (lang != NULL && strcmp(lang, "c") == 0) {
        L.kw = timui_hl_c_kw; L.ty = timui_hl_c_ty;
        L.line_slash = 1; L.block = 1; L.preproc = 1;
        L.sq_char = 1; L.sq_escape = 1; L.t_heur = 1;
        return L;
    }
    if (lang != NULL && (strcmp(lang, "sh") == 0 || strcmp(lang, "bash") == 0)) {
        L.kw = timui_hl_sh_kw;
        L.line_hash = 1; L.dollar = 1; L.sq_escape = 0; /* sh '' is literal */
        return L;
    }
    if (lang != NULL && (strcmp(lang, "python") == 0 || strcmp(lang, "py") == 0)) {
        L.kw = timui_hl_py_kw;
        L.line_hash = 1; L.triple = 1; L.sq_escape = 1;
        return L;
    }
    if (lang != NULL && strcmp(lang, "sql") == 0) {
        L.kw = timui_hl_sql_kw; L.ty = timui_hl_sql_ty;
        L.line_dash = 1; L.block = 1; L.nocase = 1;
        /* SQL strings are single-quoted with '' doubling (no backslash escape);
         * double quotes are identifiers. Both scan as string spans here. */
        L.sq_escape = 0;
        return L;
    }
    /* generic (NULL / "" / unknown): both comment styles, both string quotes,
     * numbers, no keywords. */
    L.line_hash = 1; L.line_slash = 1; L.block = 1; L.sq_escape = 1;
    return L;
}

/* ASCII lowercase — for case-insensitive keyword matching (SQL). */
static int timui_hl_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Whole-word membership test for the span code[off..off+n) against a
 * NULL-terminated table. `nocase` compares ASCII case-insensitively. */
static int timui_hl_in_list(const char *code, int off, int n,
                            const char *const *list, int nocase)
{
    int k;
    if (list == NULL) return 0;
    for (k = 0; list[k] != NULL; k++) {
        if ((int)strlen(list[k]) != n) continue;
        if (!nocase) {
            if (memcmp(code + off, list[k], (size_t)n) == 0) return 1;
        } else {
            int j, eq = 1;
            for (j = 0; j < n; j++)
                if (timui_hl_lower((unsigned char)code[off + j]) !=
                    timui_hl_lower((unsigned char)list[k][j])) { eq = 0; break; }
            if (eq) return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Public: highlighter.                                                      */
/* ----------------------------------------------------------------------- */

TIMUI_API int timui_highlight(const char *src, int len, const char *lang,
                              TimuiHlTok *out, int max)
{
    TimuiHlLang L;
    int n = 0, i = 0, at_bol = 1;

    if (src == NULL || len <= 0 || out == NULL || max <= 0) return 0;
    L = timui_hl_lang_for(lang);

    /* Each iteration handles the byte at `i` and pushes AT MOST one token.
     * We enter the loop only while n < max, so a push can never overflow. */
    while (i < len && n < max) {
        int c = (unsigned char)src[i];
        int start = i, end;
        int bol;
        TimuiHlClass cls;

        /* Whitespace and newlines are HL_TEXT gaps — never emitted. */
        if (c == '\n') { at_bol = 1; i++; continue; }
        if (timui_hl_is_space(c)) { i++; continue; }

        bol = at_bol;   /* is this the first non-space token on its line? */
        at_bol = 0;

        /* Comments first, so '/' and '#' cannot be seen as punctuation. */
        if (L.block && c == '/' && i + 1 < len && src[i + 1] == '*') {
            end = timui_hl_scan_block(src, len, i); cls = TIMUI_HL_COMMENT;
        } else if (L.line_slash && c == '/' && i + 1 < len && src[i + 1] == '/') {
            end = timui_hl_scan_line(src, len, i); cls = TIMUI_HL_COMMENT;
        } else if (L.line_dash && c == '-' && i + 1 < len && src[i + 1] == '-') {
            end = timui_hl_scan_line(src, len, i); cls = TIMUI_HL_COMMENT;
        } else if (L.preproc && c == '#' && bol) {
            end = timui_hl_scan_preproc(src, len, i); cls = TIMUI_HL_PREPROC;
        } else if (L.line_hash && c == '#' &&
                   (i == 0 || timui_hl_is_space((unsigned char)src[i - 1]))) {
            end = timui_hl_scan_line(src, len, i); cls = TIMUI_HL_COMMENT;
        }
        /* Strings and character literals. */
        else if (c == '"') {
            if (L.triple && i + 2 < len && src[i + 1] == '"' && src[i + 2] == '"')
                end = timui_hl_scan_triple(src, len, i, '"');
            else
                end = timui_hl_scan_quoted(src, len, i, '"', 1);
            cls = TIMUI_HL_STRING;
        } else if (c == '\'') {
            if (L.sq_char) {
                end = timui_hl_scan_quoted(src, len, i, '\'', 1); cls = TIMUI_HL_CHAR;
            } else if (L.triple && i + 2 < len &&
                       src[i + 1] == '\'' && src[i + 2] == '\'') {
                end = timui_hl_scan_triple(src, len, i, '\''); cls = TIMUI_HL_STRING;
            } else {
                end = timui_hl_scan_quoted(src, len, i, '\'', (int)L.sq_escape);
                cls = TIMUI_HL_STRING;
            }
        }
        /* Shell variable expansion. */
        else if (L.dollar && c == '$') {
            end = timui_hl_scan_dollar(src, len, i);
            cls = (end == start + 1) ? TIMUI_HL_PUNCT : TIMUI_HL_TYPE; /* bare '$' -> punct */
        }
        /* Numbers. */
        else if (timui_hl_is_digit(c) ||
                 (c == '.' && i + 1 < len &&
                  timui_hl_is_digit((unsigned char)src[i + 1]))) {
            end = timui_hl_scan_number(src, len, i); cls = TIMUI_HL_NUMBER;
        }
        /* Identifiers: keyword / type / *_t heuristic, else plain text. */
        else if (timui_hl_is_ident_start(c)) {
            int tl;
            end = timui_hl_scan_ident(src, len, i);
            tl = end - start;
            if (timui_hl_in_list(src, start, tl, L.kw, L.nocase)) cls = TIMUI_HL_KEYWORD;
            else if (timui_hl_in_list(src, start, tl, L.ty, L.nocase)) cls = TIMUI_HL_TYPE;
            else if (L.t_heur && tl > 2 &&
                     src[end - 2] == '_' && src[end - 1] == 't') cls = TIMUI_HL_TYPE;
            else { i = end; continue; }  /* plain identifier => HL_TEXT gap */
        }
        /* Punctuation. */
        else if (timui_hl_is_punct(c)) {
            end = i + 1; cls = TIMUI_HL_PUNCT;
        }
        /* Anything else (non-ASCII bytes, NUL, …) is text. */
        else { i++; continue; }

        out[n].off = start;
        out[n].len = end - start;
        out[n].cls = cls;
        n++;
        i = end;
    }
    return n;
}

/* ----------------------------------------------------------------------- */
/* Public: class -> default colour (a Night-Owl-ish palette over CODE_BG).   */
/* ----------------------------------------------------------------------- */

/* Read-only code viewer palette (matches the chat example's fenced blocks). */
#define TIMUI_CODE_BG     0x1B1E2Bu   /* subtle code-block background */
#define TIMUI_CODE_FG     0xD6DEEBu   /* default code text colour     */
#define TIMUI_CODE_GUTTER 0x5C6478u   /* muted line-number gutter     */

TIMUI_API uint32_t timui_hl_color(TimuiHlClass cls)
{
    switch (cls) {
        case TIMUI_HL_KEYWORD: return 0xC792EAu;                        /* purple */
        case TIMUI_HL_TYPE:    return 0x82AAFFu;                        /* blue   */
        case TIMUI_HL_STRING:  /* fall through: string + char share green */
        case TIMUI_HL_CHAR:    return 0xC3E88Du;                        /* green  */
        case TIMUI_HL_COMMENT: return 0x7A88A0u;                        /* muted  */
        case TIMUI_HL_NUMBER:  return 0xF78C6Cu;                        /* orange */
        case TIMUI_HL_PREPROC: return 0xFFCB6Bu;                        /* yellow */
        case TIMUI_HL_PUNCT:   return 0x89DDFFu;                        /* cyan   */
        case TIMUI_HL_TEXT:    /* fall through */
        default:               return TIMUI_CODE_FG;
    }
}

/* ----------------------------------------------------------------------- */
/* Public: read-only code viewer.                                            */
/* ----------------------------------------------------------------------- */

/* Clamp a top-line scroll offset to [0, max(0, nlines - visible)]. Pure. */
TIMUI_API int timui_code_scroll_clamp(int scroll, int nlines, int visible)
{
    int maxscroll;
    if (nlines < 0) nlines = 0;
    if (visible < 0) visible = 0;
    maxscroll = nlines - visible;
    if (maxscroll < 0) maxscroll = 0;
    if (scroll < 0) scroll = 0;
    if (scroll > maxscroll) scroll = maxscroll;
    return scroll;
}

/* Count the '\n'-separated lines in src[0..len): 1 + the number of newlines
 * (an empty buffer still has one, empty, line). */
static int timui_hl_count_lines(const char *src, int len)
{
    int i, n = 1;
    for (i = 0; i < len; i++) if (src[i] == '\n') n++;
    return n;
}

/* Decimal digit count of a positive line number (>=1 => at least 1). */
static int timui_hl_digits(int n)
{
    int d = 1;
    if (n < 1) return 1;
    while (n >= 10) { n /= 10; d++; }
    return d;
}

/* Render the 1-based line number `ln` right-aligned into `out` (which must hold
 * w+1 bytes), left-padded with spaces and NUL-terminated. */
static void timui_hl_fmt_lineno(int ln, int w, char *out)
{
    int i = w;
    out[w] = '\0';
    while (i > 0) { i--; out[i] = ' '; }
    i = w - 1;
    if (ln < 1) ln = 1;
    while (ln > 0 && i >= 0) { out[i--] = (char)('0' + ln % 10); ln /= 10; }
}

/* Draw one already-isolated source line at row `y`, columns [x0, x1), with a
 * per-token syntax colour over CODE_BG. Not wrapped — clips at x1 (and the
 * caller's pushed clip guards the buffer edges). */
static void timui_hl_draw_line(TimuiFrame *f, int x0, int y, int x1,
                               const char *line, int llen, const char *lang)
{
    TimuiHlTok toks[256];
    int nt = timui_highlight(line, llen, lang, toks, 256);
    int ti = 0, col = x0, i = 0;
    while (i < llen && col < x1) {
        TimuiHlClass cls = TIMUI_HL_TEXT;
        TimuiStr ch;
        uint32_t cp = 0;
        int adv, w;
        /* Advance past tokens that end at/before this byte, then adopt the one
         * covering it (gaps stay TIMUI_HL_TEXT). */
        while (ti < nt && i >= toks[ti].off + toks[ti].len) ti++;
        if (ti < nt && i >= toks[ti].off) cls = toks[ti].cls;
        adv = timui_utf8_decode(line + i, (size_t)(llen - i), &cp);
        if (adv <= 0) adv = 1;
        ch.ptr = line + i; ch.len = (size_t)adv;
        timui_label(f, col, y, ch, timui_style_make(timui_hl_color(cls), TIMUI_CODE_BG, 0));
        w = timui_utf8_width(cp);
        col += w > 0 ? w : 0;
        i += adv;
    }
}

TIMUI_API void timui_code(TimuiFrame *f, TimuiRect r, const char *src, int len,
                          const char *lang, int *scroll)
{
    TimuiCellBuffer *buf;
    int nlines, top, digits, gutter, codex, row, lo;

    if (!f || !src || r.w <= 0 || r.h <= 0) return;
    if (len < 0) len = 0;
    buf = timui_frame_buffer(f);
    if (!buf) return;

    nlines = timui_hl_count_lines(src, len);

    /* Clamp the scroll offset (in place) so neither end overscrolls. */
    top = scroll ? timui_code_scroll_clamp(*scroll, nlines, r.h) : 0;
    if (scroll) *scroll = top;

    /* A line-number gutter "<num> " when the rect is wide enough to leave room
     * for at least one column of code; otherwise draw code flush-left. */
    digits = timui_hl_digits(nlines);
    gutter = digits + 1;                 /* digits + a single-space separator */
    codex  = (r.w > gutter + 1) ? r.x + gutter : r.x;

    /* Subtle code background across the whole rect, then draw on top of it. */
    timui_draw_fill(buf, r, timui_style_make(TIMUI_CODE_FG, TIMUI_CODE_BG, 0));
    timui_push_clip(f, r);               /* guard glyphs against the rect edges */

    /* Walk src to the first visible line, then render r.h rows. */
    lo = 0;
    { int skipped = 0;
      while (skipped < top && lo < len) { if (src[lo] == '\n') skipped++; lo++; } }

    for (row = 0; row < r.h; row++) {
        int lineno = top + row + 1;      /* 1-based */
        int y = r.y + row, hi;
        if (top + row >= nlines) break;  /* past the last line -> just background */

        /* This line spans [lo, hi); hi is the next '\n' or end of buffer. */
        hi = lo;
        while (hi < len && src[hi] != '\n') hi++;

        if (codex != r.x) {              /* draw the gutter number (right-aligned) */
            char num[24];
            int nw = digits < (int)sizeof num - 1 ? digits : (int)sizeof num - 1;
            timui_hl_fmt_lineno(lineno, nw, num);
            timui_label(f, r.x, y, timui_str_from_cstr(num),
                        timui_style_make(TIMUI_CODE_GUTTER, TIMUI_CODE_BG, 0));
        }
        timui_hl_draw_line(f, codex, y, r.x + r.w, src + lo, hi - lo, lang);

        lo = (hi < len) ? hi + 1 : hi;   /* step past the newline to the next line */
    }

    timui_pop_clip(f);
}
