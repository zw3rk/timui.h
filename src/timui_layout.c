/*
 * timui_layout.c — ratatui-style constraint layout solver.
 *
 * timui_split divides an area along an axis into contiguous child rects. Fixed
 * (LEN/PCT) sizes are allocated first; the leftover is shared across the
 * flexible children (FLEX/MIN/MAX) by weight, with the LAST flexible child
 * absorbing the rounding remainder so the children tile the area EXACTLY (no
 * gaps or overlaps). MIN/MAX lower/upper bounds are honoured by a small
 * freeze-and-redistribute loop (CSS-flexbox style): each round hands the
 * remaining space to the still-free flexible children, then freezes any child
 * that lands outside its bound at that bound and repeats — terminating in at
 * most one round per flexible child. All sizes are clamped non-negative, and
 * contiguous placement clamps to the area boundary so over-constrained inputs
 * never produce negative or overflowing rects.
 *
 * timui_grid composes two splits (rows then columns); timui_split_h/_v are thin
 * axis-fixing wrappers. Everything here is a pure function of its arguments.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */

/* Upper bound on children per split — sizes the stack scratch arrays. A TUI
 * never splits one area into this many pieces; larger n is rejected (returns 0)
 * rather than silently truncated. */
#define TIMUI_LAYOUT_MAX 128

static int timui_sat_i64_(int64_t v){
    if(v < INT_MIN) return INT_MIN;
    if(v > INT_MAX) return INT_MAX;
    return (int)v;
}

/* Round a * num / den to the nearest integer (den > 0; a, num >= 0). */
static int64_t timui_round_div_(int64_t a, int64_t num, int64_t den){
    if(den <= 0) return 0;
    return (a * num + den / 2) / den;
}

/* A flexible constraint shares in the leftover space (FLEX/MIN/MAX). */
static int timui_con_is_flex_(TimuiConstraintKind k){
    return k == TIMUI_CON_FLEX || k == TIMUI_CON_MIN || k == TIMUI_CON_MAX;
}

/* Weight of a flexible constraint: FLEX uses its value (clamped >= 0); MIN/MAX
 * are single-weight fill segments. */
static int64_t timui_con_weight_(const TimuiConstraint *c){
    if(c->kind == TIMUI_CON_FLEX) return c->value > 0 ? c->value : 0;
    return 1;   /* MIN / MAX */
}

