/* irc_proto.h — pure, allocation-free RFC 1459 / 2812 message parser.
 *
 * A single IRC line has the shape
 *
 *     [ ':' prefix SPACE ] command { SPACE middle } [ SPACE ':' trailing ] CRLF
 *
 * where `prefix` is `servername` or `nick [ '!' user ] [ '@' host ]`, `command`
 * is a verb or a 3-digit numeric, up to 15 `middle` params contain no spaces and
 * do not start with ':', and the single optional `trailing` param (introduced by
 * a leading ':') may contain spaces. This header parses ONE such line into a
 * caller-provided IrcMessage (fixed buffers, no malloc): the line is copied into
 * an in-struct scratch buffer, tokenized IN PLACE, and params[] point into it.
 *
 * Header-only + `static` so it can be included by the client and the unit test
 * alike. It has no timui / network dependency — just <string.h> / <stddef.h>.
 *
 * Ref: RFC 1459 §2.3.1, RFC 2812 §2.3.1 (message format + the 15-param limit).
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef IRC_PROTO_H
#define IRC_PROTO_H

#include <stddef.h>
#include <string.h>

/* RFC caps a message at 512 bytes INCLUDING CRLF; 15 is the middle-param limit
 * (the 16th token, if any, is folded into the trailing by the spec — we simply
 * clamp, which is enough for a robust client). */
#define IRC_LINE_MAX   512
#define IRC_MAX_PARAMS 15

/* Command classes we act on. Anything else (that is not a 3-digit numeric) is
 * IRC_CMD_UNKNOWN; every 3-digit numeric is IRC_CMD_NUMERIC (inspect .numeric). */
typedef enum {
    IRC_CMD_UNKNOWN = 0,
    IRC_CMD_PING, IRC_CMD_PONG,
    IRC_CMD_JOIN, IRC_CMD_PART, IRC_CMD_QUIT,
    IRC_CMD_PRIVMSG, IRC_CMD_NOTICE,
    IRC_CMD_NICK, IRC_CMD_TOPIC, IRC_CMD_MODE, IRC_CMD_NAMES,
    IRC_CMD_ERROR,
    IRC_CMD_NUMERIC
} IrcCmd;

/* The numeric replies the client handles (RFC 2812 §5). */
enum {
    IRC_RPL_WELCOME       = 1,
    IRC_RPL_TOPIC         = 332,
    IRC_RPL_NAMREPLY      = 353,
    IRC_RPL_ENDOFNAMES    = 366,
    IRC_ERR_NOSUCHNICK    = 401,
    IRC_ERR_NICKNAMEINUSE = 433
};

/* A parsed message. All storage is inline; `params[i]` point into `store` (which
 * holds the line, CRLF-stripped and NUL-split at token boundaries). Copy the
 * whole struct to keep a message past the next parse. */
typedef struct {
    char        store[IRC_LINE_MAX];   /* working copy, tokenized in place        */
    char        prefix[IRC_LINE_MAX];  /* raw prefix (servername/nick!user@host)  */
    char        nick[64];              /* nick (or servername) portion, or ""     */
    char        user[64];              /* user portion, or ""                     */
    char        host[128];             /* host portion, or ""                     */
    char        command[32];           /* verb ("PRIVMSG") or numeric text ("353")*/
    int         numeric;               /* parsed 3-digit numeric, else 0          */
    int         nparams;               /* count of params (middles + trailing)    */
    const char *params[IRC_MAX_PARAMS];/* pointers into `store`                   */
} IrcMessage;

