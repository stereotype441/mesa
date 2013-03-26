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

#include <assert.h>

#include "intel_batchbuffer.h"
#include "intel_fbo.h"
#include "intel_mipmap_tree.h"

#include "brw_context.h"
#include "brw_defines.h"
#include "brw_state.h"

#include "brw_blorp.h"
#include "gen7_blorp.h"


static void
gen7_blorp_emit_cc_viewport(struct brw_context *brw,
			    const brw_blorp_params *params)
{
   struct intel_context *intel = &brw->intel;
   struct brw_cc_viewport *ccv;
   uint32_t cc_vp_offset;

   ccv = (struct brw_cc_viewport *)brw_state_batch(brw, AUB_TRACE_CC_VP_STATE,
						   sizeof(*ccv), 32,
						   &cc_vp_offset);
   ccv->min_depth = 0.0;
   ccv->max_depth = 1.0;

   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VIEWPORT_STATE_POINTERS_CC << 16 | (2 - 2));
   OUT_BATCH(cc_vp_offset);
   ADVANCE_BATCH();
}


/* SURFACE_STATE for renderbuffer or texture surface (see
 * brw_update_renderbuffer_surface and brw_update_texture_surface)
 */
uint32_t
gen7_blorp_emit_surface_state(struct brw_context *brw,
                              const brw_blorp_params *params,
                              const brw_blorp_surface_info *surface,
                              uint32_t read_domains, uint32_t write_domain,
                              bool is_render_target)
{
   struct intel_context *intel = &brw->intel;

   uint32_t wm_surf_offset;
   uint32_t width = surface->mip_info.width;
   uint32_t height = surface->mip_info.height;
   /* Note: since gen7 uses INTEL_MSAA_LAYOUT_CMS or INTEL_MSAA_LAYOUT_UMS for
    * color surfaces, width and height are measured in pixels; we don't need
    * to divide them by 2 as we do for Gen6 (see
    * gen6_blorp_emit_surface_state).
    */
   struct intel_region *region = surface->mip_info.mt->region;
   uint32_t tile_x, tile_y;

   uint32_t tiling = surface->map_stencil_as_y_tiled
      ? I915_TILING_Y : region->tiling;

   uint32_t *surf = (uint32_t *)
      brw_state_batch(brw, AUB_TRACE_SURFACE_STATE, 8 * 4, 32, &wm_surf_offset);
   memset(surf, 0, 8 * 4);

   surf[0] = BRW_SURFACE_2D << BRW_SURFACE_TYPE_SHIFT |
             surface->brw_surfaceformat << BRW_SURFACE_FORMAT_SHIFT |
             gen7_surface_tiling_mode(tiling);

   if (surface->mip_info.mt->align_h == 4)
      surf[0] |= GEN7_SURFACE_VALIGN_4;
   if (surface->mip_info.mt->align_w == 8)
      surf[0] |= GEN7_SURFACE_HALIGN_8;

   if (surface->array_spacing_lod0)
      surf[0] |= GEN7_SURFACE_ARYSPC_LOD0;
   else
      surf[0] |= GEN7_SURFACE_ARYSPC_FULL;

   /* reloc */
   surf[1] = brw_blorp_compute_tile_offsets(surface, &tile_x, &tile_y) +
      region->bo->offset;

   /* Note that the low bits of these fields are missing, so
    * there's the possibility of getting in trouble.
    */
   assert(tile_x % 4 == 0);
   assert(tile_y % 2 == 0);
   surf[5] = SET_FIELD(tile_x / 4, BRW_SURFACE_X_OFFSET) |
             SET_FIELD(tile_y / 2, BRW_SURFACE_Y_OFFSET);

   surf[2] = SET_FIELD(width - 1, GEN7_SURFACE_WIDTH) |
             SET_FIELD(height - 1, GEN7_SURFACE_HEIGHT);

   uint32_t pitch_bytes = region->pitch;
   if (surface->map_stencil_as_y_tiled)
      pitch_bytes *= 2;
   surf[3] = pitch_bytes - 1;

   surf[4] = gen7_surface_msaa_bits(surface->num_samples, surface->msaa_layout);
   if (surface->msaa_layout == INTEL_MSAA_LAYOUT_CMS) {
      gen7_set_surface_mcs_info(brw, surf, wm_surf_offset,
                                surface->mip_info.mt->mcs_mt,
                                is_render_target);
   }

   if (intel->is_haswell) {
      surf[7] = SET_FIELD(HSW_SCS_RED,   GEN7_SURFACE_SCS_R) |
                SET_FIELD(HSW_SCS_GREEN, GEN7_SURFACE_SCS_G) |
                SET_FIELD(HSW_SCS_BLUE,  GEN7_SURFACE_SCS_B) |
                SET_FIELD(HSW_SCS_ALPHA, GEN7_SURFACE_SCS_A);
   }

   /* Emit relocation to surface contents */
   drm_intel_bo_emit_reloc(intel->batch.bo,
                           wm_surf_offset + 4,
                           region->bo,
                           surf[1] - region->bo->offset,
                           read_domains, write_domain);

   gen7_check_surface_setup(surf, is_render_target);

   return wm_surf_offset;
}


