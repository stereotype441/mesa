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

#include <stdbool.h>
#include "brw_blorp.h"
#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "brw_util.h"
#include "brw_wm.h"
#include "program/prog_parameter.h"
#include "program/prog_statevars.h"
#include "intel_batchbuffer.h"

static void
upload_wm_state(struct brw_context *brw)
{
   struct intel_context *intel = &brw->intel;
   struct gl_context *ctx = &intel->ctx;
   const struct brw_fragment_program *fp =
      brw_fragment_program_const(brw->fragment_program);
   bool writes_depth = false;
   uint32_t dw1, dw2;

   /* _NEW_BUFFERS */
   bool multisampled_fbo = ctx->DrawBuffer->Visual.samples > 1;

   dw1 = dw2 = 0;
   /* BRW_NEW_BLORP */
   if (!brw->blorp.params)
      dw1 |= GEN7_WM_STATISTICS_ENABLE;
   dw1 |= GEN7_WM_LINE_AA_WIDTH_1_0;
   dw1 |= GEN7_WM_LINE_END_CAP_AA_WIDTH_0_5;

   if (brw->blorp.params) {
      switch (brw->blorp.params->hiz_op) {
      case GEN6_HIZ_OP_DEPTH_CLEAR:
         dw1 |= GEN7_WM_DEPTH_CLEAR;
         break;
      case GEN6_HIZ_OP_DEPTH_RESOLVE:
         dw1 |= GEN7_WM_DEPTH_RESOLVE;
         break;
      case GEN6_HIZ_OP_HIZ_RESOLVE:
         dw1 |= GEN7_WM_HIERARCHICAL_DEPTH_RESOLVE;
         break;
      case GEN6_HIZ_OP_NONE:
         break;
      default:
         assert(0);
         break;
      }

      dw1 |= 0 << GEN7_WM_BARYCENTRIC_INTERPOLATION_MODE_SHIFT; /* No interp */
      if (brw->blorp.params->get_wm_prog) {
         dw1 |= GEN7_WM_KILL_ENABLE; /* TODO: temporarily smash on */
         dw1 |= GEN7_WM_DISPATCH_ENABLE; /* We are rendering */
      }

      if (brw->blorp.params->num_samples > 1) {
         dw1 |= GEN7_WM_MSRAST_ON_PATTERN;
         if (brw->blorp.params->get_wm_prog &&
             brw->blorp.prog_data->persample_msaa_dispatch)
            dw2 |= GEN7_WM_MSDISPMODE_PERSAMPLE;
         else
            dw2 |= GEN7_WM_MSDISPMODE_PERPIXEL;
      } else {
         dw1 |= GEN7_WM_MSRAST_OFF_PIXEL;
         dw2 |= GEN7_WM_MSDISPMODE_PERSAMPLE;
      }
   } else {
      /* _NEW_LINE */
      if (ctx->Line.StippleFlag)
         dw1 |= GEN7_WM_LINE_STIPPLE_ENABLE;

      /* _NEW_POLYGON */
      if (ctx->Polygon.StippleFlag)
         dw1 |= GEN7_WM_POLYGON_STIPPLE_ENABLE;

      /* BRW_NEW_FRAGMENT_PROGRAM */
      if (fp->program.Base.InputsRead & VARYING_BIT_POS)
         dw1 |= GEN7_WM_USES_SOURCE_DEPTH | GEN7_WM_USES_SOURCE_W;
      if (fp->program.Base.OutputsWritten &
          BITFIELD64_BIT(FRAG_RESULT_DEPTH)) {
         writes_depth = true;
         dw1 |= GEN7_WM_PSCDEPTH_ON;
      }
      /* CACHE_NEW_WM_PROG */
      dw1 |= brw->wm.prog_data->barycentric_interp_modes <<
         GEN7_WM_BARYCENTRIC_INTERPOLATION_MODE_SHIFT;

      /* _NEW_COLOR, _NEW_MULTISAMPLE */
      if (fp->program.UsesKill || ctx->Color.AlphaEnabled ||
          ctx->Multisample.SampleAlphaToCoverage)
         dw1 |= GEN7_WM_KILL_ENABLE;

      /* _NEW_BUFFERS */
      if (brw_color_buffer_write_enabled(brw) || writes_depth ||
          dw1 & GEN7_WM_KILL_ENABLE) {
         dw1 |= GEN7_WM_DISPATCH_ENABLE;
      }
      if (multisampled_fbo) {
         /* _NEW_MULTISAMPLE */
         if (ctx->Multisample.Enabled)
            dw1 |= GEN7_WM_MSRAST_ON_PATTERN;
         else
            dw1 |= GEN7_WM_MSRAST_OFF_PIXEL;
         dw2 |= GEN7_WM_MSDISPMODE_PERPIXEL;
      } else {
         dw1 |= GEN7_WM_MSRAST_OFF_PIXEL;
         dw2 |= GEN7_WM_MSDISPMODE_PERSAMPLE;
      }
   }

   BEGIN_BATCH(3);
   OUT_BATCH(_3DSTATE_WM << 16 | (3 - 2));
   OUT_BATCH(dw1);
   OUT_BATCH(dw2);
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen7_wm_state = {
   .dirty = {
      .mesa  = (_NEW_LINE | _NEW_POLYGON |
	        _NEW_COLOR | _NEW_BUFFERS |
                _NEW_MULTISAMPLE),
      .brw   = (BRW_NEW_FRAGMENT_PROGRAM |
		BRW_NEW_BATCH |
                BRW_NEW_BLORP),
      .cache = CACHE_NEW_WM_PROG,
   },
   .emit = upload_wm_state,
};

static void
upload_ps_state(struct brw_context *brw)
{
   struct intel_context *intel = &brw->intel;
   struct gl_context *ctx = &intel->ctx;
   uint32_t dw2, dw4, dw5;
   const int max_threads_shift = brw->intel.is_haswell ?
      HSW_PS_MAX_THREADS_SHIFT : IVB_PS_MAX_THREADS_SHIFT;

   unsigned num_push_const_regs;
   uint32_t prog_offset, prog_offset_2;
   bool scratch_needed;

   /* BRW_NEW_PS_BINDING_TABLE */
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_BINDING_TABLE_POINTERS_PS << 16 | (2 - 2));
   OUT_BATCH(brw->wm.bind_bo_offset);
   ADVANCE_BATCH();

   /* CACHE_NEW_SAMPLER */
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_SAMPLER_STATE_POINTERS_PS << 16 | (2 - 2));
   OUT_BATCH(brw->sampler.offset);
   ADVANCE_BATCH();

   /* BRW_NEW_BLORP */
   if (brw->blorp.params) {
      /* Make sure the push constants fill an exact integer number of
       * registers.
       */
      assert(sizeof(struct brw_blorp_wm_push_constants) % 32 == 0);

      num_push_const_regs = brw->blorp.params->get_wm_prog ?
         BRW_BLORP_NUM_PUSH_CONST_REGS : 0;
   } else {
      /* CACHE_NEW_WM_PROG */
      num_push_const_regs = ALIGN(brw->wm.prog_data->nr_params,
                                  brw->wm.prog_data->dispatch_width) / 8;
   }

   if (num_push_const_regs == 0) {
      /* Disable the push constant buffers. */
      BEGIN_BATCH(7);
      OUT_BATCH(_3DSTATE_CONSTANT_PS << 16 | (7 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(7);
      OUT_BATCH(_3DSTATE_CONSTANT_PS << 16 | (7 - 2));

      OUT_BATCH(num_push_const_regs);
      OUT_BATCH(0);
      /* Pointer to the WM constant buffer.  Covered by the set of
       * state flags from gen6_upload_wm_push_constants.
       */
      OUT_BATCH(brw->wm.push_const_offset);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   dw2 = dw4 = dw5 = 0;

   dw4 |= (brw->max_wm_threads - 1) << max_threads_shift;

   if (intel->is_haswell)
      dw4 |= SET_FIELD(1, HSW_PS_SAMPLE_MASK); /* 1 sample for now */

   if (brw->blorp.params) {
      /* If there's a WM program, we need to do 16-pixel dispatch since that's
       * what the program is compiled for.  If there isn't, then it shouldn't
       * matter because no program is actually being run.  However, the
       * hardware gets angry if we don't enable at least one dispatch mode, so
       * just enable 16-pixel dispatch unconditionally.
       */
      dw4 |= GEN7_PS_16_DISPATCH_ENABLE;

      if (brw->blorp.params->get_wm_prog) {
         dw2 |= 1 << GEN7_PS_SAMPLER_COUNT_SHIFT; /* Up to 4 samplers */
         dw4 |= GEN7_PS_PUSH_CONSTANT_ENABLE;
         dw5 |= brw->blorp.prog_data->first_curbe_grf <<
            GEN7_PS_DISPATCH_START_GRF_SHIFT_0;
      }

      prog_offset = brw->blorp.params->get_wm_prog ?
         brw->blorp.prog_offset : 0;
      prog_offset_2 = 0;
      scratch_needed = false;
   } else {
      /* CACHE_NEW_SAMPLER */
      dw2 |= (ALIGN(brw->sampler.count, 4) / 4) << GEN7_PS_SAMPLER_COUNT_SHIFT;

      /* Use ALT floating point mode for ARB fragment programs, because they
       * require 0^0 == 1.  Even though _CurrentFragmentProgram is used for
       * rendering, CurrentFragmentProgram is used for this check to
       * differentiate between the GLSL and non-GLSL cases.
       */
      if (intel->ctx.Shader.CurrentFragmentProgram == NULL)
         dw2 |= GEN7_PS_FLOATING_POINT_MODE_ALT;

      /* CACHE_NEW_WM_PROG */
      if (brw->wm.prog_data->nr_params > 0)
         dw4 |= GEN7_PS_PUSH_CONSTANT_ENABLE;

      /* CACHE_NEW_WM_PROG | _NEW_COLOR
       *
       * The hardware wedges if you have this bit set but don't turn on any
       * dual source blend factors.
       */
      if (brw->wm.prog_data->dual_src_blend &&
          (ctx->Color.BlendEnabled & 1) &&
          ctx->Color.Blend[0]._UsesDualSrc) {
         dw4 |= GEN7_PS_DUAL_SOURCE_BLEND_ENABLE;
      }

      /* BRW_NEW_FRAGMENT_PROGRAM */
      if (brw->fragment_program->Base.InputsRead != 0)
         dw4 |= GEN7_PS_ATTRIBUTE_ENABLE;

      dw4 |= GEN7_PS_8_DISPATCH_ENABLE;
      if (brw->wm.prog_data->prog_offset_16)
         dw4 |= GEN7_PS_16_DISPATCH_ENABLE;

      dw5 |= (brw->wm.prog_data->first_curbe_grf <<
              GEN7_PS_DISPATCH_START_GRF_SHIFT_0);
      dw5 |= (brw->wm.prog_data->first_curbe_grf_16 <<
              GEN7_PS_DISPATCH_START_GRF_SHIFT_2);

      prog_offset = brw->wm.prog_offset;
      prog_offset_2 = brw->wm.prog_offset + brw->wm.prog_data->prog_offset_16;
      scratch_needed = brw->wm.prog_data->total_scratch;
   }

   BEGIN_BATCH(8);
   OUT_BATCH(_3DSTATE_PS << 16 | (8 - 2));
   OUT_BATCH(prog_offset);
   OUT_BATCH(dw2);
   if (scratch_needed) {
      OUT_RELOC(brw->wm.scratch_bo,
		I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
		ffs(brw->wm.prog_data->total_scratch) - 11);
   } else {
      OUT_BATCH(0);
   }
   OUT_BATCH(dw4);
   OUT_BATCH(dw5);
   OUT_BATCH(0); /* kernel 1 pointer */
   OUT_BATCH(prog_offset_2);
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen7_ps_state = {
   .dirty = {
      .mesa  = (_NEW_PROGRAM_CONSTANTS |
		_NEW_COLOR),
      .brw   = (BRW_NEW_FRAGMENT_PROGRAM |
		BRW_NEW_PS_BINDING_TABLE |
		BRW_NEW_BATCH),
      .cache = (CACHE_NEW_SAMPLER |
		CACHE_NEW_WM_PROG)
   },
   .emit = upload_ps_state,
};
