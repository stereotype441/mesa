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

#pragma once

#include <stdint.h>

#include "intel_mipmap_tree.h"

struct brw_context;


/**
 * For an overview of the HiZ operations, see the following sections of the
 * Sandy Bridge PRM, Volume 1, Part2:
 *   - 7.5.3.1 Depth Buffer Clear
 *   - 7.5.3.2 Depth Buffer Resolve
 *   - 7.5.3.3 Hierarchical Depth Buffer Resolve
 */
enum gen6_hiz_op {
   GEN6_HIZ_OP_DEPTH_CLEAR,
   GEN6_HIZ_OP_DEPTH_RESOLVE,
   GEN6_HIZ_OP_HIZ_RESOLVE,
   GEN6_HIZ_OP_NONE,
};

class brw_hiz_mip_info
{
public:
   brw_hiz_mip_info()
      : mt(NULL)
   {
   }

   void set(struct intel_mipmap_tree *mt,
            unsigned int level, unsigned int layer);
   void get_draw_offsets(uint32_t *draw_x, uint32_t *draw_y) const;

   void get_miplevel_dims(uint32_t *width, uint32_t *height) const
   {
      *width = mt->level[level].width;
      *height = mt->level[level].height;
   }

   struct intel_mipmap_tree *mt;
   unsigned int level;
   unsigned int layer;

   /* Setting this flag indicates that the buffer's contents are W-tiled
    * stencil data, but the surface state should be set up for Y tiled
    * MESA_FORMAT_R8 data (this is necessary because surface states don't
    * support W tiling).
    *
    * Since W tiles are 64 pixels wide by 64 pixels high, whereas Y tiles of
    * MESA_FORMAT_R8 data are 128 pixels wide by 32 pixels high, the width and
    * pitch stored in the surface state will be multiplied by 2, and the
    * height will be halved.  Also, since W and Y tiles store their data in a
    * different order, the width and height will be rounded up to a multiple
    * of the tile size, to ensure that the WM program can access the full
    * width and height of the buffer.
    */
   bool map_stencil_as_y_tiled;
};

struct brw_blorp_wm_push_constants
{
   uint16_t dst_x0;
   uint16_t dst_x1;
   uint16_t dst_y0;
   uint16_t dst_y1;

   /* Pad out to an integral number of registers */
   uint16_t pad[12];
};

/* Every 32 bytes of push constant data constitutes one GEN register. */
const unsigned int BRW_BLORP_NUM_PUSH_CONST_REGS =
   sizeof(brw_blorp_wm_push_constants) / 32;

struct brw_blorp_prog_data
{
   unsigned int first_curbe_grf;
};

class brw_blorp_params
{
public:
   brw_blorp_params();

   virtual uint32_t get_wm_prog(struct brw_context *brw,
                                brw_blorp_prog_data **prog_data) const = 0;

   void exec(struct intel_context *intel) const;

   uint32_t width;
   uint32_t height;
   brw_hiz_mip_info depth;
   struct intel_mipmap_tree *hiz_mt;
   brw_hiz_mip_info src;
   brw_hiz_mip_info dst;
   enum gen6_hiz_op op;
   bool use_wm_prog;
   bool src_multisampled;
   brw_blorp_wm_push_constants wm_push_consts;
};

class brw_hiz_resolve_params : public brw_blorp_params
{
public:
   brw_hiz_resolve_params(struct intel_mipmap_tree *mt,
                          struct intel_mipmap_tree *hiz_mt,
                          unsigned int level, unsigned int layer,
                          gen6_hiz_op op);

   virtual uint32_t get_wm_prog(struct brw_context *brw,
                                brw_blorp_prog_data **prog_data) const;
};

enum brw_msaa_coord_transform
{
   BRW_MSAA_COORD_TRANSFORM_STENCIL_SWIZZLE,
   BRW_MSAA_COORD_TRANSFORM_DEPTH_SWIZZLE,
   BRW_MSAA_COORD_TRANSFORM_NORMAL,
};

struct brw_msaa_resolve_prog_key
{
   brw_msaa_coord_transform coord_transform;
   GLuint sampler_msg_type;
};

struct brw_blorp_blit_prog_key
{
   bool blend;

   /* Setting this flag indicates that the source and destination buffers are
    * W-tiled stencil data, but their surface states have been set up for Y
    * tiled MESA_FORMAT_R8 data (this is necessary because surface states
    * don't support W tiling).
    *
    * This causes the WM program to make the appropriate coordinate
    * adjustments to compensate for the differences between W and Y tile
    * layout.
    *
    * Additionally it causes the WM program to discard any fragments whose x
    * and y coordinates are outside the destination rectangle (this is
    * necessary because the memory locations corresponding to a rectangluar
    * region in W tiling do not necessarily correspond to a rectangular region
    * in Y tiling, so to ensure that the proper blit happens, we may have to
    * send a rectangle through the pipeline that is larger than the desired
    * blit).
    *
    * TODO: discarding fragments is not implemented yet.
    */
   bool adjust_coords_for_stencil;

   /* Setting this flag indicates that the source buffer is multisampled, but
    * its surface state has been set up as single-sampled.  So the WM program
    * needs to manually adjust the u and v texture coordinates to select just
    * sample 0 out of each pixel.
    */
   bool manual_downsample;

   /* Setting this flag indicates that the program should kill pixels whose
    * coordinates are out of range.
    */
   bool kill_out_of_range;
};

class brw_msaa_resolve_params : public brw_blorp_params
{
public:
   brw_msaa_resolve_params(struct intel_mipmap_tree *mt);

   virtual uint32_t get_wm_prog(struct brw_context *brw,
                                brw_blorp_prog_data **prog_data) const;

private:
   brw_msaa_resolve_prog_key wm_prog_key;
};

class brw_blorp_blit_params : public brw_blorp_params
{
public:
   brw_blorp_blit_params(struct intel_mipmap_tree *src_mt,
                         struct intel_mipmap_tree *dst_mt,
                         GLuint src_x0, GLuint src_y0,
                         GLuint dst_x0, GLuint dst_y0,
                         GLuint width, GLuint height);

   virtual uint32_t get_wm_prog(struct brw_context *brw,
                                brw_blorp_prog_data **prog_data) const;

private:
   brw_blorp_blit_prog_key wm_prog_key;
};

/**
 * \name HiZ internals
 * \{
 *
 * Used internally by gen6_hiz_exec() and gen7_hiz_exec().
 */

void
gen6_hiz_init(struct brw_context *brw);

void
gen6_hiz_emit_batch_head(struct brw_context *brw,
                         const brw_blorp_params *params);

void
gen6_hiz_emit_vertices(struct brw_context *brw,
                       const brw_blorp_params *params);

void
gen6_hiz_emit_depth_stencil_state(struct brw_context *brw,
                                  const brw_blorp_params *params,
                                  uint32_t *out_offset);
/** \} */

void
gen6_hiz_exec(struct intel_context *intel,
              const brw_blorp_params *params);

void
gen7_hiz_exec(struct intel_context *intel,
              const brw_blorp_params *params);
