/*
 * Copyright © 2013 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "intel_batchbuffer.h"


static void
gen7_upload_gs_push_constants(struct brw_context *brw)
{
   /* BRW_NEW_GEOMETRY_PROGRAM */
   const struct brw_geometry_program *vp =
      (struct brw_geometry_program *) brw->geometry_program;
   if (!vp)
      return;

   /* CACHE_NEW_GS_PROG */
   const struct brw_vec4_prog_data *prog_data = &brw->gs.prog_data->base;
   struct brw_stage_state *stage_state = &brw->gs.base;

   gen6_upload_vec4_push_constants(brw, &vp->program.Base, prog_data,
                                   stage_state, AUB_TRACE_VS_CONSTANTS);
}

const struct brw_tracked_state gen7_gs_push_constants = {
   .dirty = {
      .mesa  = _NEW_TRANSFORM | _NEW_PROGRAM_CONSTANTS,
      .brw   = (BRW_NEW_BATCH |
		BRW_NEW_GEOMETRY_PROGRAM),
      .cache = CACHE_NEW_GS_PROG,
   },
   .emit = gen7_upload_gs_push_constants,
};


static const struct gen7_vec4_upload_params gs_upload_params = {
   .binding_table_pointers_cmd = _3DSTATE_BINDING_TABLE_POINTERS_GS,
   .sampler_state_pointers_cmd = _3DSTATE_SAMPLER_STATE_POINTERS_GS,
   .constant_cmd = _3DSTATE_CONSTANT_GS,
   .state_cmd = _3DSTATE_GS,
   .state_cmd_size = 7,
};


static void
upload_gs_state(struct brw_context *brw)
{
   const struct brw_stage_state *stage_state = &brw->gs.base;
   const int max_threads_shift = brw->is_haswell ?
      HSW_GS_MAX_THREADS_SHIFT : GEN6_GS_MAX_THREADS_SHIFT;
   /* BRW_NEW_GEOMETRY_PROGRAM */
   bool active = brw->geometry_program;
   /* CACHE_NEW_GS_PROG */
   const struct brw_vec4_prog_data *prog_data = &brw->gs.prog_data->base;

   unsigned stage_specific_cmd_data[3];
   if (active) {
      stage_specific_cmd_data[0] =
         ((brw->gs.prog_data->output_vertex_size_hwords * 2 - 1) <<
          GEN7_GS_OUTPUT_VERTEX_SIZE_SHIFT) |
         (brw->gs.prog_data->output_topology <<
          GEN7_GS_OUTPUT_TOPOLOGY_SHIFT) |
         (prog_data->urb_read_length <<
          GEN6_GS_URB_READ_LENGTH_SHIFT) |
         (0 << GEN6_GS_URB_ENTRY_READ_OFFSET_SHIFT) |
         (prog_data->dispatch_grf_start_reg <<
          GEN6_GS_DISPATCH_START_GRF_SHIFT);
      /* Note: the meaning of the GEN7_GS_REORDER_MODE bit changes between Ivy
       * Bridge and Haswell.
       *
       * On Ivy bridge, setting this bit causes the vertices of a triangle
       * strip to be delivered to the geometry shader in an order that does
       * not strictly follow the OpenGL spec, but preserves triangle
       * orientation.  For example, if the vertices are (1, 2, 3, 4, 5), then
       * the geometry shader sees triangles:
       *
       * (1, 2, 3), (2, 4, 3), (3, 4, 5)
       *
       * (Clearing the bit is even worse, because it fails to preserve
       * orientation).
       *
       * Triangle strips with adjacency always ordered in a way that preserves
       * triangle orientation but does not strictly follow the OpenGL spec,
       * regardless of the setting of this bit.
       *
       * On Haswell, both triangle strips and triangle strips with adjacency
       * are always ordered in a way that preserves triangle orientation.
       * Setting this bit causes the ordering to strictly follow the OpenGL
       * spec.
       *
       * So in either case we want to set the bit.  Unfortunately on Ivy
       * Bridge this will get the order close to correct but not perfect.
       */
      stage_specific_cmd_data[1] =
         ((brw->max_gs_threads - 1) << max_threads_shift) |
         (brw->gs.prog_data->control_data_format <<
          GEN7_GS_CONTROL_DATA_FORMAT_SHIFT) |
         (brw->gs.prog_data->control_data_header_size_hwords <<
          GEN7_GS_CONTROL_DATA_HEADER_SIZE_SHIFT) |
         GEN7_GS_DISPATCH_MODE_DUAL_OBJECT |
         GEN6_GS_STATISTICS_ENABLE |
         (brw->gs.prog_data->include_primitive_id ?
          GEN7_GS_INCLUDE_PRIMITIVE_ID : 0) |
         GEN7_GS_REORDER_MODE |
         GEN7_GS_ENABLE;
      stage_specific_cmd_data[2] = 0;
   } else {
      stage_specific_cmd_data[0] =
         (1 << GEN6_GS_DISPATCH_START_GRF_SHIFT) |
         (0 << GEN6_GS_URB_READ_LENGTH_SHIFT) |
         GEN7_GS_INCLUDE_VERTEX_HANDLES |
         (0 << GEN6_GS_URB_ENTRY_READ_OFFSET_SHIFT);
      stage_specific_cmd_data[1] =
         (0 << GEN6_GS_MAX_THREADS_SHIFT) |
         GEN6_GS_STATISTICS_ENABLE;
      stage_specific_cmd_data[2] = 0;
   }

   /* BRW_NEW_GS_BINDING_TABLE */
   /* CACHE_NEW_SAMPLER */
   gen7_upload_vec4_state(brw, &gs_upload_params, stage_state, active,
                          false /* alt_floating_point_mode */, prog_data,
                          stage_specific_cmd_data);
}

const struct brw_tracked_state gen7_gs_state = {
   .dirty = {
      .mesa  = _NEW_PROGRAM_CONSTANTS,
      .brw   = (BRW_NEW_CONTEXT |
                BRW_NEW_GEOMETRY_PROGRAM |
                BRW_NEW_GS_BINDING_TABLE |
                BRW_NEW_BATCH |
                BRW_NEW_PUSH_CONSTANT_ALLOCATION),
      .cache = CACHE_NEW_GS_PROG | CACHE_NEW_SAMPLER
   },
   .emit = upload_gs_state,
};
