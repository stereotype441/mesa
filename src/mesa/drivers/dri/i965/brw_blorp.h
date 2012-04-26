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

struct brw_context;
struct intel_mipmap_tree;


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
};

class brw_blorp_params
{
public:
   brw_blorp_params();

   virtual uint32_t get_wm_prog(struct brw_context *brw) const = 0;

   uint32_t width;
   uint32_t height;
   brw_hiz_mip_info depth;
   struct intel_mipmap_tree *hiz_mt;
   enum gen6_hiz_op op;
   bool use_wm_prog;
};

class brw_hiz_resolve_params : public brw_blorp_params
{
public:
   brw_hiz_resolve_params(struct intel_mipmap_tree *mt,
                          struct intel_mipmap_tree *hiz_mt,
                          unsigned int level, unsigned int layer,
                          gen6_hiz_op op);

   virtual uint32_t get_wm_prog(struct brw_context *brw) const;
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

class brw_msaa_resolve_params : public brw_blorp_params
{
public:
   brw_msaa_resolve_params(struct intel_mipmap_tree *src_mt,
                           struct intel_mipmap_tree *dst_mt,
                           unsigned int level, unsigned int layer);

   virtual uint32_t get_wm_prog(struct brw_context *brw) const;

private:
   brw_msaa_resolve_prog_key wm_prog_key;
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
