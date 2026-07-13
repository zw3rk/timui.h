/*
 * test_result.c — TimuiResult stringification + version (T1.1 close-out).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_error_string){
    /* every enumerator stringifies to its exact human string */
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_OK), "ok") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_INVALID_ARGUMENT), "invalid argument") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_OUT_OF_MEMORY), "out of memory") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_NOT_A_TTY), "not a tty") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_OS), "os error") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_UNSUPPORTED), "unsupported") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_PROTOCOL), "protocol error") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_IO), "i/o error") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_WOULD_BLOCK), "would block") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_EOF), "end of file") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_CLOSED), "closed") == 0);

    /* out-of-range values still return a valid, non-NULL string */
    TIMUI_CHECK(timui_error_string((TimuiResult)9999) != NULL);
}

TIMUI_TEST(test_version){
    TIMUI_CHECK(strcmp(timui_version_string(), "0.2.0") == 0);
}

TIMUI_TEST(test_config_init_defaults){
    TimuiConfig macro_cfg = TIMUI_CONFIG_INIT;
    TimuiConfig fn_cfg;

    memset(&fn_cfg, 0xA5, sizeof fn_cfg);
    timui_config_init(&fn_cfg);

    TIMUI_CHECK(macro_cfg.struct_size == sizeof macro_cfg);
    TIMUI_CHECK(macro_cfg.api_version == TIMUI_API_VERSION);
    TIMUI_CHECK(macro_cfg.input_fd == 0);
    TIMUI_CHECK(macro_cfg.output_fd == 1);
    TIMUI_CHECK(macro_cfg.profile == TIMUI_PROFILE_AUTO);
    TIMUI_CHECK(macro_cfg.theme == TIMUI_THEME_MODERN_DARK);
    TIMUI_CHECK((macro_cfg.flags & TIMUI_FLAG_RESTORE_ON_EXIT) != 0);
    TIMUI_CHECK(fn_cfg.struct_size == macro_cfg.struct_size);
    TIMUI_CHECK(fn_cfg.api_version == macro_cfg.api_version);
    TIMUI_CHECK(fn_cfg.title == macro_cfg.title);
    TIMUI_CHECK(fn_cfg.input_fd == macro_cfg.input_fd);
    TIMUI_CHECK(fn_cfg.output_fd == macro_cfg.output_fd);
    TIMUI_CHECK(fn_cfg.profile == macro_cfg.profile);
    TIMUI_CHECK(fn_cfg.flags == macro_cfg.flags);
    TIMUI_CHECK(fn_cfg.theme == macro_cfg.theme);
    TIMUI_CHECK(fn_cfg.frame_arena_bytes == macro_cfg.frame_arena_bytes);
    TIMUI_CHECK(fn_cfg.persistent_state_bytes == macro_cfg.persistent_state_bytes);
    TIMUI_CHECK(fn_cfg.message_queue_bytes == macro_cfg.message_queue_bytes);
    TIMUI_CHECK(fn_cfg.userdata == macro_cfg.userdata);
}

TIMUI_TEST(test_open_rejects_config_version_mismatch){
    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Timui *ui = NULL;

    cfg.struct_size = sizeof cfg - 1;
    TIMUI_CHECK(timui_open(&cfg, &ui) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(ui == NULL);

    cfg = TIMUI_CONFIG_INIT;
    cfg.api_version = TIMUI_API_VERSION + 1;
    TIMUI_CHECK(timui_open(&cfg, &ui) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(ui == NULL);
}
