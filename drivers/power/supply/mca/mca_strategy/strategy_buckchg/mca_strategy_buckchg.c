// SPDX-License-Identifier: GPL-2.0
/*
 * Keep the bring-up implementation in a separate translation unit while
 * preserving the public strategy callback typedef.  The helper macro accepts
 * the function-style cast used by the reconstruction source and expands it to
 * a normal C pointer cast.
 */
#define mca_strategy_func(x) ((mca_strategy_func)(x))
#include "mca_strategy_buckchg_basic.c"
