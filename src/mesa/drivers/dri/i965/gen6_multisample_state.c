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

#include "intel_batchbuffer.h"

#include "brw_context.h"
#include "brw_defines.h"

static void upload_multisample_state(struct brw_context *brw)
{
   struct intel_context *intel = &brw->intel;
   struct gl_context *ctx = &intel->ctx;

   assert(ctx->DrawBuffer->_ColorDrawBuffers[0]); // TODO
   /* _NEW_BUFFERS */
   bool multisampled = ctx->DrawBuffer->_ColorDrawBuffers[0]->NumSamples > 1; // TODO: what does 1 mean?

   /* 3DSTATE_MULTISAMPLE is nonpipelined. */
   intel_emit_post_sync_nonzero_flush(intel);

   /* 3DSTATE_MULTISAMPLE */
   {
      int len = intel->gen >= 7 ? 4 : 3;
      BEGIN_BATCH(len);
      OUT_BATCH(_3DSTATE_MULTISAMPLE << 16 | (len - 2));
      OUT_BATCH(MS_PIXEL_LOCATION_CENTER |
		(multisampled ? MS_NUMSAMPLES_4 : MS_NUMSAMPLES_1));
      OUT_BATCH(multisampled ? 0xae2ae662 : 0); /* positions for 4/8-sample */
      if (intel->gen >= 7)
	 OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_SAMPLE_MASK */
   {
      BEGIN_BATCH(2);
      OUT_BATCH(_3DSTATE_SAMPLE_MASK << 16 | (2 - 2));
      OUT_BATCH(multisampled ? 15 : 1);
      ADVANCE_BATCH();
   }
}

const struct brw_tracked_state gen6_multisample_state = {
   .dirty = {
      .mesa = _NEW_BUFFERS,
      .brw = BRW_NEW_CONTEXT,
      .cache = 0
   },
   .emit = upload_multisample_state
};