/* Bounded copy: at most cap-1 bytes of [src, src+n), always NUL-terminated. */
static void irc_bcpy_(char *dst, size_t cap, const char *src, size_t n){
    if(cap == 0) return;
    if(n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Split a raw prefix `nick[!user][@host]` (or a bare servername) into fields. */
static void irc_split_prefix_(const char *pfx, IrcMessage *m){
    const char *bang = strchr(pfx, '!');
    const char *at   = strchr(pfx, '@');
    size_t nlen;
    irc_bcpy_(m->prefix, sizeof m->prefix, pfx, strlen(pfx));
    /* nick runs up to the first of '!'/'@', else the whole string (a servername
     * has neither and lands wholesale in `nick`). */
    if(bang)     nlen = (size_t)(bang - pfx);
    else if(at)  nlen = (size_t)(at - pfx);
    else         nlen = strlen(pfx);
    irc_bcpy_(m->nick, sizeof m->nick, pfx, nlen);
    if(bang){                                        /* user is between '!' and '@'/end */
        const char *ue = at ? at : pfx + strlen(pfx);
        if(ue > bang + 1) irc_bcpy_(m->user, sizeof m->user, bang + 1, (size_t)(ue - (bang + 1)));
    }
    if(at) irc_bcpy_(m->host, sizeof m->host, at + 1, strlen(at + 1));
}

/* True iff `s` is exactly three ASCII digits (an IRC numeric reply code). */
static int irc_is_numeric_(const char *s){
    return s[0] >= '0' && s[0] <= '9' &&
           s[1] >= '0' && s[1] <= '9' &&
           s[2] >= '0' && s[2] <= '9' && s[3] == '\0';
}

/* Parse ONE line into `m`. `len` bounds the input (a NUL also ends it); a CR or
 * LF terminates the line, so CR-LF / bare-LF / no-ending all behave the same.
 * Returns 1 on a message carrying a command, 0 on an empty, whitespace-only, or
 * prefix-only line. Allocation-free and side-effect-free apart from writing *m. */
static int irc_parse(const char *line, size_t len, IrcMessage *m){
    char *p;
    size_t n = 0, i;

    memset(m, 0, sizeof *m);

    /* Bounded copy into the scratch, stopping at the first CR/LF or the cap. */
    for(i = 0; i < len && line[i] && n < IRC_LINE_MAX - 1; i++){
        if(line[i] == '\r' || line[i] == '\n') break;
        m->store[n++] = line[i];
    }
    m->store[n] = '\0';

    p = m->store;
    while(*p == ' ') p++;                 /* leading space run */
    if(*p == '\0') return 0;              /* empty / whitespace-only */

    /* Optional prefix. */
    if(*p == ':'){
        char *pfx = ++p;
        while(*p && *p != ' ') p++;
        if(*p == ' ') *p++ = '\0';        /* terminate the prefix token */
        irc_split_prefix_(pfx, m);
        while(*p == ' ') p++;             /* space run after the prefix */
    }
    if(*p == '\0') return 0;              /* a prefix with no command is not a message */

    /* Command verb / numeric. */
    {
        char *cmd = p;
        while(*p && *p != ' ') p++;
        if(*p == ' ') *p++ = '\0';
        irc_bcpy_(m->command, sizeof m->command, cmd, strlen(cmd));
        if(irc_is_numeric_(m->command))
            m->numeric = (m->command[0]-'0')*100 + (m->command[1]-'0')*10 + (m->command[2]-'0');
    }

    /* Params: middles (space-separated, no leading ':') then one trailing
     * (a param-leading ':' consumes the rest of the line, spaces included). */
    while(*p){
        while(*p == ' ') p++;            /* collapse space runs between params */
        if(*p == '\0') break;
        if(*p == ':'){                    /* trailing */
            p++;
            if(m->nparams < IRC_MAX_PARAMS) m->params[m->nparams++] = p;
            break;
        }
        {
            char *param = p;
            while(*p && *p != ' ') p++;
            if(*p == ' ') *p++ = '\0';
            if(m->nparams < IRC_MAX_PARAMS) m->params[m->nparams++] = param;
            else break;                   /* overflow: clamp, never overrun */
        }
    }
    return 1;
}

/* ASCII case-insensitive equality (IRC verbs are case-insensitive; servers use
 * uppercase, clients may not). */
static int irc_ieq_(const char *a, const char *b){
    for(; *a && *b; a++, b++){
        int ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        int cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
        if(ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/* Classify a parsed message. Numerics collapse to IRC_CMD_NUMERIC (read
 * .numeric for the code); unknown verbs map to IRC_CMD_UNKNOWN. */
static IrcCmd irc_classify(const IrcMessage *m){
    const char *c = m->command;
    if(m->numeric > 0)          return IRC_CMD_NUMERIC;
    if(irc_ieq_(c, "PING"))     return IRC_CMD_PING;
    if(irc_ieq_(c, "PONG"))     return IRC_CMD_PONG;
    if(irc_ieq_(c, "JOIN"))     return IRC_CMD_JOIN;
    if(irc_ieq_(c, "PART"))     return IRC_CMD_PART;
    if(irc_ieq_(c, "QUIT"))     return IRC_CMD_QUIT;
    if(irc_ieq_(c, "PRIVMSG"))  return IRC_CMD_PRIVMSG;
    if(irc_ieq_(c, "NOTICE"))   return IRC_CMD_NOTICE;
    if(irc_ieq_(c, "NICK"))     return IRC_CMD_NICK;
    if(irc_ieq_(c, "TOPIC"))    return IRC_CMD_TOPIC;
    if(irc_ieq_(c, "MODE"))     return IRC_CMD_MODE;
    if(irc_ieq_(c, "NAMES"))    return IRC_CMD_NAMES;
    if(irc_ieq_(c, "ERROR"))    return IRC_CMD_ERROR;
    return IRC_CMD_UNKNOWN;
}

/* A channel name starts with a channel sigil ('#', '&', '+', '!' per RFC 2811). */
static int irc_is_channel(const char *target){
    if(!target || !*target) return 0;
    return target[0] == '#' || target[0] == '&' || target[0] == '+' || target[0] == '!';
}

/* Safe param accessor: params[i], or "" when i is out of range. */
static const char *irc_param(const IrcMessage *m, int i){
    if(i < 0 || i >= m->nparams) return "";
    return m->params[i];
}

#endif /* IRC_PROTO_H */
