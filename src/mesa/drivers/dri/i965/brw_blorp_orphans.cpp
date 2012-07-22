/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * \file
 *
 * This file contains functions which  whose definitions conceptually belong
 * in other C files but need to interact closely with blorp.
 */

#include "intel_mipmap_tree.h"

#include "brw_blorp.h"

extern "C" {

/**
 * \brief Downsample from mt to mt->singlesample_mt.
 *
 * If the miptree needs no downsample, then skip.
 */
void
intel_miptree_downsample(struct intel_context *intel,
                         struct intel_mipmap_tree *mt)
{
   if (!mt->need_downsample)
      return;

   int src_x0 = 0;
   int src_y0 = 0;
   int dst_x0 = 0;
   int dst_y0 = 0;

   brw_blorp_blit_params params(brw_context(&intel->ctx),
                                mt, mt->singlesample_mt,
                                src_x0, src_y0,
                                dst_x0, dst_y0,
                                mt->singlesample_mt->width0,
                                mt->singlesample_mt->height0,
                                false, false);
   brw_blorp_exec(intel, &params);

   mt->need_downsample = false;
}

} /* end extern "C" */
