/*
 * test_conpty_smoke_tool.c -- portable helper tests for the Win32 ConPTY smoke.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#define main timui_conpty_smoke_main_for_test
#include "../tools/conpty_smoke_win32.c"
#undef main

#define CHECK(expr) do { \
    if(!(expr)){ \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while(0)

typedef struct {
    int writes;
    int short_write;
    char data[128];
    size_t len;
} FakeSmokeWrite;

static int fake_smoke_write(TimuiTransport *t, const void *d, size_t n){
    FakeSmokeWrite *fake = (FakeSmokeWrite *)t->ctx;
    size_t copy;
    fake->writes++;
    copy = n;
    if(copy > sizeof fake->data - fake->len) copy = sizeof fake->data - fake->len;
    if(copy > 0){
        memcpy(fake->data + fake->len, d, copy);
        fake->len += copy;
    }
    if(fake->short_write && n > 0) return (int)n - 1;
    return (int)n;
}

int main(void){
    FakeSmokeWrite fake;
    TimuiTransport tr;
    int last_write = 0;
    const char *primary = smoke_echo_script_for_attempt(0);
    const char *fallback = smoke_echo_script_for_attempt(1);

    CHECK(primary != NULL);
    CHECK(strstr(primary, "echo " TOKEN "\n") != NULL);
    CHECK(strstr(primary, "\r") == NULL);
    CHECK(strstr(primary, "exit") == NULL);
    CHECK(strcmp(smoke_exit_script_for_attempt(0), "exit\n") == 0);

    CHECK(fallback != NULL);
    CHECK(strstr(fallback, "echo " TOKEN "\r\n") != NULL);
    CHECK(strstr(fallback, "exit") == NULL);
    CHECK(strcmp(smoke_exit_script_for_attempt(1), "exit\r\n") == 0);
    CHECK(smoke_echo_script_for_attempt(2) == NULL);
    CHECK(smoke_exit_script_for_attempt(2) == NULL);

    memset(&fake, 0, sizeof fake);
    memset(&tr, 0, sizeof tr);
    tr.write = fake_smoke_write;
    tr.ctx = &fake;
    CHECK(smoke_write_exact(&tr, "abc", 3, &last_write));
    CHECK(last_write == 3);
    CHECK(fake.writes == 1);
    CHECK(fake.len == 3);
    CHECK(memcmp(fake.data, "abc", 3) == 0);

    fake.short_write = 1;
    CHECK(!smoke_write_exact(&tr, "def", 3, &last_write));
    CHECK(last_write == 2);

    CHECK(smoke_contains("xx" TOKEN "yy", sizeof("xx" TOKEN "yy") - 1, TOKEN));
    CHECK(!smoke_contains("xxTIMUI_CONPTY_SMO", sizeof("xxTIMUI_CONPTY_SMO") - 1, TOKEN));
    return 0;
}
