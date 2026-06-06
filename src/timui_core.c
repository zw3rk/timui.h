/*
 * timui_core.c — split-build translation unit.
 *
 * The canonical source of truth is include/timui.h (single-header, stb-style).
 * This TU turns that header into one compiled object for the split build and
 * for the unit tests; it is the single place that defines TIMUI_IMPLEMENTATION.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"
