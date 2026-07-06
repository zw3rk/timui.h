/* test_irc.c — standalone unit tests for examples/irc_proto.h: the PURE,
 * allocation-free RFC 1459/2812 message parser + command classifier.
 *
 * No timui library, no network — just the header-only parser driven on
 * HAND-COMPUTED vectors (positive per message type + adversarial: malformed,
 * truncated, missing prefix, an embedded colon, runs of spaces, CR-LF / bare-LF
 * / no line ending, empty / prefix-only lines, over-long + param overflow).
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#include "irc_proto.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if(!(cond)){ \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while(0)

/* Parse a NUL-terminated C string (the common case) into `m`. */
static int parse(IrcMessage *m, const char *s){ return irc_parse(s, strlen(s), m); }

int main(void){
    IrcMessage m;

    /* ===================== positive: one per message type ================= */

    /* PING with a trailing token, no prefix. */
    CHECK(parse(&m, "PING :12345") == 1);
    CHECK(m.prefix[0] == '\0');
    CHECK(strcmp(m.command, "PING") == 0);
    CHECK(m.numeric == 0);
    CHECK(m.nparams == 1);
    CHECK(strcmp(m.params[0], "12345") == 0);
    CHECK(irc_classify(&m) == IRC_CMD_PING);

    /* PRIVMSG with a full nick!user@host prefix + a channel + trailing body. */
    CHECK(parse(&m, ":nick!user@host PRIVMSG #chan :Hello world") == 1);
    CHECK(strcmp(m.prefix, "nick!user@host") == 0);
    CHECK(strcmp(m.nick, "nick") == 0);
    CHECK(strcmp(m.user, "user") == 0);
    CHECK(strcmp(m.host, "host") == 0);
    CHECK(strcmp(m.command, "PRIVMSG") == 0);
    CHECK(m.nparams == 2);
    CHECK(strcmp(m.params[0], "#chan") == 0);
    CHECK(strcmp(m.params[1], "Hello world") == 0);   /* trailing keeps its space */
    CHECK(irc_classify(&m) == IRC_CMD_PRIVMSG);
    CHECK(irc_is_channel(m.params[0]) == 1);

    /* RPL_WELCOME 001 — a server prefix (no '!'/'@' => nick holds the whole). */
    CHECK(parse(&m, ":irc.example.net 001 mynick :Welcome to the network") == 1);
    CHECK(strcmp(m.prefix, "irc.example.net") == 0);
    CHECK(strcmp(m.nick, "irc.example.net") == 0);
    CHECK(m.user[0] == '\0' && m.host[0] == '\0');
    CHECK(strcmp(m.command, "001") == 0);
    CHECK(m.numeric == IRC_RPL_WELCOME);
    CHECK(m.nparams == 2);
    CHECK(strcmp(m.params[0], "mynick") == 0);
    CHECK(strcmp(m.params[1], "Welcome to the network") == 0);
    CHECK(irc_classify(&m) == IRC_CMD_NUMERIC);

    /* RPL_NAMREPLY 353: me = #timui :me alice bob  (4 params; '=' is middle). */
    CHECK(parse(&m, ":server 353 me = #timui :me alice bob") == 1);
    CHECK(m.numeric == IRC_RPL_NAMREPLY);
    CHECK(m.nparams == 4);
    CHECK(strcmp(m.params[0], "me") == 0);
    CHECK(strcmp(m.params[1], "=") == 0);
    CHECK(strcmp(m.params[2], "#timui") == 0);
    CHECK(strcmp(m.params[3], "me alice bob") == 0);

    /* RPL_ENDOFNAMES 366. */
    CHECK(parse(&m, ":server 366 me #timui :End of /NAMES list.") == 1);
    CHECK(m.numeric == IRC_RPL_ENDOFNAMES);
    CHECK(m.nparams == 3);
    CHECK(strcmp(m.params[1], "#timui") == 0);

    /* RPL_TOPIC 332. */
    CHECK(parse(&m, ":server 332 me #timui :Welcome to timui") == 1);
    CHECK(m.numeric == IRC_RPL_TOPIC);
    CHECK(strcmp(m.params[1], "#timui") == 0);
    CHECK(strcmp(m.params[2], "Welcome to timui") == 0);

    /* JOIN — channel as a plain middle param (no trailing). */
    CHECK(parse(&m, ":me!u@h JOIN #timui") == 1);
    CHECK(strcmp(m.nick, "me") == 0);
    CHECK(irc_classify(&m) == IRC_CMD_JOIN);
    CHECK(m.nparams == 1);
    CHECK(strcmp(m.params[0], "#timui") == 0);

    /* JOIN — channel delivered as a trailing param (some servers do this). */
    CHECK(parse(&m, ":me!u@h JOIN :#timui") == 1);
    CHECK(irc_classify(&m) == IRC_CMD_JOIN);
    CHECK(strcmp(m.params[0], "#timui") == 0);

    /* PART with a reason. */
    CHECK(parse(&m, ":bob!u@h PART #timui :bye all") == 1);
    CHECK(irc_classify(&m) == IRC_CMD_PART);
    CHECK(strcmp(m.nick, "bob") == 0);
    CHECK(strcmp(m.params[0], "#timui") == 0);
    CHECK(strcmp(m.params[1], "bye all") == 0);

    /* QUIT with a trailing reason (no channel param). */
    CHECK(parse(&m, ":carol!u@h QUIT :Ping timeout") == 1);
    CHECK(irc_classify(&m) == IRC_CMD_QUIT);
    CHECK(strcmp(m.nick, "carol") == 0);
    CHECK(m.nparams == 1);
    CHECK(strcmp(m.params[0], "Ping timeout") == 0);

    /* NICK change. */
    CHECK(parse(&m, ":old!u@h NICK newnick") == 1);
    CHECK(irc_classify(&m) == IRC_CMD_NICK);
    CHECK(strcmp(m.nick, "old") == 0);
    CHECK(strcmp(m.params[0], "newnick") == 0);

    /* TOPIC command (as opposed to the 332 numeric). */
    CHECK(parse(&m, ":op!u@h TOPIC #timui :new topic here") == 1);
    CHECK(irc_classify(&m) == IRC_CMD_TOPIC);
    CHECK(strcmp(m.params[0], "#timui") == 0);
    CHECK(strcmp(m.params[1], "new topic here") == 0);

    /* NOTICE. */
    CHECK(parse(&m, ":srv NOTICE * :*** Looking up your hostname") == 1);
    CHECK(irc_classify(&m) == IRC_CMD_NOTICE);
    CHECK(strcmp(m.params[0], "*") == 0);
    CHECK(strcmp(m.params[1], "*** Looking up your hostname") == 0);

    /* ERR_NICKNAMEINUSE 433. */
    CHECK(parse(&m, ":srv 433 * taken :Nickname is already in use") == 1);
    CHECK(m.numeric == IRC_ERR_NICKNAMEINUSE);
    CHECK(irc_classify(&m) == IRC_CMD_NUMERIC);

    /* nick-only prefix (no user/host), e.g. services / a bare NICK from a peer. */
    CHECK(parse(&m, ":alice PRIVMSG #c :hi") == 1);
    CHECK(strcmp(m.nick, "alice") == 0);
    CHECK(m.user[0] == '\0' && m.host[0] == '\0');

    /* nick@host with no user part. */
    CHECK(parse(&m, ":alice@host PRIVMSG #c :hi") == 1);
    CHECK(strcmp(m.nick, "alice") == 0);
    CHECK(m.user[0] == '\0');
    CHECK(strcmp(m.host, "host") == 0);

    /* ===================== adversarial / malformed ======================== */

    /* empty line + whitespace-only line => not a message. */
    CHECK(parse(&m, "") == 0);
    CHECK(parse(&m, "   ") == 0);

    /* a line with ONLY a prefix and no command => rejected. */
    CHECK(parse(&m, ":irc.server") == 0);
    CHECK(parse(&m, ":irc.server   ") == 0);

    /* missing prefix: bare command still parses (prefix empty). */
    CHECK(parse(&m, "PRIVMSG #c :hi") == 1);
    CHECK(m.prefix[0] == '\0');
    CHECK(strcmp(m.command, "PRIVMSG") == 0);

    /* a colon INSIDE a middle param is literal (only a param-leading ':' opens
     * the trailing). */
    CHECK(parse(&m, ":s PRIVMSG a:b :c d") == 1);
    CHECK(m.nparams == 2);
    CHECK(strcmp(m.params[0], "a:b") == 0);
    CHECK(strcmp(m.params[1], "c d") == 0);

    /* runs of spaces between EVERY token collapse; the trailing preserves its
     * OWN internal spaces. */
    CHECK(parse(&m, ":n!u@h   PRIVMSG   #c   :multi   space   body") == 1);
    CHECK(strcmp(m.command, "PRIVMSG") == 0);
    CHECK(m.nparams == 2);
    CHECK(strcmp(m.params[0], "#c") == 0);
    CHECK(strcmp(m.params[1], "multi   space   body") == 0);
    /* leading run of spaces before the command (no prefix). */
    CHECK(parse(&m, "PING     :tok") == 1);
    CHECK(strcmp(m.params[0], "tok") == 0);

    /* an EXPLICIT but empty trailing param is still one param. */
    CHECK(parse(&m, ":s!u@h PART #c :") == 1);
    CHECK(m.nparams == 2);
    CHECK(strcmp(m.params[0], "#c") == 0);
    CHECK(m.params[1][0] == '\0');

    /* CR-LF, bare-LF, and no line ending all yield the same fields. */
    CHECK(parse(&m, "PING :x\r\n") == 1); CHECK(strcmp(m.params[0], "x") == 0);
    CHECK(parse(&m, "PING :y\n")   == 1); CHECK(strcmp(m.params[0], "y") == 0);
    CHECK(parse(&m, "PING :z")     == 1); CHECK(strcmp(m.params[0], "z") == 0);

    /* a numeric-looking-but-not (3 chars, not all digits) is NOT a numeric. */
    CHECK(parse(&m, ":srv 4XY me :x") == 1);
    CHECK(m.numeric == 0);
    CHECK(irc_classify(&m) == IRC_CMD_UNKNOWN);
    /* 2- and 4-digit tokens are not the 3-digit numeric form either. */
    CHECK(parse(&m, ":srv 42 me :x") == 1);   CHECK(m.numeric == 0);
    CHECK(parse(&m, ":srv 0012 me :x") == 1); CHECK(m.numeric == 0);

    /* over-long line: bounded copy, no overrun; the command still parses and a
     * trailing is still produced (truncated but present + NUL-terminated). */
    {
        char big[900];
        size_t i;
        memcpy(big, ":s PRIVMSG #c :", 15);
        for(i = 15; i < sizeof big - 1; i++) big[i] = 'a';
        big[sizeof big - 1] = '\0';
        CHECK(parse(&m, big) == 1);
        CHECK(strcmp(m.command, "PRIVMSG") == 0);
        CHECK(m.nparams == 2);
        CHECK(strcmp(m.params[0], "#c") == 0);
        CHECK(m.params[1][0] == 'a');
        CHECK(strlen(m.params[1]) < IRC_LINE_MAX);       /* clamped, terminated */
    }

    /* param overflow: more middle params than IRC_MAX_PARAMS clamp (no overrun). */
    CHECK(parse(&m, "CMD p1 p2 p3 p4 p5 p6 p7 p8 p9 p10 p11 p12 p13 p14 p15 p16 p17 p18 p19 p20") == 1);
    CHECK(m.nparams == IRC_MAX_PARAMS);
    CHECK(strcmp(m.params[0], "p1") == 0);
    CHECK(strcmp(m.params[IRC_MAX_PARAMS - 1], "p15") == 0);

    /* command with no params at all. */
    CHECK(parse(&m, "PING") == 1);
    CHECK(m.nparams == 0);
    CHECK(irc_classify(&m) == IRC_CMD_PING);

    /* irc_param() accessor clamps out-of-range to "". */
    CHECK(parse(&m, "PING :tok") == 1);
    CHECK(strcmp(irc_param(&m, 0), "tok") == 0);
    CHECK(irc_param(&m, 1)[0] == '\0');
    CHECK(irc_param(&m, -1)[0] == '\0');

    /* channel-name predicate covers all channel sigils + rejects a nick. */
    CHECK(irc_is_channel("#chan") == 1);
    CHECK(irc_is_channel("&local") == 1);
    CHECK(irc_is_channel("alice") == 0);
    CHECK(irc_is_channel("") == 0);

    if(failures){ printf("test_irc: %d FAILED\n", failures); return 1; }
    printf("test_irc: all passed\n");
    return 0;
}
