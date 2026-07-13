/*
 * fuzz_main.c -- deterministic fuzz/adversarial release gate.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "test.h"

int timui_test_failures = 0;

int main(void){
    test_fuzz_parser_random_stream();
    test_fuzz_parser_adversarial();
    test_kitty_graphics_rejects_invalid_png();
    test_image_from_rgba_rejects_invalid_inputs();
    test_image_from_png_rgba_rejects_invalid_inputs();
    test_image_png_byte_limit_rejects_oversized_inputs();

    if(timui_test_failures){
        printf("%d fuzz checks failed\n", timui_test_failures);
        return 1;
    }
    printf("fuzz regression corpus passed\n");
    return 0;
}
