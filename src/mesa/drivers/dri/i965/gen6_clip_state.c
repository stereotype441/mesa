/*
 * Copyright © 2009 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "brw_util.h"
#include "intel_batchbuffer.h"
#include "main/fbobject.h"

static void
upload_clip_state(struct brw_context *brw)
{
   struct intel_context *intel = &brw->intel;
   struct gl_context *ctx = &intel->ctx;
   uint32_t dw1 = brw->meta_in_progress ? 0 : GEN6_CLIP_STATISTICS_ENABLE;
   uint32_t dw2 = 0;

   /* BRW_NEW_BLORP */
   if (brw->blorp.params) {
      /* Disable the clipper.
       *
       * The BLORP op emits a rectangle primitive, which requires clipping to
       * be disabled. From page 10 of the Sandy Bridge PRM Volume 2 Part 1
       * Section 1.3 "3D Primitives Overview":
       *    RECTLIST:
       *    Either the CLIP unit should be DISABLED, or the CLIP unit's Clip
       *    Mode should be set to a value other than CLIPMODE_NORMAL.
       *
       * Also disable perspective divide. This doesn't change the clipper's
       * output, but does spare a few electrons.
       */
      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_CLIP << 16 | (4 - 2));
      OUT_BATCH(0);
      OUT_BATCH(GEN6_CLIP_PERSPECTIVE_DIVIDE_DISABLE);
      OUT_BATCH(0);
      ADVANCE_BATCH();
      return;
   }

   /* _NEW_BUFFERS */
   struct gl_framebuffer *fb = ctx->DrawBuffer;

   /* CACHE_NEW_WM_PROG */
   if (brw->wm.prog_data->barycentric_interp_modes &
       BRW_WM_NONPERSPECTIVE_BARYCENTRIC_BITS) {
      dw2 |= GEN6_CLIP_NON_PERSPECTIVE_BARYCENTRIC_ENABLE;
   }

   if (!ctx->Transform.DepthClamp)
      dw2 |= GEN6_CLIP_Z_TEST;

   /* _NEW_LIGHT */
   if (ctx->Light.ProvokingVertex == GL_FIRST_VERTEX_CONVENTION) {
      dw2 |=
	 (0 << GEN6_CLIP_TRI_PROVOKE_SHIFT) |
	 (1 << GEN6_CLIP_TRIFAN_PROVOKE_SHIFT) |
	 (0 << GEN6_CLIP_LINE_PROVOKE_SHIFT);
   } else {
      dw2 |=
	 (2 << GEN6_CLIP_TRI_PROVOKE_SHIFT) |
	 (2 << GEN6_CLIP_TRIFAN_PROVOKE_SHIFT) |
	 (1 << GEN6_CLIP_LINE_PROVOKE_SHIFT);
   }

   /* _NEW_TRANSFORM */
   dw2 |= (ctx->Transform.ClipPlanesEnabled <<
           GEN6_USER_CLIP_CLIP_DISTANCES_SHIFT);

   if (ctx->Viewport.X == 0 &&
       ctx->Viewport.Y == 0 &&
       ctx->Viewport.Width == fb->Width &&
       ctx->Viewport.Height == fb->Height) {
      dw2 |= GEN6_CLIP_GB_TEST;
   }

   /* BRW_NEW_RASTERIZER_DISCARD */
   if (ctx->RasterDiscard) {
      dw2 |= GEN6_CLIP_MODE_REJECT_ALL;
      perf_debug("Rasterizer discard is currently implemented via the clipper; "
                 "having the GS not write primitives would likely be faster.");
   }

   BEGIN_BATCH(4);
   OUT_BATCH(_3DSTATE_CLIP << 16 | (4 - 2));
   OUT_BATCH(dw1);
   OUT_BATCH(GEN6_CLIP_ENABLE |
	     GEN6_CLIP_API_OGL |
	     GEN6_CLIP_MODE_NORMAL |
	     GEN6_CLIP_XY_TEST |
	     dw2);
   OUT_BATCH(U_FIXED(0.125, 3) << GEN6_CLIP_MIN_POINT_WIDTH_SHIFT |
             U_FIXED(255.875, 3) << GEN6_CLIP_MAX_POINT_WIDTH_SHIFT |
             GEN6_CLIP_FORCE_ZERO_RTAINDEX);
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen6_clip_state = {
   .dirty = {
      .mesa  = _NEW_TRANSFORM | _NEW_LIGHT | _NEW_BUFFERS,
      .brw   = BRW_NEW_CONTEXT |
               BRW_NEW_META_IN_PROGRESS |
               BRW_NEW_RASTERIZER_DISCARD |
               BRW_NEW_BLORP,
      .cache = CACHE_NEW_WM_PROG
   },
   .emit = upload_clip_state,
};
