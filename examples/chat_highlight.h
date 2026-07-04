/*
 * chat_highlight.h — a tiny, self-contained syntax highlighter for the timui.h
 * chat example. Header-only, pure C99, no allocation, no globals, no I/O.
 *
 * It scans a source span and emits, in source order, the NON-default spans
 * (keywords, types, strings, comments, numbers, …). Whatever it does NOT emit
 * a token for is plain text (HL_TEXT) — the consumer paints the gaps with the
 * default colour. This keeps the token stream small and lets a renderer walk
 * "gap, token, gap, token, …" trivially.
 *
 * Languages: "c", "sh"/"bash", "python"/"py", and a NULL/"" generic mode
 * (strings, # and // line comments, block comments, numbers — no keywords).
 * An unknown language name falls back to generic.
 *
 * Design notes:
 *   - Table-driven: each language is a small HlLang descriptor (a set of feature
 *     bits + keyword/type tables). The scanner is one shared loop.
 *   - Bounded & pure: every scan helper advances by at least one byte (no
 *     infinite loops), reads only within [0,len), and treats bytes as unsigned
 *     so non-ASCII input can never be misclassified or overrun.
 *   - Keyword matches are whole-word: we scan a full identifier and match the
 *     exact span, so `iffy` never matches `if`.
 *   - Shell $-expansions ($VAR, ${...}) reuse the HL_TYPE colour bucket; there
 *     is no dedicated variable class in the public enum.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#ifndef CHAT_HIGHLIGHT_H
#define CHAT_HIGHLIGHT_H

#include <stddef.h>
#include <string.h>

/* Token classes. HL_TEXT (0) is the default; it is never emitted — gaps
 * between emitted tokens are implicitly HL_TEXT. */
typedef enum {
    HL_TEXT, HL_KEYWORD, HL_TYPE, HL_STRING, HL_CHAR,
    HL_COMMENT, HL_NUMBER, HL_PREPROC, HL_PUNCT
} HlClass;

/* A highlighted span: byte offset + length into the input, and its class. */
typedef struct { int off, len; HlClass cls; } HlTok;

/* ----------------------------------------------------------------------- */
/* Character predicates. All take an int that is already an unsigned-char    */
/* value (0..255) so behaviour is well-defined for non-ASCII bytes.          */
/* ----------------------------------------------------------------------- */

