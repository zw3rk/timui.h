/*
 * timui.c -- dev build entry point.
 *
 * Compile this one TU to build the whole library: it defines
 * TIMUI_IMPLEMENTATION and includes the header, which pulls in every
 * src/timui_*.c section. (Users of the single-header release just define
 * TIMUI_IMPLEMENTATION before #include "timui.h" in their own TU.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIMUI_IMPLEMENTATION
#include "../include/timui.h"