static void
gen7_blorp_emit_depth_stencil_config(struct brw_context *brw,
                                     const brw_blorp_params *params)
{
   struct intel_context *intel = &brw->intel;
   struct gl_context *ctx = &intel->ctx;
   uint32_t draw_x = params->depth.x_offset;
   uint32_t draw_y = params->depth.y_offset;
   uint32_t tile_mask_x, tile_mask_y;

   brw_get_depthstencil_tile_masks(params->depth.mt,
                                   params->depth.level,
                                   params->depth.layer,
                                   NULL,
                                   &tile_mask_x, &tile_mask_y);

   /* 3DSTATE_DEPTH_BUFFER */
   {
      uint32_t tile_x = draw_x & tile_mask_x;
      uint32_t tile_y = draw_y & tile_mask_y;
      uint32_t offset =
         intel_region_get_aligned_offset(params->depth.mt->region,
                                         draw_x & ~tile_mask_x,
                                         draw_y & ~tile_mask_y, false);

      /* According to the Sandy Bridge PRM, volume 2 part 1, pp326-327
       * (3DSTATE_DEPTH_BUFFER dw5), in the documentation for "Depth
       * Coordinate Offset X/Y":
       *
       *   "The 3 LSBs of both offsets must be zero to ensure correct
       *   alignment"
       *
       * We have no guarantee that tile_x and tile_y are correctly aligned,
       * since they are determined by the mipmap layout, which is only aligned
       * to multiples of 4.
       *
       * So, to avoid hanging the GPU, just smash the low order 3 bits of
       * tile_x and tile_y to 0.  This is a temporary workaround until we come
       * up with a better solution.
       */
      WARN_ONCE((tile_x & 7) || (tile_y & 7),
                "Depth/stencil buffer needs alignment to 8-pixel boundaries.\n"
                "Truncating offset, bad rendering may occur.\n");
      tile_x &= ~7;
      tile_y &= ~7;

      intel_emit_depth_stall_flushes(intel);

      BEGIN_BATCH(7);
      OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER << 16 | (7 - 2));
      OUT_BATCH((params->depth.mt->region->pitch - 1) |
                params->depth_format << 18 |
                1 << 22 | /* hiz enable */
                1 << 28 | /* depth write */
                BRW_SURFACE_2D << 29);
      OUT_RELOC(params->depth.mt->region->bo,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                offset);
      OUT_BATCH((params->depth.width + tile_x - 1) << 4 |
                (params->depth.height + tile_y - 1) << 18);
      OUT_BATCH(0);
      OUT_BATCH(tile_x |
                tile_y << 16);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_HIER_DEPTH_BUFFER */
   {
      struct intel_region *hiz_region = params->depth.mt->hiz_mt->region;
      uint32_t hiz_offset =
         intel_region_get_aligned_offset(hiz_region,
                                         draw_x & ~tile_mask_x,
                                         (draw_y & ~tile_mask_y) / 2, false);

      BEGIN_BATCH(3);
      OUT_BATCH((GEN7_3DSTATE_HIER_DEPTH_BUFFER << 16) | (3 - 2));
      OUT_BATCH(hiz_region->pitch - 1);
      OUT_RELOC(hiz_region->bo,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                hiz_offset);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_STENCIL_BUFFER */
   {
      BEGIN_BATCH(3);
      OUT_BATCH((GEN7_3DSTATE_STENCIL_BUFFER << 16) | (3 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }
}


static void
gen7_blorp_emit_depth_disable(struct brw_context *brw,
                              const brw_blorp_params *params)
{
   struct intel_context *intel = &brw->intel;

   BEGIN_BATCH(7);
   OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER << 16 | (7 - 2));
   OUT_BATCH(BRW_DEPTHFORMAT_D32_FLOAT << 18 | (BRW_SURFACE_NULL << 29));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}


/* 3DSTATE_CLEAR_PARAMS
 *
 * From the BSpec, Volume 2a.11 Windower, Section 1.5.6.3.2
 * 3DSTATE_CLEAR_PARAMS:
 *    [DevIVB] 3DSTATE_CLEAR_PARAMS must always be programmed in the along
 *    with the other Depth/Stencil state commands(i.e.  3DSTATE_DEPTH_BUFFER,
 *    3DSTATE_STENCIL_BUFFER, or 3DSTATE_HIER_DEPTH_BUFFER).
 */
static void
gen7_blorp_emit_clear_params(struct brw_context *brw,
                             const brw_blorp_params *params)
{
   struct intel_context *intel = &brw->intel;

   BEGIN_BATCH(3);
   OUT_BATCH(GEN7_3DSTATE_CLEAR_PARAMS << 16 | (3 - 2));
   OUT_BATCH(params->depth.mt ? params->depth.mt->depth_clear_value : 0);
   OUT_BATCH(GEN7_DEPTH_CLEAR_VALID);
   ADVANCE_BATCH();
}


/* 3DPRIMITIVE */
static void
gen7_blorp_emit_primitive(struct brw_context *brw,
                          const brw_blorp_params *params)
{
   struct intel_context *intel = &brw->intel;

   BEGIN_BATCH(7);
   OUT_BATCH(CMD_3D_PRIM << 16 | (7 - 2));
   OUT_BATCH(GEN7_3DPRIM_VERTEXBUFFER_ACCESS_SEQUENTIAL |
             _3DPRIM_RECTLIST);
   OUT_BATCH(3); /* vertex count per instance */
   OUT_BATCH(0);
   OUT_BATCH(1); /* instance count */
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}


/**
 * \copydoc gen6_blorp_exec()
 */
void
gen7_blorp_exec(struct intel_context *intel,
                const brw_blorp_params *params)
{
   struct gl_context *ctx = &intel->ctx;
   struct brw_context *brw = brw_context(ctx);

   brw_wm_prog.emit(brw);
   gen6_blorp_emit_batch_head(brw, params);
   gen7_push_constant_alloc.emit(brw);
   gen6_multisample_state.emit(brw);
   brw_state_base_address.emit(brw);
   brw_vertices.emit(brw);
   gen7_urb.emit(brw);
   gen6_blend_state.emit(brw);
   gen6_color_calc_state.emit(brw);
   gen7_blend_state_pointer.emit(brw);
   gen7_cc_state_pointer.emit(brw);
   gen6_depth_stencil_state.emit(brw);
   gen7_depth_stencil_state_pointer.emit(brw);
   gen6_wm_push_constants.emit(brw);
   gen6_renderbuffer_surfaces.emit(brw);
   brw_texture_surfaces.emit(brw);
   brw_wm_binding_table.emit(brw);
   gen7_samplers.emit(brw);
   gen7_vs_state.emit(brw);
   gen7_disable_stages.emit(brw);
   gen7_sol_state.emit(brw);
   gen7_clip_state.emit(brw);
   gen7_sf_state.emit(brw);
   gen7_sbe_state.emit(brw);
   gen7_wm_state.emit(brw);
   gen7_ps_state.emit(brw);
   gen7_blorp_emit_cc_viewport(brw, params);

   if (params->depth.mt)
      gen7_blorp_emit_depth_stencil_config(brw, params);
   else
      gen7_blorp_emit_depth_disable(brw, params);
   gen7_blorp_emit_clear_params(brw, params);
   gen6_blorp_emit_drawing_rectangle(brw, params);
   gen7_blorp_emit_primitive(brw, params);
}
