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

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "brw_util.h"
#include "program/prog_parameter.h"
#include "program/prog_statevars.h"
#include "intel_batchbuffer.h"


void
gen7_upload_vec4_state(struct brw_context *brw,
                       const struct gen7_vec4_upload_params *upload_params,
                       const struct brw_stage_state *stage_state,
                       bool active, bool alt_floating_point_mode,
                       const struct brw_vec4_prog_data *prog_data,
                       const unsigned *stage_specific_cmd_data)
{
   /* BRW_NEW_*_BINDING_TABLE */
   BEGIN_BATCH(2);
   OUT_BATCH(upload_params->binding_table_pointers_cmd << 16 | (2 - 2));
   OUT_BATCH(stage_state->bind_bo_offset);
   ADVANCE_BATCH();

   /* CACHE_NEW_SAMPLER */
   BEGIN_BATCH(2);
   OUT_BATCH(upload_params->sampler_state_pointers_cmd << 16 | (2 - 2));
   OUT_BATCH(stage_state->sampler_offset);
   ADVANCE_BATCH();

   if (!active || stage_state->push_const_size == 0) {
      /* Disable the push constant buffers. */
      BEGIN_BATCH(7);
      OUT_BATCH(upload_params->constant_cmd << 16 | (7 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(7);
      OUT_BATCH(upload_params->constant_cmd << 16 | (7 - 2));
      OUT_BATCH(stage_state->push_const_size);
      OUT_BATCH(0);
      /* Pointer to the stage's constant buffer.  Covered by the set of
       * state flags from gen6_prepare_wm_contants
       */
      OUT_BATCH(stage_state->push_const_offset | GEN7_MOCS_L3);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   BEGIN_BATCH(upload_params->state_cmd_size);
   OUT_BATCH(upload_params->state_cmd << 16 |
             (upload_params->state_cmd_size - 2));
   if (active) {
      OUT_BATCH(stage_state->prog_offset);
      OUT_BATCH((alt_floating_point_mode ? GEN6_FLOATING_POINT_MODE_ALT
                 : GEN6_FLOATING_POINT_MODE_IEEE_754) |
                ((ALIGN(stage_state->sampler_count, 4)/4) <<
                 GEN6_SAMPLER_COUNT_SHIFT));

      if (prog_data->total_scratch) {
         OUT_RELOC(stage_state->scratch_bo,
                   I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                   ffs(prog_data->total_scratch) - 11);
      } else {
         OUT_BATCH(0);
      }
   } else {
      OUT_BATCH(0); /* prog_bo */
      OUT_BATCH((0 << GEN6_SAMPLER_COUNT_SHIFT) |
                (0 << GEN6_BINDING_TABLE_ENTRY_COUNT_SHIFT));
      OUT_BATCH(0); /* scratch space base offset */
   }
   for (int i = 0; i < upload_params->state_cmd_size - 4; ++i)
      OUT_BATCH(stage_specific_cmd_data[i]);
   ADVANCE_BATCH();
}


static const struct gen7_vec4_upload_params vs_upload_params = {
   .binding_table_pointers_cmd = _3DSTATE_BINDING_TABLE_POINTERS_VS,
   .sampler_state_pointers_cmd = _3DSTATE_SAMPLER_STATE_POINTERS_VS,
   .constant_cmd = _3DSTATE_CONSTANT_VS,
   .state_cmd = _3DSTATE_VS,
   .state_cmd_size = 6,
};


static void
upload_vs_state(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   const struct brw_stage_state *stage_state = &brw->vs.base;
   const int max_threads_shift = brw->is_haswell ?
      HSW_VS_MAX_THREADS_SHIFT : GEN6_VS_MAX_THREADS_SHIFT;
   /* CACHE_NEW_VS_PROG */
   const struct brw_vec4_prog_data *prog_data = &brw->vs.prog_data->base;

   gen7_emit_vs_workaround_flush(brw);

   /* Use ALT floating point mode for ARB vertex programs, because they
    * require 0^0 == 1.
    */
   bool alt_floating_point_mode = (ctx->Shader.CurrentVertexProgram == NULL);

   unsigned stage_specific_cmd_data[2];
   stage_specific_cmd_data[0] =
      (prog_data->dispatch_grf_start_reg <<
       GEN6_VS_DISPATCH_START_GRF_SHIFT) |
      (prog_data->urb_read_length << GEN6_VS_URB_READ_LENGTH_SHIFT) |
      (0 << GEN6_VS_URB_ENTRY_READ_OFFSET_SHIFT);
   stage_specific_cmd_data[1] =
      ((brw->max_vs_threads - 1) << max_threads_shift) |
      GEN6_VS_STATISTICS_ENABLE |
      GEN6_VS_ENABLE;

   /* BRW_NEW_VS_BINDING_TABLE */
   /* CACHE_NEW_SAMPLER */
   gen7_upload_vec4_state(brw, &vs_upload_params, stage_state,
                          true /* active */, alt_floating_point_mode,
                          prog_data, stage_specific_cmd_data);
}

const struct brw_tracked_state gen7_vs_state = {
   .dirty = {
      .mesa  = _NEW_TRANSFORM | _NEW_PROGRAM_CONSTANTS,
      .brw   = (BRW_NEW_CONTEXT |
		BRW_NEW_VERTEX_PROGRAM |
		BRW_NEW_VS_BINDING_TABLE |
		BRW_NEW_BATCH |
                BRW_NEW_PUSH_CONSTANT_ALLOCATION),
      .cache = CACHE_NEW_VS_PROG | CACHE_NEW_SAMPLER
   },
   .emit = upload_vs_state,
};