static int hl_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f' || c == '\v';
}
static int hl_is_digit(int c) { return c >= '0' && c <= '9'; }
static int hl_is_hexdigit(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static int hl_is_ident_start(int c)
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int hl_is_ident(int c) { return hl_is_ident_start(c) || hl_is_digit(c); }

/* ASCII punctuation not otherwise consumed as a string/comment/number/ident.
 * NUL is excluded so strchr's terminator can't produce a false positive. */
static int hl_is_punct(int c)
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
static int hl_scan_quoted(const char *s, int len, int i, char quote, int esc)
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
static int hl_scan_triple(const char *s, int len, int i, char q)
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
static int hl_scan_block(const char *s, int len, int i)
{
    int j = i + 2;
    while (j + 1 < len) {
        if (s[j] == '*' && s[j + 1] == '/') return j + 2;
        j++;
    }
    return len;                                      /* unterminated */
}

/* A line comment: from s[i] up to (not including) the next newline. */
static int hl_scan_line(const char *s, int len, int i)
{
    int j = i;
    while (j < len && s[j] != '\n') j++;
    return j;
}

/* A number: decimal / hex (0x…) / float (frac + e/E exponent) with integer
 * and float suffixes (u l f). Called only when s[i] begins a number. */
static int hl_scan_number(const char *s, int len, int i)
{
    int j = i;
    if (s[j] == '0' && j + 1 < len && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
        j += 2;
        while (j < len && hl_is_hexdigit((unsigned char)s[j])) j++;
    } else {
        while (j < len && hl_is_digit((unsigned char)s[j])) j++;
        if (j < len && s[j] == '.') {
            j++;
            while (j < len && hl_is_digit((unsigned char)s[j])) j++;
        }
        if (j < len && (s[j] == 'e' || s[j] == 'E')) {
            int k = j + 1;
            if (k < len && (s[k] == '+' || s[k] == '-')) k++;
            if (k < len && hl_is_digit((unsigned char)s[k])) {
                j = k + 1;
                while (j < len && hl_is_digit((unsigned char)s[j])) j++;
            }
        }
    }
    while (j < len && s[j] != 0 && strchr("uUlLfF", s[j]) != NULL) j++;
    return j;
}

/* An identifier: [A-Za-z_][A-Za-z0-9_]* starting at s[i]. */
static int hl_scan_ident(const char *s, int len, int i)
{
    int j = i;
    while (j < len && hl_is_ident((unsigned char)s[j])) j++;
    return j;
}

/* A shell $-expansion at s[i]=='$': ${...}, $name, or a special param
 * ($#, $@, $*, $?, $!, $$, $-, $0..$9). Returns i+1 for a bare '$'. */
static int hl_scan_dollar(const char *s, int len, int i)
{
    int j = i + 1;
    if (j >= len) return j;                          /* trailing '$' */
    if (s[j] == '{') {
        j++;
        while (j < len && s[j] != '}' && s[j] != '\n') j++;
        if (j < len && s[j] == '}') j++;             /* include '}' */
        return j;
    }
    if (hl_is_ident_start((unsigned char)s[j])) {
        while (j < len && hl_is_ident((unsigned char)s[j])) j++;
        return j;
    }
    if (s[j] != 0 && (strchr("#@*?!$-", s[j]) != NULL ||
                      hl_is_digit((unsigned char)s[j])))
        return j + 1;
    return i + 1;                                    /* bare '$' */
}

/* A C preprocessor directive from the '#' at s[i] to end of line. Line
 * continuations (\<nl>) extend it; a string inside is skipped whole (so a //
 * inside it is not a comment); a real trailing line- or block-comment start
 * ends the directive so the comment itself stays highlighted as a comment. */
static int hl_scan_preproc(const char *s, int len, int i)
{
    int j = i;
    while (j < len) {
        char ch = s[j];
        if (ch == '\n') return j;                    /* end of directive */
        if (ch == '\\' && j + 1 < len) { j += 2; continue; } /* continuation */
        if (ch == '"' || ch == '\'') { j = hl_scan_quoted(s, len, j, ch, 1); continue; }
        if (ch == '/' && j + 1 < len && s[j + 1] == '/') return j; /* // */
        if (ch == '/' && j + 1 < len && s[j + 1] == '*') return j; /* block */
        j++;
    }
    return len;
}

/* ----------------------------------------------------------------------- */
/* Language descriptors.                                                     */
/* ----------------------------------------------------------------------- */

static const char *const hl_c_kw[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while", "asm", "_Complex",
    "_Imaginary", "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
    "_Static_assert", "_Thread_local", NULL
};
static const char *const hl_c_ty[] = {
    "int", "char", "short", "long", "float", "double", "bool", "_Bool", NULL
};
static const char *const hl_sh_kw[] = {
    "if", "then", "elif", "else", "fi", "for", "while", "until", "do", "done",
    "case", "esac", "in", "function", "select", "return", "local", "export",
    NULL
};
static const char *const hl_py_kw[] = {
    "def", "class", "if", "elif", "else", "for", "while", "return", "import",
    "from", "as", "with", "try", "except", "finally", "lambda", "None", "True",
    "False", "and", "or", "not", "in", "is", "pass", "break", "continue",
    "global", "nonlocal", "yield", "raise", "assert", "del", "async", "await",
    NULL
};

/* Feature bits + tables for one language. */
typedef struct {
    const char *const *kw;    /* keyword table (NULL-terminated) or NULL */
    const char *const *ty;    /* type table (NULL-terminated) or NULL */
    unsigned line_hash  : 1;  /* '#' starts a line comment (after ws/BOL) */
    unsigned line_slash : 1;  /* '//' starts a line comment */
    unsigned block      : 1;  /* C-style block comments */
    unsigned preproc    : 1;  /* '#' at BOL = whole-line preprocessor */
    unsigned triple     : 1;  /* triple-quoted strings */
    unsigned dollar     : 1;  /* $VAR / ${…} expansions */
    unsigned sq_char    : 1;  /* single quote is a C char literal */
    unsigned sq_escape  : 1;  /* backslash escapes inside single-quoted strings */
    unsigned t_heur     : 1;  /* identifiers ending in _t are types */
} HlLang;

static HlLang hl_lang_for(const char *lang)
{
    HlLang L;
    memset(&L, 0, sizeof L);

    if (lang != NULL && strcmp(lang, "c") == 0) {
        L.kw = hl_c_kw; L.ty = hl_c_ty;
        L.line_slash = 1; L.block = 1; L.preproc = 1;
        L.sq_char = 1; L.sq_escape = 1; L.t_heur = 1;
        return L;
    }
    if (lang != NULL && (strcmp(lang, "sh") == 0 || strcmp(lang, "bash") == 0)) {
        L.kw = hl_sh_kw;
        L.line_hash = 1; L.dollar = 1; L.sq_escape = 0; /* sh '' is literal */
        return L;
    }
    if (lang != NULL && (strcmp(lang, "python") == 0 || strcmp(lang, "py") == 0)) {
        L.kw = hl_py_kw;
        L.line_hash = 1; L.triple = 1; L.sq_escape = 1;
        return L;
    }
    /* generic (NULL / "" / unknown): both comment styles, both string quotes,
     * numbers, no keywords. */
    L.line_hash = 1; L.line_slash = 1; L.block = 1; L.sq_escape = 1;
    return L;
}

/* Whole-word membership test for the span code[off..off+n) against a
 * NULL-terminated table. */
static int hl_in_list(const char *code, int off, int n, const char *const *list)
{
    int k;
    if (list == NULL) return 0;
    for (k = 0; list[k] != NULL; k++) {
        if ((int)strlen(list[k]) == n &&
            memcmp(code + off, list[k], (size_t)n) == 0)
            return 1;
    }
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Public entry point.                                                       */
/* ----------------------------------------------------------------------- */

/* Tokenize `code` (length `len`) in language `lang` (e.g. "c","sh","python",
 * NULL/"" = generic). Writes up to `max` tokens to `out` in source order,
 * covering only NON-default spans (gaps are HL_TEXT). Returns the token
 * count (always <= max; the scan stops the moment the buffer is full). */
static int chat_highlight(const char *code, int len, const char *lang,
                          HlTok *out, int max)
{
    HlLang L;
    int n = 0, i = 0, at_bol = 1;

    if (code == NULL || len <= 0 || out == NULL || max <= 0) return 0;
    L = hl_lang_for(lang);

    /* Each iteration handles the byte at `i` and pushes AT MOST one token.
     * We enter the loop only while n < max, so a push can never overflow. */
    while (i < len && n < max) {
        int c = (unsigned char)code[i];
        int start = i, end;
        int bol;
        HlClass cls;

        /* Whitespace and newlines are HL_TEXT gaps — never emitted. */
        if (c == '\n') { at_bol = 1; i++; continue; }
        if (hl_is_space(c)) { i++; continue; }

        bol = at_bol;   /* is this the first non-space token on its line? */
        at_bol = 0;

        /* Comments first, so '/' and '#' cannot be seen as punctuation. */
        if (L.block && c == '/' && i + 1 < len && code[i + 1] == '*') {
            end = hl_scan_block(code, len, i); cls = HL_COMMENT;
        } else if (L.line_slash && c == '/' && i + 1 < len && code[i + 1] == '/') {
            end = hl_scan_line(code, len, i); cls = HL_COMMENT;
        } else if (L.preproc && c == '#' && bol) {
            end = hl_scan_preproc(code, len, i); cls = HL_PREPROC;
        } else if (L.line_hash && c == '#' &&
                   (i == 0 || hl_is_space((unsigned char)code[i - 1]))) {
            end = hl_scan_line(code, len, i); cls = HL_COMMENT;
        }
        /* Strings and character literals. */
        else if (c == '"') {
            if (L.triple && i + 2 < len && code[i + 1] == '"' && code[i + 2] == '"')
                end = hl_scan_triple(code, len, i, '"');
            else
                end = hl_scan_quoted(code, len, i, '"', 1);
            cls = HL_STRING;
        } else if (c == '\'') {
            if (L.sq_char) {
                end = hl_scan_quoted(code, len, i, '\'', 1); cls = HL_CHAR;
            } else if (L.triple && i + 2 < len &&
                       code[i + 1] == '\'' && code[i + 2] == '\'') {
                end = hl_scan_triple(code, len, i, '\''); cls = HL_STRING;
            } else {
                end = hl_scan_quoted(code, len, i, '\'', (int)L.sq_escape);
                cls = HL_STRING;
            }
        }
        /* Shell variable expansion. */
        else if (L.dollar && c == '$') {
            end = hl_scan_dollar(code, len, i);
            cls = (end == start + 1) ? HL_PUNCT : HL_TYPE; /* bare '$' -> punct */
        }
        /* Numbers. */
        else if (hl_is_digit(c) ||
                 (c == '.' && i + 1 < len &&
                  hl_is_digit((unsigned char)code[i + 1]))) {
            end = hl_scan_number(code, len, i); cls = HL_NUMBER;
        }
        /* Identifiers: keyword / type / *_t heuristic, else plain text. */
        else if (hl_is_ident_start(c)) {
            int tl;
            end = hl_scan_ident(code, len, i);
            tl = end - start;
            if (hl_in_list(code, start, tl, L.kw)) cls = HL_KEYWORD;
            else if (hl_in_list(code, start, tl, L.ty)) cls = HL_TYPE;
            else if (L.t_heur && tl > 2 &&
                     code[end - 2] == '_' && code[end - 1] == 't') cls = HL_TYPE;
            else { i = end; continue; }  /* plain identifier => HL_TEXT gap */
        }
        /* Punctuation. */
        else if (hl_is_punct(c)) {
            end = i + 1; cls = HL_PUNCT;
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

#endif /* CHAT_HIGHLIGHT_H */
