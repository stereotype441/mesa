/*
 * Copyright © 2011 Intel Corporation
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

/** \file gen6_sol.c
 *
 * Code to initialize the binding table entries used by transform feedback.
 */

#include "brw_context.h"
#include "intel_buffer_objects.h"
#include "brw_defines.h"

static void
brw_update_sol_surfaces(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->intel.ctx;
   struct intel_context *intel = &brw->intel;
   /* _NEW_TRANSFORM_FEEDBACK */
   struct gl_transform_feedback_object *xfb_obj =
      ctx->TransformFeedback.CurrentObject;
   /* BRW_NEW_VERTEX_PROGRAM */
   const struct gl_shader_program *shaderprog =
      ctx->Shader.CurrentVertexProgram;
   const struct gl_transform_feedback_info *linked_xfb_info =
      &shaderprog->LinkedTransformFeedback;
   int i;
   for (i = 0; i < BRW_MAX_SOL_BINDINGS; ++i) {
      const int surf_index = SURF_INDEX_SOL_BINDING(i);
      if (xfb_obj->Active && i < linked_xfb_info->NumOutputs) {
         unsigned buffer = linked_xfb_info->Outputs[i].OutputBuffer;
         struct gl_buffer_object *buffer_obj = xfb_obj->Buffers[buffer];
         drm_intel_bo *bo = intel_buffer_object(buffer_obj)->buffer;
         size_t size_dwords = buffer_obj->Size / 4;
         unsigned num_components = linked_xfb_info->Outputs[i].NumComponents;
         unsigned stride = linked_xfb_info->BufferStride[buffer];
         unsigned buffer_offset =
            xfb_obj->Offset[buffer] / 4 +
            linked_xfb_info->Outputs[i].DstOffset;
         uint32_t buffer_size_minus_1;
         /* FIXME: can we rely on core Mesa to ensure that the buffer isn't
          * too big to map using a single binding table entry?
          */
         assert ((size_dwords - buffer_offset) / stride
                 <= BRW_MAX_NUM_BUFFER_ENTRIES);
         if (size_dwords > buffer_offset + num_components) {
            /* There is room for at least 1 transform feedback output in the
             * buffer.  Compute the number of additional transform feedback
             * outputs the buffer has room for.
             */
            buffer_size_minus_1 =
               (size_dwords - buffer_offset - num_components) / stride;
         } else {
            /* There isn't even room for a single transform feedback output in
             * the buffer.  We can't configure the binding table entry to
             * prevent output entirely; we'll have to rely on the geometry
             * shader to detect overflow.  But to minimize the damage in case
             * of a bug, set up the binding table entry to just allow a single
             * output.
             */
            buffer_size_minus_1 = 0;
         }
         intel->vtbl.update_sol_surface(
            brw, bo, &brw->bind.surf_offset[surf_index],
            num_components, stride, buffer_offset,
            buffer_size_minus_1);
      } else {
         brw->bind.surf_offset[surf_index] = 0;
      }
   }
}

const struct brw_tracked_state gen6_sol_surface = {
   .dirty = {
      .mesa = _NEW_TRANSFORM_FEEDBACK,
      .brw = (BRW_NEW_BATCH |
              BRW_NEW_VERTEX_PROGRAM),
      .cache = 0
   },
   .emit = brw_update_sol_surfaces,
};