TIMUI_API int timui_split_ex(TimuiRect area, TimuiAxis axis, const TimuiConstraint *cons,
                             int n, TimuiLayoutOpts opts, TimuiRect *out){
    int64_t size[TIMUI_LAYOUT_MAX];
    char locked[TIMUI_LAYOUT_MAX];
    int  i;
    int64_t gap, margin;
    int64_t axis_start, axis_len, cross_start, cross_len;
    int64_t inner_start, inner_len, cross_inner_start, cross_inner_len;
    int64_t avail, fixed_sum, leftover, remaining, pos, end;

    if(!cons || !out || n <= 0 || n > TIMUI_LAYOUT_MAX) return 0;

    gap    = opts.gap    > 0 ? opts.gap    : 0;
    margin = opts.margin > 0 ? opts.margin : 0;

    /* Project the area onto (axis, cross): H splits width, V splits height. */
    if(axis == TIMUI_AXIS_V){
        axis_start = area.y; axis_len = area.h; cross_start = area.x; cross_len = area.w;
    } else {
        axis_start = area.x; axis_len = area.w; cross_start = area.y; cross_len = area.h;
    }

    /* Outer margin shrinks both axes by `margin` on each side. */
    inner_start       = axis_start + margin;
    inner_len         = axis_len - 2 * margin;  if(inner_len < 0)       inner_len = 0;
    cross_inner_start = cross_start + margin;
    cross_inner_len   = cross_len - 2 * margin; if(cross_inner_len < 0) cross_inner_len = 0;

    /* Space the children actually divide — the (n-1) gaps live between them. */
    avail = inner_len - gap * (n - 1); if(avail < 0) avail = 0;

    /* Pass 1: fixed (LEN, PCT) sizes; flexible children start at 0. */
    fixed_sum = 0;
    for(i = 0; i < n; i++){
        locked[i] = 0;
        if(cons[i].kind == TIMUI_CON_LEN){
            size[i] = cons[i].value > 0 ? cons[i].value : 0;
            fixed_sum += size[i];
        } else if(cons[i].kind == TIMUI_CON_PCT){
            int64_t p = cons[i].value < 0 ? 0 : cons[i].value;
            size[i] = timui_round_div_(avail, p, 100);
            fixed_sum += size[i];
        } else {
            size[i] = 0;   /* flexible — resolved in pass 2 */
        }
    }

    leftover = avail - fixed_sum; if(leftover < 0) leftover = 0;

    /* Pass 2: distribute the leftover across flexible children by weight,
     * honouring MIN/MAX bounds via freeze-and-redistribute. */
    remaining = leftover;
    for(;;){
        int64_t free_weight = 0, assigned = 0;
        int last_free = -1, changed = 0;
        for(i = 0; i < n; i++)
            if(timui_con_is_flex_(cons[i].kind) && !locked[i]){
                free_weight += timui_con_weight_(&cons[i]);
                last_free = i;
            }
        if(last_free < 0) break;   /* no free flexible children left */
        if(free_weight <= 0) break; /* all remaining flex children asked for zero */

        /* Provisional shares — the last free child gets the exact remainder so
         * the free children always sum to `remaining` (no rounding gap). */
        for(i = 0; i < n; i++){
            if(!timui_con_is_flex_(cons[i].kind) || locked[i]) continue;
            if(i == last_free){
                size[i] = remaining - assigned;
            } else {
                size[i] = timui_round_div_(remaining, timui_con_weight_(&cons[i]), free_weight);
                assigned += size[i];
            }
            if(size[i] < 0) size[i] = 0;
        }

        /* Freeze any child whose provisional share violates its bound. */
        for(i = 0; i < n; i++){
            if(!timui_con_is_flex_(cons[i].kind) || locked[i]) continue;
            if(cons[i].kind == TIMUI_CON_MIN && size[i] < cons[i].value){
                size[i] = cons[i].value < 0 ? 0 : cons[i].value;
                locked[i] = 1; remaining -= size[i]; changed = 1;
            } else if(cons[i].kind == TIMUI_CON_MAX && cons[i].value >= 0 && size[i] > cons[i].value){
                size[i] = cons[i].value;
                locked[i] = 1; remaining -= size[i]; changed = 1;
            }
        }
        if(remaining < 0) remaining = 0;
        if(!changed) break;   /* all free shares within bounds — solved */
    }

    /* Pass 3: place children contiguously, clamping each so it never goes
     * negative or overflows the area (the over-constrained case). */
    pos = inner_start;
    end = inner_start + inner_len;
    for(i = 0; i < n; i++){
        int64_t s = size[i], rem;
        if(i > 0) pos += gap;
        rem = end - pos; if(rem < 0) rem = 0;
        if(s > rem) s = rem;
        if(s < 0) s = 0;
        if(axis == TIMUI_AXIS_V){
            out[i].x = timui_sat_i64_(cross_inner_start); out[i].w = timui_sat_i64_(cross_inner_len);
            out[i].y = timui_sat_i64_(pos);               out[i].h = timui_sat_i64_(s);
        } else {
            out[i].x = timui_sat_i64_(pos);               out[i].w = timui_sat_i64_(s);
            out[i].y = timui_sat_i64_(cross_inner_start); out[i].h = timui_sat_i64_(cross_inner_len);
        }
        pos += s;
    }
    return n;
}

TIMUI_API int timui_split(TimuiRect area, TimuiAxis axis, const TimuiConstraint *cons, int n, TimuiRect *out){
    TimuiLayoutOpts o; o.gap = 0; o.margin = 0;
    return timui_split_ex(area, axis, cons, n, o, out);
}
TIMUI_API int timui_split_h(TimuiRect area, const TimuiConstraint *cons, int n, TimuiRect *out){
    return timui_split(area, TIMUI_AXIS_H, cons, n, out);
}
TIMUI_API int timui_split_v(TimuiRect area, const TimuiConstraint *cons, int n, TimuiRect *out){
    return timui_split(area, TIMUI_AXIS_V, cons, n, out);
}

TIMUI_API int timui_grid_ex(TimuiRect area, const TimuiConstraint *rows, int nr,
                            const TimuiConstraint *cols, int nc, TimuiLayoutOpts opts, TimuiRect *out){
    TimuiRect rowrects[TIMUI_LAYOUT_MAX];
    int r;
    if(!rows || !cols || !out || nr <= 0 || nc <= 0 ||
       nr > TIMUI_LAYOUT_MAX || nc > TIMUI_LAYOUT_MAX || nr > INT_MAX / nc) return 0;
    /* Rows carve `area` into vertical bands; each band is then split into cells
     * by the column constraints. Output is row-major: out[r*nc + c]. */
    if(timui_split_ex(area, TIMUI_AXIS_V, rows, nr, opts, rowrects) != nr) return 0;
    for(r = 0; r < nr; r++)
        if(timui_split_ex(rowrects[r], TIMUI_AXIS_H, cols, nc, opts, out + (size_t)r * nc) != nc) return 0;
    return nr * nc;
}
TIMUI_API int timui_grid(TimuiRect area, const TimuiConstraint *rows, int nr,
                         const TimuiConstraint *cols, int nc, TimuiRect *out){
    TimuiLayoutOpts o; o.gap = 0; o.margin = 0;
    return timui_grid_ex(area, rows, nr, cols, nc, o, out);
}

#undef TIMUI_LAYOUT_MAX
