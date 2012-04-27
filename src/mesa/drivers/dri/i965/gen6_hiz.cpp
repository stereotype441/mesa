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
#include "gen6_hiz.h"
#include "brw_eu.h"

#include "glsl/ralloc.h"

/**
 * \name Constants for HiZ VBO
 * \{
 *
 * \see brw_context::hiz::vertex_bo
 */
#define GEN6_HIZ_NUM_VERTICES 3
#define GEN6_HIZ_NUM_VUE_ELEMS 8
#define GEN6_HIZ_VBO_SIZE (GEN6_HIZ_NUM_VERTICES \
                           * GEN6_HIZ_NUM_VUE_ELEMS \
                           * sizeof(float))
/** \} */

enum {
   GEN6_HIZ_TEXTURE_BINDING_TABLE_INDEX,
   GEN6_HIZ_RENDERBUFFER_BINDING_TABLE_INDEX,
   GEN6_HIZ_NUM_BINDING_TABLE_ENTRIES
};

void
brw_hiz_mip_info::set(struct intel_mipmap_tree *mt,
                      unsigned int level, unsigned int layer)
{
   intel_miptree_check_level_layer(mt, level, layer);

   this->mt = mt;
   this->level = level;
   this->layer = layer;
}

void
brw_hiz_mip_info::get_draw_offsets(uint32_t *draw_x, uint32_t *draw_y) const
{
   /* Construct a dummy renderbuffer just to extract tile offsets. */
   struct intel_renderbuffer rb;
   rb.mt = mt;
   rb.mt_level = level;
   rb.mt_layer = layer;
   intel_renderbuffer_set_draw_offset(&rb);
   *draw_x = rb.draw_x;
   *draw_y = rb.draw_y;
}

brw_blorp_params::brw_blorp_params()
   : width(0),
     height(0),
     hiz_mt(NULL),
     op(GEN6_HIZ_OP_NONE),
     use_wm_prog(false),
     stencil_magic(false),
     src_multisampled(false)
{
}

brw_hiz_resolve_params::brw_hiz_resolve_params(struct intel_mipmap_tree *mt,
                                               struct intel_mipmap_tree *hiz_mt,
                                               unsigned int level,
                                               unsigned int layer,
                                               gen6_hiz_op op)
{
   assert(op != GEN6_HIZ_OP_DEPTH_CLEAR); /* Not implemented yet. */
   this->op = op;

   depth.set(mt, level, layer);
   depth.get_miplevel_dims(&width, &height);

   assert(hiz_mt != NULL);
   this->hiz_mt = hiz_mt;
}

brw_msaa_resolve_params::brw_msaa_resolve_params(struct intel_mipmap_tree *mt)
{
   src.set(mt->msaa_mt, 0, 0);
   dst.set(mt, 0, 0);
   dst.get_miplevel_dims(&width, &height);

   use_wm_prog = true;
   memset(&wm_prog_key, 0, sizeof(wm_prog_key));
   if (mt->format == MESA_FORMAT_S8) {
      wm_prog_key.coord_transform = BRW_MSAA_COORD_TRANSFORM_STENCIL_SWIZZLE;
      wm_prog_key.sampler_msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_LD; /* TODO: different for Gen7? */
      stencil_magic = true;
      width = ALIGN(width, 64) / 2;
      height = ALIGN(height, 64) / 2;
   } else if (_mesa_get_format_base_format(mt->format) == GL_DEPTH_COMPONENT) { /* TODO: handle GL_DEPTH_STENCIL? */
      wm_prog_key.coord_transform = BRW_MSAA_COORD_TRANSFORM_DEPTH_SWIZZLE;
      wm_prog_key.sampler_msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE_LD; /* TODO: different for Gen7? */
   } else {
      wm_prog_key.coord_transform = BRW_MSAA_COORD_TRANSFORM_NORMAL;
      wm_prog_key.sampler_msg_type = GEN5_SAMPLER_MESSAGE_SAMPLE; /* TODO: different for Gen7? */
      src_multisampled = true;
   }
}

class brw_msaa_resolve_program
{
public:
   brw_msaa_resolve_program(struct brw_context *brw,
                            const brw_msaa_resolve_prog_key *key);
   ~brw_msaa_resolve_program();

   const GLuint *compile(struct brw_context *brw, GLuint *program_size);

private:
   void alloc_regs();
   void emit_frag_coord_computation();
   void emit_coord_transform_stencil_swizzle();
   void emit_coord_transform_depth_swizzle();
   void emit_coord_transform_normal();
   void emit_texture_lookup();
   void emit_render_target_write();

   void *mem_ctx;
   struct brw_context *brw;
   const brw_msaa_resolve_prog_key *key;
   struct brw_compile func;

   /* Pixel X/Y coordinates (always in R1). */
   struct brw_reg R1;

   /* Data returned from texture lookup (4 vec16's) */
   struct brw_reg Rdata;

   /* X coordinate of each fragment */
   struct brw_reg x_frag;

   /* Y coordinate of each fragment */
   struct brw_reg y_frag;

   /* U coordinate for texture lookup */
   struct brw_reg u_tex;

   /* V coordinate for texture lookup */
   struct brw_reg v_tex;

   /* M2-3: u coordinate */
   GLuint base_mrf;
   struct brw_reg mrf_u_float;

   /* M4-5: v coordinate */
   struct brw_reg mrf_v_float;

   /* M6-7: r coordinate */
   struct brw_reg mrf_r_float;
};

void
brw_msaa_resolve_program::alloc_regs()
{
   /* R1 is part of the payload to the thread; the first available
    * general-purpose register is R2.
    */
   this->R1 = retype(brw_vec8_grf(1, 0), BRW_REGISTER_TYPE_UW);
   int reg = 2;
   this->Rdata = vec16(brw_vec8_grf(reg, 0)); reg += 8;
   this->x_frag = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->y_frag = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->u_tex = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->v_tex = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));

   int mrf = 2;
   this->base_mrf = mrf;
   this->mrf_u_float = vec16(brw_message_reg(mrf)); mrf += 2;
   this->mrf_v_float = vec16(brw_message_reg(mrf)); mrf += 2;
   this->mrf_r_float = vec16(brw_message_reg(mrf)); mrf += 2;
}

brw_msaa_resolve_program::brw_msaa_resolve_program(struct brw_context *brw,
                                                   const brw_msaa_resolve_prog_key *key)
   : mem_ctx(ralloc_context(NULL)),
     brw(brw),
     key(key)
{
   brw_init_compile(brw, &func, mem_ctx);
}

brw_msaa_resolve_program::~brw_msaa_resolve_program()
{
   ralloc_free(mem_ctx);
}

const GLuint *
brw_msaa_resolve_program::compile(struct brw_context *brw,
                                  GLuint *program_size)
{
   brw_set_compression_control(&func, BRW_COMPRESSION_NONE);

   alloc_regs();

   emit_frag_coord_computation();

   switch (this->key->coord_transform) {
   case BRW_MSAA_COORD_TRANSFORM_STENCIL_SWIZZLE:
      emit_coord_transform_stencil_swizzle();
      break;
   case BRW_MSAA_COORD_TRANSFORM_DEPTH_SWIZZLE:
      emit_coord_transform_depth_swizzle();
      break;
   case BRW_MSAA_COORD_TRANSFORM_NORMAL:
      emit_coord_transform_normal();
      break;
   default:
      assert(false);
      break;
   }

   emit_texture_lookup();

   return brw_get_program(&func, program_size);
}

void
brw_msaa_resolve_program::emit_frag_coord_computation()
{
   /* R1.2[15:0] = X coordinate of upper left pixel of subspan 0 (pixel 0)
    * R1.3[15:0] = X coordinate of upper left pixel of subspan 1 (pixel 4)
    * R1.4[15:0] = X coordinate of upper left pixel of subspan 2 (pixel 8)
    * R1.5[15:0] = X coordinate of upper left pixel of subspan 3 (pixel 12)
    *
    * Pixels within a subspan are laid out in this arrangement:
    * 0 1
    * 2 3
    *
    * So, to compute the coordinates of each pixel, we need to read every 2nd
    * 16-bit value (vstride=2) from R1, starting at the 4th 16-bit value
    * (suboffset=4), and duplicate each value 4 times (hstride=0, width=4).
    * In other words, the data we want to access is R1.4<2;4,0>UW.
    *
    * Then, we need to add the repeating sequence (0, 1, 0, 1, ...) to the
    * result, since pixels n+1 and n+3 are in the right half of the subspan.
    */
   brw_ADD(&func, x_frag, stride(suboffset(R1, 4), 2, 4, 0),
           brw_imm_v(0x10101010));

   /* Similarly, Y coordinates for subspans come from R1.2[31:16] through
    * R1.5[31:16], so to get pixel Y coordinates we need to start at the 5th
    * 16-bit value instead of the 4th (R1.5<2;4,0>UW instead of
    * R1.4<2;4,0>UW).
    *
    * And we need to add the repeating sequence (0, 0, 1, 1, ...), since
    * pixels n+2 and n+3 are in the bottom half of the subspan.
    */
   brw_ADD(&func, y_frag, stride(suboffset(R1, 5), 2, 4, 0),
           brw_imm_v(0x11001100));
}

void
brw_msaa_resolve_program::emit_coord_transform_stencil_swizzle()
{
   /* TODO: handle 8x */
   /* The windowizer has been configured to output to a single-sampled,
    * 8-bit, 4-color-component, Y-tiled surface.  For such a surface,
    * the interpretation of address bits is as follows (see Graphics
    * BSpec: vol1c Memory Interface and Command Stream [SNB+] > Graphics
    * Memory Interface Functions > Address Tiling Function > Tile
    * Formats):
    *
    *   Y-tiled single-sampled 4-color:
    *   ttttttttttttttttttttxxxyyyyyxxcc                        (1)
    *
    * (That is, the first 20 bits of the memory address select which
    * tile we are rendering to (offset appropriately by the surface
    * start address), followed by bits 4-2 of the x coordinate within
    * the tile, followed by the y coordinate within the tile, followed
    * by bits 1-0 of the x coordinate, followed by two bits describing
    * which color component is being addressed).  However, the surface
    * we are rendering to is actually a W-tiled, single-sampled stencil
    * buffer, so the correct interpretation of address bits is (see
    * Graphics BSpec: vol1c Memory Interface and Command Stream [SNB+] >
    * Graphics Memory Interface Functions > Address Tiling Function >
    * W-Major Tile Format [DevIL+]):
    *
    *   W-tiled single-sampled:
    *   ttttttttttttttttttttxxxyyyyxyxyx                        (2)
    *
    * The surface we are texturing from is also configured as a
    * single-sampled, 8-bit, 4-color-component, Y-tiled surface.  But it
    * is actually a W-tiled, 4x-multisampled stencil buffer, so the
    * correct interpretation of address bits is (see Graphics BSpec:
    * vol1a GPU Overview [All projects] > Memory Data Formats > Surface
    * Layout and Tiling [DevSKL+] > Stencil Buffer Layout):
    *
    *   W-tiled 4x-multisampled:
    *   ttttttttttttttttttttxxxyyyyxssyx                        (3)
    *
    * (Where "ss" represents the 2 bits of the sample number).
    *
    * Therefore, when the pixel shader program appears to be generating
    * a value for the pixel coordinate and color component index given
    * by:
    *
    *   x_frag = A << 5 | 0bBCDEF
    *   y_frag = G << 5 | 0bHIJKL                               (4)
    *   c_frag = 0bMN
    *
    * we can apply (1) to see that it is actually generating the byte
    * that will be stored at:
    *
    *   offset = (G * ss_stride + A) << 12 | 0bBCDHIJKLEFMN     (5)
    *
    * within the render target surface (where ss_stride is the number of
    * tiles that cover the width of the single-sampled render surface).
    * Rearranging these bits according to (2), this corresponds to a
    * pixel coordinate of:
    *
    *   x_stencil = A << 6 | 0bBCDLFN
    *   y_stencil = G << 6 | 0bHIJKEM                           (6)
    *
    * within the output stencil buffer.
    *
    * We wish to obtain input from these coordinates in the multisampled
    * stencil texture, so applying the (3), we wish to read from this
    * memory offset:
    *
    *   offset = ((G*2+H) * ms_stride + (A*2+B)) | 0bCDLIJKEF00MN (7)
    *
    * within the multisampled stencil texture (ms_stride is the number
    * of tiles that cover the width of the multisampled texture
    * surface).  Since we have this texture configured as a
    * single-sampled, 8-bit, 4-color-component, Y-tiled surface, we can
    * apply (1) to obtain the u, v, and color component index that we
    * need to sample from:
    *
    *   u_tex = (A*2 + B) << 5 | 0bCDL00 = A << 6 | 0bBCDL00
    *   v_tex = (G*2 + H) << 5 | 0bIJKEF = G << 6 | 0bHIJKEF    (8)
    *   c_tex = 0bMN
    *
    * Combining (4) and (8), we see that the shader needs to transform
    * fragment coordinates into texture coordinates as follows:
    *
    *   u_tex = (x_frag & ~0b11) << 1 | (y_frag &  0b1) << 2
    *   v_tex = (x_frag &  0b11)      | (y_frag & ~0b1) << 1    (9)
    */
   brw_AND(&func, u_tex, x_frag, brw_imm_uw(0xfffc)); /* x_frag & ~0b11 */
   brw_SHL(&func, u_tex, u_tex, brw_imm_uw(1)); /* (x_frag & ~0b11) << 1 */
   brw_AND(&func, v_tex, y_frag, brw_imm_uw(1)); /* y_frag & 0b1 */
   brw_SHL(&func, v_tex, v_tex, brw_imm_uw(2)); /* (y_frag & 0b1) << 2 */
   brw_OR(&func, u_tex, u_tex, v_tex); /* u_tex */
   brw_AND(&func, x_frag, x_frag, brw_imm_uw(3)); /* x_frag & 0b11 */
   brw_AND(&func, y_frag, y_frag, brw_imm_uw(0xfffe)); /* y_frag & ~0b1 */
   brw_SHL(&func, y_frag, y_frag, brw_imm_uw(1)); /* (y_frag & ~0b1) << 1 */
   brw_OR(&func, v_tex, x_frag, y_frag); /* v_tex */
}

void
brw_msaa_resolve_program::emit_coord_transform_depth_swizzle()
{
   /* TODO: handle 8x */
   /* TODO: explain what's going on here.
    *
    * u_tex = (x_frag & ~0b1) << 1 | (x_frag & 0b1)
    * v_tex = (y_frag & ~0b1) << 1 | (y_frag & 0b1)
    */
   brw_AND(&func, u_tex, x_frag, brw_imm_uw(0xfffe)); /* x_frag & ~0b1 */
   brw_SHL(&func, u_tex, u_tex, brw_imm_uw(1)); /* (x_frag & ~0b1) << 1 */
   brw_AND(&func, x_frag, x_frag, brw_imm_uw(1)); /* x_frag & 0b1 */
   brw_OR(&func, u_tex, u_tex, x_frag); /* u_tex */
   brw_AND(&func, v_tex, y_frag, brw_imm_uw(0xfffe)); /* y_frag & ~0b1 */
   brw_SHL(&func, v_tex, v_tex, brw_imm_uw(1)); /* (y_frag & ~0b1) << 1 */
   brw_AND(&func, y_frag, y_frag, brw_imm_uw(1)); /* y_frag & 0b1 */
   brw_OR(&func, v_tex, v_tex, y_frag); /* v_tex */
}

void
brw_msaa_resolve_program::emit_coord_transform_normal()
{
   /* TODO: handle 8X */
   /* TODO: explain what's going on here.
    *
    * u_tex = x_frag << 1 | 1
    * v_tex = y_frag << 1 | 1
    */
   brw_SHL(&func, u_tex, x_frag, brw_imm_w(1));
   brw_SHL(&func, v_tex, y_frag, brw_imm_w(1));
   brw_ADD(&func, u_tex, u_tex, brw_imm_w(1));
   brw_ADD(&func, v_tex, v_tex, brw_imm_w(1));
}

void
brw_msaa_resolve_program::emit_texture_lookup()
{
   struct brw_reg mrf_u, mrf_v;
   switch (key->sampler_msg_type) {
   case GEN5_SAMPLER_MESSAGE_SAMPLE:
      /* This sampler message expects floating point texture coordinates. */
      mrf_u = mrf_u_float;
      mrf_v = mrf_v_float;
      break;
   case GEN5_SAMPLER_MESSAGE_SAMPLE_LD:
      /* This sampler message expects integer texture coordinates. */
      mrf_u = retype(mrf_u_float, BRW_REGISTER_TYPE_UD);
      mrf_v = retype(mrf_v_float, BRW_REGISTER_TYPE_UD);
      break;
   }

   /* TODO: can we do some of this faster with a compressed instruction? */
   /* TODO: do we need to use 2NDHALF compression mode? */
   brw_MOV(&func, vec8(mrf_u), vec8(u_tex));
   brw_MOV(&func, offset(vec8(mrf_u), 1), suboffset(vec8(u_tex), 8));
   brw_MOV(&func, vec8(mrf_v), vec8(v_tex));
   brw_MOV(&func, offset(vec8(mrf_v), 1), suboffset(vec8(v_tex), 8));

   /* TODO: is this necessary? */
   /* TODO: what does this mean for LD mode? */
   brw_MOV(&func, mrf_r_float, brw_imm_f(0.5));

   brw_SAMPLE(&func,
              retype(Rdata, BRW_REGISTER_TYPE_UW) /* dest */,
              base_mrf /* msg_reg_nr */,
              vec8(mrf_u) /* src0 */,
              GEN6_HIZ_TEXTURE_BINDING_TABLE_INDEX,
              0 /* sampler -- ignored for SAMPLE_LD message */,
              WRITEMASK_XYZW,
              key->sampler_msg_type,
              8 /* response_length */,
              6 /* msg_length */,
              0 /* header_present */,
              BRW_SAMPLER_SIMD_MODE_SIMD16,
              BRW_SAMPLER_RETURN_FORMAT_FLOAT32);
}

void
brw_msaa_resolve_program::emit_render_target_write()
{
   /* Copy texture data to MRFs */
   struct brw_reg mrf_rt_write = vec16(brw_message_reg(base_mrf));
   for (int i = 0; i < 4; ++i) {
      /* E.g. mov(16) m2.0<1>:f r2.0<8;8,1>:f { Align1, H1 } */
      brw_MOV(&func, offset(mrf_rt_write, 2*i), offset(vec8(Rdata), 2*i));
   }

   /* Now write to the render target and terminate the thread */
   brw_fb_WRITE(&func,
                16 /* dispatch_width */,
                base_mrf /* msg_reg_nr */,
                mrf_rt_write /* src0 */,
                GEN6_HIZ_RENDERBUFFER_BINDING_TABLE_INDEX,
                8 /* msg_length */,
                0 /* response_length */,
                true /* eot */,
                false /* header_present */);
}

uint32_t
brw_hiz_resolve_params::get_wm_prog(struct brw_context *brw) const
{
   return 0;
}

uint32_t
brw_msaa_resolve_params::get_wm_prog(struct brw_context *brw) const
{
   uint32_t prog_offset;
   if (!brw_search_cache(&brw->cache, BRW_MSAA_WM_PROG,
                         &this->wm_prog_key, sizeof(this->wm_prog_key),
                         &prog_offset, NULL)) {
      brw_msaa_resolve_program prog(brw, &this->wm_prog_key);
      GLuint program_size;
      const GLuint *program = prog.compile(brw, &program_size);
      brw_upload_cache(&brw->cache, BRW_MSAA_WM_PROG,
                       &this->wm_prog_key, sizeof(this->wm_prog_key),
                       program, program_size,
                       NULL, 0,
                       &prog_offset, NULL);
   }
   return prog_offset;
}

void
gen6_hiz_emit_batch_head(struct brw_context *brw,
                         const brw_blorp_params *params)
{
   struct gl_context *ctx = &brw->intel.ctx;
   struct intel_context *intel = &brw->intel;

   /* To ensure that the batch contains only the resolve, flush the batch
    * before beginning and after finishing emitting the resolve packets.
    *
    * Ideally, we would not need to flush for the resolve op. But, I suspect
    * that it's unsafe for CMD_PIPELINE_SELECT to occur multiple times in
    * a single batch, and there is no safe way to ensure that other than by
    * fencing the resolve with flushes. Ideally, we would just detect if
    * a batch is in progress and do the right thing, but that would require
    * the ability to *safely* access brw_context::state::dirty::brw
    * outside of the brw_upload_state() codepath.
    */
   intel_flush(ctx);

   /* CMD_PIPELINE_SELECT
    *
    * Select the 3D pipeline, as opposed to the media pipeline.
    */
   {
      BEGIN_BATCH(1);
      OUT_BATCH(brw->CMD_PIPELINE_SELECT << 16);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_MULTISAMPLE */
   {
      int length = intel->gen == 7 ? 4 : 3;

      BEGIN_BATCH(length);
      OUT_BATCH(_3DSTATE_MULTISAMPLE << 16 | (length - 2));
      OUT_BATCH(MS_PIXEL_LOCATION_CENTER |
                MS_NUMSAMPLES_1);
      OUT_BATCH(0);
      if (length >= 4)
         OUT_BATCH(0);
      ADVANCE_BATCH();

   }

   /* 3DSTATE_SAMPLE_MASK */
   {
      BEGIN_BATCH(2);
      OUT_BATCH(_3DSTATE_SAMPLE_MASK << 16 | (2 - 2));
      OUT_BATCH(1);
      ADVANCE_BATCH();
   }

   /* CMD_STATE_BASE_ADDRESS
    *
    * From the Sandy Bridge PRM, Volume 1, Part 1, Table STATE_BASE_ADDRESS:
    *     The following commands must be reissued following any change to the
    *     base addresses:
    *         3DSTATE_CC_POINTERS
    *         3DSTATE_BINDING_TABLE_POINTERS
    *         3DSTATE_SAMPLER_STATE_POINTERS
    *         3DSTATE_VIEWPORT_STATE_POINTERS
    *         MEDIA_STATE_POINTERS
    */
   {
      BEGIN_BATCH(10);
      OUT_BATCH(CMD_STATE_BASE_ADDRESS << 16 | (10 - 2));
      OUT_BATCH(1); /* GeneralStateBaseAddressModifyEnable */
      /* SurfaceStateBaseAddress */
      OUT_RELOC(intel->batch.bo, I915_GEM_DOMAIN_SAMPLER, 0, 1);
      /* DynamicStateBaseAddress */
      OUT_RELOC(intel->batch.bo, (I915_GEM_DOMAIN_RENDER |
                                  I915_GEM_DOMAIN_INSTRUCTION), 0, 1);
      OUT_BATCH(1); /* IndirectObjectBaseAddress */
      if (params->use_wm_prog) {
         OUT_RELOC(brw->cache.bo, I915_GEM_DOMAIN_INSTRUCTION, 0,
                   1); /* Instruction base address: shader kernels */
      } else {
         OUT_BATCH(1); /* InstructionBaseAddress */
      }
      OUT_BATCH(1); /* GeneralStateUpperBound */
      OUT_BATCH(1); /* DynamicStateUpperBound */
      OUT_BATCH(1); /* IndirectObjectUpperBound*/
      OUT_BATCH(1); /* InstructionAccessUpperBound */
      ADVANCE_BATCH();
   }
}

void
gen6_hiz_emit_vertices(struct brw_context *brw,
                       const brw_blorp_params *params)
{
   struct intel_context *intel = &brw->intel;
   uint32_t vertex_offset;

   /* Setup VBO for the rectangle primitive..
    *
    * A rectangle primitive (3DPRIM_RECTLIST) consists of only three
    * vertices. The vertices reside in screen space with DirectX coordinates
    * (that is, (0, 0) is the upper left corner).
    *
    *   v2 ------ implied
    *    |        |
    *    |        |
    *   v0 ----- v1
    *
    * Since the VS is disabled, the clipper loads each VUE directly from
    * the URB. This is controlled by the 3DSTATE_VERTEX_BUFFERS and
    * 3DSTATE_VERTEX_ELEMENTS packets below. The VUE contents are as follows:
    *   dw0: Reserved, MBZ.
    *   dw1: Render Target Array Index. The HiZ op does not use indexed
    *        vertices, so set the dword to 0.
    *   dw2: Viewport Index. The HiZ op disables viewport mapping and
    *        scissoring, so set the dword to 0.
    *   dw3: Point Width: The HiZ op does not emit the POINTLIST primitive, so
    *        set the dword to 0.
    *   dw4: Vertex Position X.
    *   dw5: Vertex Position Y.
    *   dw6: Vertex Position Z.
    *   dw7: Vertex Position W.
    *
    * For details, see the Sandybridge PRM, Volume 2, Part 1, Section 1.5.1
    * "Vertex URB Entry (VUE) Formats".
    */
   {
      const int width = params->width;
      const int height = params->height;
      float *vertex_data;

      const float vertices[GEN6_HIZ_VBO_SIZE] = {
         /* v0 */ 0, 0, 0, 0,         0, height, 0, 1,
         /* v1 */ 0, 0, 0, 0,     width, height, 0, 1,
         /* v2 */ 0, 0, 0, 0,         0,      0, 0, 1,
      };

      vertex_data = (float *) brw_state_batch(brw, AUB_TRACE_NO_TYPE,
                                              GEN6_HIZ_VBO_SIZE, 32,
                                              &vertex_offset);
      memcpy(vertex_data, vertices, GEN6_HIZ_VBO_SIZE);
   }

   /* 3DSTATE_VERTEX_BUFFERS */
   {
      const int num_buffers = 1;
      const int batch_length = 1 + 4 * num_buffers;

      uint32_t dw0 = GEN6_VB0_ACCESS_VERTEXDATA |
                     (GEN6_HIZ_NUM_VUE_ELEMS * sizeof(float)) << BRW_VB0_PITCH_SHIFT;

      if (intel->gen >= 7)
         dw0 |= GEN7_VB0_ADDRESS_MODIFYENABLE;

      BEGIN_BATCH(batch_length);
      OUT_BATCH((_3DSTATE_VERTEX_BUFFERS << 16) | (batch_length - 2));
      OUT_BATCH(dw0);
      /* start address */
      OUT_RELOC(intel->batch.bo, I915_GEM_DOMAIN_VERTEX, 0,
		vertex_offset);
      /* end address */
      OUT_RELOC(intel->batch.bo, I915_GEM_DOMAIN_VERTEX, 0,
		vertex_offset + GEN6_HIZ_VBO_SIZE - 1);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_VERTEX_ELEMENTS
    *
    * Fetch dwords 0 - 7 from each VUE. See the comments above where
    * hiz->vertex_bo is filled with data.
    */
   {
      const int num_elements = 2;
      const int batch_length = 1 + 2 * num_elements;

      BEGIN_BATCH(batch_length);
      OUT_BATCH((_3DSTATE_VERTEX_ELEMENTS << 16) | (batch_length - 2));
      /* Element 0 */
      OUT_BATCH(GEN6_VE0_VALID |
                BRW_SURFACEFORMAT_R32G32B32A32_FLOAT << BRW_VE0_FORMAT_SHIFT |
                0 << BRW_VE0_SRC_OFFSET_SHIFT);
      OUT_BATCH(BRW_VE1_COMPONENT_STORE_SRC << BRW_VE1_COMPONENT_0_SHIFT |
                BRW_VE1_COMPONENT_STORE_SRC << BRW_VE1_COMPONENT_1_SHIFT |
                BRW_VE1_COMPONENT_STORE_SRC << BRW_VE1_COMPONENT_2_SHIFT |
                BRW_VE1_COMPONENT_STORE_SRC << BRW_VE1_COMPONENT_3_SHIFT);
      /* Element 1 */
      OUT_BATCH(GEN6_VE0_VALID |
                BRW_SURFACEFORMAT_R32G32B32A32_FLOAT << BRW_VE0_FORMAT_SHIFT |
                16 << BRW_VE0_SRC_OFFSET_SHIFT);
      OUT_BATCH(BRW_VE1_COMPONENT_STORE_SRC << BRW_VE1_COMPONENT_0_SHIFT |
                BRW_VE1_COMPONENT_STORE_SRC << BRW_VE1_COMPONENT_1_SHIFT |
                BRW_VE1_COMPONENT_STORE_SRC << BRW_VE1_COMPONENT_2_SHIFT |
                BRW_VE1_COMPONENT_STORE_SRC << BRW_VE1_COMPONENT_3_SHIFT);
      ADVANCE_BATCH();
   }
}

/**
 * Disable thread dispatch (dw5.19) and enable the HiZ op.
 */
static void
gen6_hiz_disable_wm(struct brw_context *brw,
                    const brw_blorp_params *params)
{
   struct intel_context *intel = &brw->intel;

   /* Even though thread dispatch is disabled, max threads (dw5.25:31) must be
    * nonzero to prevent the GPU from hanging. See the valid ranges in the
    * BSpec, Volume 2a.11 Windower, Section 3DSTATE_WM, Dword 5.25:31
    * "Maximum Number Of Threads".
    */
   uint32_t dw4 = 0;

   switch (params->op) {
   case GEN6_HIZ_OP_DEPTH_CLEAR:
      assert(!"not implemented");
      dw4 |= GEN6_WM_DEPTH_CLEAR;
      break;
   case GEN6_HIZ_OP_DEPTH_RESOLVE:
      dw4 |= GEN6_WM_DEPTH_RESOLVE;
      break;
   case GEN6_HIZ_OP_HIZ_RESOLVE:
      dw4 |= GEN6_WM_HIERARCHICAL_DEPTH_RESOLVE;
      break;
   default:
      assert(0);
      break;
   }

   BEGIN_BATCH(9);
   OUT_BATCH(_3DSTATE_WM << 16 | (9 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(dw4);
   OUT_BATCH((brw->max_wm_threads - 1) << GEN6_WM_MAX_THREADS_SHIFT);
   OUT_BATCH((1 - 1) << GEN6_WM_NUM_SF_OUTPUTS_SHIFT); /* only position */
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen6_hiz_enable_wm(struct brw_context *brw, uint32_t prog_offset)
{
   struct intel_context *intel = &brw->intel;
   uint32_t dw2, dw4, dw5, dw6;

   /* Disable the push constant buffers. */
   BEGIN_BATCH(5);
   OUT_BATCH(_3DSTATE_CONSTANT_PS << 16 | (5 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();

   dw2 = dw4 = dw5 = dw6 = 0;
   dw4 |= GEN6_WM_STATISTICS_ENABLE;
   dw5 |= GEN6_WM_LINE_AA_WIDTH_1_0;
   dw5 |= GEN6_WM_LINE_END_CAP_AA_WIDTH_0_5;
   dw2 |= 0 << GEN6_WM_SAMPLER_COUNT_SHIFT; /* No samplers */
   dw4 |= 0 << GEN6_WM_DISPATCH_START_GRF_SHIFT_0; /* No constants */
   dw4 |= 0 << GEN6_WM_DISPATCH_START_GRF_SHIFT_2; /* No constants */
   dw5 |= (brw->max_wm_threads - 1) << GEN6_WM_MAX_THREADS_SHIFT;
   dw5 |= GEN6_WM_16_DISPATCH_ENABLE;
   dw6 |= 0 << GEN6_WM_BARYCENTRIC_INTERPOLATION_MODE_SHIFT; /* No interp */
   dw5 |= GEN6_WM_DISPATCH_ENABLE; /* We are rendering */
   dw6 |= 0 << GEN6_WM_NUM_SF_OUTPUTS_SHIFT; /* No inputs from SF */
   dw6 |= GEN6_WM_MSRAST_OFF_PIXEL; /* Render target is not multisampled */
   dw6 |= GEN6_WM_MSDISPMODE_PERSAMPLE; /* Render target is not multisampled */

   BEGIN_BATCH(9);
   OUT_BATCH(_3DSTATE_WM << 16 | (9 - 2));
   OUT_BATCH(prog_offset);
   OUT_BATCH(dw2);
   OUT_BATCH(0); /* No scratch needed */
   OUT_BATCH(dw4);
   OUT_BATCH(dw5);
   OUT_BATCH(dw6);
   OUT_BATCH(0); /* No other programs */
   OUT_BATCH(0); /* No other programs */
   ADVANCE_BATCH();
}

/**
 * \brief Execute a HiZ op on a miptree slice.
 *
 * To execute the HiZ op, this function manually constructs and emits a batch
 * to "draw" the HiZ op's rectangle primitive. The batchbuffer is flushed
 * before constructing and after emitting the batch.
 *
 * This function alters no GL state.
 *
 * For an overview of HiZ ops, see the following sections of the Sandy Bridge
 * PRM, Volume 1, Part 2:
 *   - 7.5.3.1 Depth Buffer Clear
 *   - 7.5.3.2 Depth Buffer Resolve
 *   - 7.5.3.3 Hierarchical Depth Buffer Resolve
 */
static void
gen6_hiz_exec(struct intel_context *intel,
              const brw_blorp_params *params)
{
   struct gl_context *ctx = &intel->ctx;
   struct brw_context *brw = brw_context(ctx);
   uint32_t draw_x = 0, draw_y = 0;
   uint32_t tile_mask_x, tile_mask_y;

   if (params->depth.mt) {
      params->depth.get_draw_offsets(&draw_x, &draw_y);

      /* Compute masks to determine how much of draw_x and draw_y should be
       * performed using the fine adjustment of "depth coordinate offset X/Y"
       * (dw5 of 3DSTATE_DEPTH_BUFFER).  See the emit_depthbuffer() function
       * for details.
       */
      {
         uint32_t depth_mask_x, depth_mask_y, hiz_mask_x, hiz_mask_y;
         intel_region_get_tile_masks(params->depth.mt->region,
                                     &depth_mask_x, &depth_mask_y);
         intel_region_get_tile_masks(params->hiz_mt->region,
                                     &hiz_mask_x, &hiz_mask_y);

         /* Each HiZ row represents 2 rows of pixels */
         hiz_mask_y = hiz_mask_y << 1 | 1;

         tile_mask_x = depth_mask_x | hiz_mask_x;
         tile_mask_y = depth_mask_y | hiz_mask_y;
      }
   }

   /* TODO: is it ok to do this before gen6_hiz_emit_batch_head or will it
    * screw up the program cache?
    */
   uint32_t prog_offset = params->get_wm_prog(brw);

   gen6_hiz_emit_batch_head(brw, params);
   gen6_hiz_emit_vertices(brw, params);

   /* 3DSTATE_URB
    *
    * Assign the entire URB to the VS. Even though the VS disabled, URB space
    * is still needed because the clipper loads the VUE's from the URB. From
    * the Sandybridge PRM, Volume 2, Part 1, Section 3DSTATE,
    * Dword 1.15:0 "VS Number of URB Entries":
    *     This field is always used (even if VS Function Enable is DISABLED).
    *
    * The warning below appears in the PRM (Section 3DSTATE_URB), but we can
    * safely ignore it because this batch contains only one draw call.
    *     Because of URB corruption caused by allocating a previous GS unit
    *     URB entry to the VS unit, software is required to send a “GS NULL
    *     Fence” (Send URB fence with VS URB size == 1 and GS URB size == 0)
    *     plus a dummy DRAW call before any case where VS will be taking over
    *     GS URB space.
    */
   {
      BEGIN_BATCH(3);
      OUT_BATCH(_3DSTATE_URB << 16 | (3 - 2));
      OUT_BATCH(brw->urb.max_vs_entries << GEN6_URB_VS_ENTRIES_SHIFT);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* BLEND_STATE */
   uint32_t cc_blend_state_offset = 0;
   if (params->use_wm_prog)
   {
      struct gen6_blend_state *blend = (struct gen6_blend_state *)
         brw_state_batch(brw, AUB_TRACE_BLEND_STATE,
                         sizeof(struct gen6_blend_state), 64,
                         &cc_blend_state_offset);

      memset(blend, 0, sizeof(*blend));

      // TODO: handle other formats.
      blend->blend1.pre_blend_clamp_enable = 1;
      blend->blend1.post_blend_clamp_enable = 1;
      blend->blend1.clamp_range = BRW_RENDERTARGET_CLAMPRANGE_FORMAT;

      blend->blend1.write_disable_r = false;
      blend->blend1.write_disable_g = false;
      blend->blend1.write_disable_b = false;
      blend->blend1.write_disable_a = false;
   }

   /* TODO: gen6_color_calc_state */
   uint32_t cc_state_offset = 0;
   if (params->use_wm_prog)
   {
      struct gen6_color_calc_state *cc = (struct gen6_color_calc_state *)
         brw_state_batch(brw, AUB_TRACE_CC_STATE,
                         sizeof(gen6_color_calc_state), 64,
                         &cc_state_offset);
      memset(cc, 0, sizeof(*cc));
   }

   /* 3DSTATE_CC_STATE_POINTERS
    *
    * The pointer offsets are relative to
    * CMD_STATE_BASE_ADDRESS.DynamicStateBaseAddress.
    *
    * The HiZ op doesn't use BLEND_STATE or COLOR_CALC_STATE.
    * But MSAA does.
    */
   {
      uint32_t depthstencil_offset;
      gen6_hiz_emit_depth_stencil_state(brw, params, &depthstencil_offset);

      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_CC_STATE_POINTERS << 16 | (4 - 2));
      OUT_BATCH(cc_blend_state_offset | 1); /* BLEND_STATE offset */
      OUT_BATCH(depthstencil_offset | 1); /* DEPTH_STENCIL_STATE offset */
      OUT_BATCH(cc_state_offset | 1); /* COLOR_CALC_STATE offset */
      ADVANCE_BATCH();
   }

   /* SURFACE_STATE for renderbuffer surface (see
    * brw_update_renderbuffer_surface)
    */
   uint32_t wm_surf_offset_renderbuffer = 0;
   if (params->dst.mt)
   {
      uint32_t width, height;
      params->dst.get_miplevel_dims(&width, &height);
      if (params->stencil_magic) {
         width /= 2;
         height /= 2;
      }
      struct intel_region *region = params->dst.mt->region;
      uint32_t *surf = (uint32_t *)
         brw_state_batch(brw, AUB_TRACE_SURFACE_STATE, 6 * 4, 32,
                         &wm_surf_offset_renderbuffer);
      /* TODO: handle other formats */
      uint32_t format = BRW_SURFACEFORMAT_B8G8R8A8_UNORM;

      surf[0] = (BRW_SURFACE_2D << BRW_SURFACE_TYPE_SHIFT |
                 format << BRW_SURFACE_FORMAT_SHIFT);

      /* reloc */
      surf[1] = region->bo->offset; /* No tile offsets needed */

      surf[2] = ((width - 1) << BRW_SURFACE_WIDTH_SHIFT |
                 (height - 1) << BRW_SURFACE_HEIGHT_SHIFT);

      /* Note: pitch needs to be multiplied by adj_factor because we're
       * grouping 4 pixels together into 1
       */
      uint32_t tiling = params->stencil_magic
         ? BRW_SURFACE_TILED | BRW_SURFACE_TILED_Y
         : brw_get_surface_tiling_bits(region->tiling);
      uint32_t pitch_bytes = region->pitch * region->cpp;
      if (params->stencil_magic)
         pitch_bytes *= 2;
      surf[3] = (tiling | (pitch_bytes - 1) << BRW_SURFACE_PITCH_SHIFT);

      surf[4] = BRW_SURFACE_MULTISAMPLECOUNT_1;

      surf[5] = (0 << BRW_SURFACE_X_OFFSET_SHIFT |
                 0 << BRW_SURFACE_Y_OFFSET_SHIFT |
                 (params->dst.mt->align_h == 4 ? BRW_SURFACE_VERTICAL_ALIGN_ENABLE : 0));

      drm_intel_bo_emit_reloc(brw->intel.batch.bo,
                              wm_surf_offset_renderbuffer + 4,
                              region->bo,
                              surf[1] - region->bo->offset,
                              I915_GEM_DOMAIN_RENDER,
                              I915_GEM_DOMAIN_RENDER);
   }

   /* SURFACE_STATE for texture surface (see brw_update_texture_surface) */
   uint32_t wm_surf_offset_texture = 0;
   if (params->src.mt)
   {
      /* Note: don't use adj_factor because when downsampling a stencil
       * texture we map the texture in non-multisampled mode.
       */
      uint32_t width, height;
      params->src.get_miplevel_dims(&width, &height);
      if (params->stencil_magic) {
         width /= 2;
         height /= 2;
      }
      struct intel_region *region = params->src.mt->region;

      /* TODO: handle other formats */
      uint32_t format = BRW_SURFACEFORMAT_B8G8R8A8_UNORM;

      uint32_t *surf = (uint32_t *)
         brw_state_batch(brw, AUB_TRACE_SURFACE_STATE, 6 * 4, 32,
                         &wm_surf_offset_texture);

      surf[0] = (BRW_SURFACE_2D << BRW_SURFACE_TYPE_SHIFT |
                 BRW_SURFACE_MIPMAPLAYOUT_BELOW << BRW_SURFACE_MIPLAYOUT_SHIFT |
                 BRW_SURFACE_CUBEFACE_ENABLES |
                 (format << BRW_SURFACE_FORMAT_SHIFT));

      surf[1] = region->bo->offset; /* reloc */

      surf[2] = (0 << BRW_SURFACE_LOD_SHIFT |
                 (width - 1) << BRW_SURFACE_WIDTH_SHIFT |
                 (height - 1) << BRW_SURFACE_HEIGHT_SHIFT);

      /* We still have to adjust the pitch though */
      uint32_t tiling = params->stencil_magic
         ? BRW_SURFACE_TILED | BRW_SURFACE_TILED_Y
         : brw_get_surface_tiling_bits(region->tiling);
      uint32_t pitch_bytes = region->pitch * region->cpp;
      if (params->stencil_magic)
         pitch_bytes *= 2;
      surf[3] = (tiling |
                 0 << BRW_SURFACE_DEPTH_SHIFT |
                 (pitch_bytes - 1) <<
                 BRW_SURFACE_PITCH_SHIFT);

      surf[4] = params->src_multisampled ? BRW_SURFACE_MULTISAMPLECOUNT_1 : BRW_SURFACE_MULTISAMPLECOUNT_4;

      surf[5] = (params->src.mt->align_h == 4) ? BRW_SURFACE_VERTICAL_ALIGN_ENABLE : 0;

      /* Emit relocation to surface contents */
      drm_intel_bo_emit_reloc(brw->intel.batch.bo,
                              wm_surf_offset_texture + 4,
                              region->bo, 0,
                              I915_GEM_DOMAIN_SAMPLER, 0);
   }

   /* brw_wm_binding_table */
   uint32_t wm_bind_bo_offset;
   if (params->use_wm_prog) {
      uint32_t *bind = (uint32_t *)
         brw_state_batch(brw, AUB_TRACE_BINDING_TABLE,
                         sizeof(uint32_t) * GEN6_HIZ_NUM_BINDING_TABLE_ENTRIES,
                         32, /* alignment */
                         &wm_bind_bo_offset);
      bind[GEN6_HIZ_RENDERBUFFER_BINDING_TABLE_INDEX] = wm_surf_offset_renderbuffer;
      bind[GEN6_HIZ_TEXTURE_BINDING_TABLE_INDEX] = wm_surf_offset_texture;
   }

   /* SAMPLER_STATE */
   uint32_t sampler_offset = 0;
   if (params->src.mt)
   {
      struct brw_sampler_state *sampler = (struct brw_sampler_state *)
         brw_state_batch(brw, AUB_TRACE_SAMPLER_STATE,
                         sizeof(struct brw_sampler_state),
                         32, &sampler_offset);
      memset(sampler, 0, sizeof(*sampler));

      sampler->ss0.min_filter = BRW_MAPFILTER_LINEAR;
      sampler->ss0.mip_filter = BRW_MIPFILTER_NONE;
      sampler->ss0.mag_filter = BRW_MAPFILTER_LINEAR;

      sampler->ss1.r_wrap_mode = BRW_TEXCOORDMODE_CLAMP;
      sampler->ss1.s_wrap_mode = BRW_TEXCOORDMODE_CLAMP;
      sampler->ss1.t_wrap_mode = BRW_TEXCOORDMODE_CLAMP;

      sampler->ss0.min_mag_neq = 1;

      /* Set LOD bias: 
       */
      sampler->ss0.lod_bias = 0;

      sampler->ss0.lod_preclamp = 1; /* OpenGL mode */
      sampler->ss0.default_color_mode = 0; /* OpenGL/DX10 mode */

      /* Set BaseMipLevel, MaxLOD, MinLOD: 
       *
       * XXX: I don't think that using firstLevel, lastLevel works,
       * because we always setup the surface state as if firstLevel ==
       * level zero.  Probably have to subtract firstLevel from each of
       * these:
       */
      sampler->ss0.base_level = U_FIXED(0, 1);

      sampler->ss1.max_lod = U_FIXED(0, 6);
      sampler->ss1.min_lod = U_FIXED(0, 6);

      sampler->ss3.non_normalized_coord = 1;

      sampler->ss3.address_round |= BRW_ADDRESS_ROUNDING_ENABLE_U_MIN |
         BRW_ADDRESS_ROUNDING_ENABLE_V_MIN |
         BRW_ADDRESS_ROUNDING_ENABLE_R_MIN;
      sampler->ss3.address_round |= BRW_ADDRESS_ROUNDING_ENABLE_U_MAG |
         BRW_ADDRESS_ROUNDING_ENABLE_V_MAG |
         BRW_ADDRESS_ROUNDING_ENABLE_R_MAG;
   }

   /* 3DSTATE_VS
    *
    * Disable vertex shader.
    */
   {
      /* From the BSpec, Volume 2a, Part 3 "Vertex Shader", Section
       * 3DSTATE_VS, Dword 5.0 "VS Function Enable":
       *   [DevSNB] A pipeline flush must be programmed prior to a 3DSTATE_VS
       *   command that causes the VS Function Enable to toggle. Pipeline
       *   flush can be executed by sending a PIPE_CONTROL command with CS
       *   stall bit set and a post sync operation.
       */
      intel_emit_post_sync_nonzero_flush(intel);

      BEGIN_BATCH(6);
      OUT_BATCH(_3DSTATE_VS << 16 | (6 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_GS
    *
    * Disable the geometry shader.
    */
   {
      BEGIN_BATCH(7);
      OUT_BATCH(_3DSTATE_GS << 16 | (7 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_CLIP
    *
    * Disable the clipper.
    *
    * The HiZ op emits a rectangle primitive, which requires clipping to
    * be disabled. From page 10 of the Sandy Bridge PRM Volume 2 Part 1
    * Section 1.3 "3D Primitives Overview":
    *    RECTLIST:
    *    Either the CLIP unit should be DISABLED, or the CLIP unit's Clip
    *    Mode should be set to a value other than CLIPMODE_NORMAL.
    *
    * Also disable perspective divide. This doesn't change the clipper's
    * output, but does spare a few electrons.
    */
   {
      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_CLIP << 16 | (4 - 2));
      OUT_BATCH(0);
      OUT_BATCH(GEN6_CLIP_PERSPECTIVE_DIVIDE_DISABLE);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_SF
    *
    * Disable ViewportTransformEnable (dw2.1)
    *
    * From the SandyBridge PRM, Volume 2, Part 1, Section 1.3, "3D
    * Primitives Overview":
    *     RECTLIST: Viewport Mapping must be DISABLED (as is typical with the
    *     use of screen- space coordinates).
    *
    * A solid rectangle must be rendered, so set FrontFaceFillMode (dw2.4:3)
    * and BackFaceFillMode (dw2.5:6) to SOLID(0).
    *
    * From the Sandy Bridge PRM, Volume 2, Part 1, Section
    * 6.4.1.1 3DSTATE_SF, Field FrontFaceFillMode:
    *     SOLID: Any triangle or rectangle object found to be front-facing
    *     is rendered as a solid object. This setting is required when
    *     (rendering rectangle (RECTLIST) objects.
    */
   {
      BEGIN_BATCH(20);
      OUT_BATCH(_3DSTATE_SF << 16 | (20 - 2));
      OUT_BATCH((1 - 1) << GEN6_SF_NUM_OUTPUTS_SHIFT | /* only position */
                1 << GEN6_SF_URB_ENTRY_READ_LENGTH_SHIFT |
                0 << GEN6_SF_URB_ENTRY_READ_OFFSET_SHIFT);
      for (int i = 0; i < 18; ++i)
         OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_WM */
   if (params->use_wm_prog)
      gen6_hiz_enable_wm(brw, prog_offset);
   else
      gen6_hiz_disable_wm(brw, params);

   /* 3DSTATE_BINDING_TABLE_POINTERS */
   if (params->use_wm_prog) {
      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_BINDING_TABLE_POINTERS << 16 |
                GEN6_BINDING_TABLE_MODIFY_PS |
                (4 - 2));
      OUT_BATCH(0); /* vs -- ignored */
      OUT_BATCH(0); /* gs -- ignored */
      OUT_BATCH(wm_bind_bo_offset); /* wm/ps */
      ADVANCE_BATCH();
   }

   /* 3DSTATE_DEPTH_BUFFER */
   if (params->depth.mt) {
      uint32_t width, height;
      params->depth.get_miplevel_dims(&width, &height);

      uint32_t tile_x = draw_x & tile_mask_x;
      uint32_t tile_y = draw_y & tile_mask_y;
      uint32_t offset =
         intel_region_get_aligned_offset(params->depth.mt->region,
                                         draw_x & ~tile_mask_x,
                                         draw_y & ~tile_mask_y);

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
      tile_x &= ~7;
      tile_y &= ~7;

      uint32_t format;
      switch (params->depth.mt->format) {
      case MESA_FORMAT_Z16:       format = BRW_DEPTHFORMAT_D16_UNORM; break;
      case MESA_FORMAT_Z32_FLOAT: format = BRW_DEPTHFORMAT_D32_FLOAT; break;
      case MESA_FORMAT_X8_Z24:    format = BRW_DEPTHFORMAT_D24_UNORM_X8_UINT; break;
      default:                    assert(0); break;
      }

      intel_emit_post_sync_nonzero_flush(intel);
      intel_emit_depth_stall_flushes(intel);

      BEGIN_BATCH(7);
      OUT_BATCH(_3DSTATE_DEPTH_BUFFER << 16 | (7 - 2));
      uint32_t pitch_bytes =
         params->depth.mt->region->pitch * params->depth.mt->region->cpp;
      OUT_BATCH((pitch_bytes - 1) |
                format << 18 |
                1 << 21 | /* separate stencil enable */
                1 << 22 | /* hiz enable */
                BRW_TILEWALK_YMAJOR << 26 |
                1 << 27 | /* y-tiled */
                BRW_SURFACE_2D << 29);
      OUT_RELOC(params->depth.mt->region->bo,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                offset);
      OUT_BATCH(BRW_SURFACE_MIPMAPLAYOUT_BELOW << 1 |
                (width + tile_x - 1) << 6 |
                (height + tile_y - 1) << 19);
      OUT_BATCH(0);
      OUT_BATCH(tile_x |
                tile_y << 16);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(7);
      OUT_BATCH(_3DSTATE_DEPTH_BUFFER << 16 | (7 - 2));
      OUT_BATCH((BRW_DEPTHFORMAT_D32_FLOAT << 18) |
                (BRW_SURFACE_NULL << 29));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_HIER_DEPTH_BUFFER */
   if (params->depth.mt) {
      struct intel_region *hiz_region = params->hiz_mt->region;
      uint32_t hiz_offset =
         intel_region_get_aligned_offset(hiz_region,
                                         draw_x & ~tile_mask_x,
                                         (draw_y & ~tile_mask_y) / 2);

      BEGIN_BATCH(3);
      OUT_BATCH((_3DSTATE_HIER_DEPTH_BUFFER << 16) | (3 - 2));
      uint32_t pitch_bytes =
         params->hiz_mt->region->pitch *
         params->hiz_mt->region->cpp;
      OUT_BATCH(pitch_bytes - 1);
      OUT_RELOC(params->hiz_mt->region->bo,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                hiz_offset);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_STENCIL_BUFFER */
   if (params->depth.mt) {
      BEGIN_BATCH(3);
      OUT_BATCH((_3DSTATE_STENCIL_BUFFER << 16) | (3 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_CLEAR_PARAMS
    *
    * From the Sandybridge PRM, Volume 2, Part 1, Section 3DSTATE_CLEAR_PARAMS:
    *   [DevSNB] 3DSTATE_CLEAR_PARAMS packet must follow the DEPTH_BUFFER_STATE
    *   packet when HiZ is enabled and the DEPTH_BUFFER_STATE changes.
    */
   {
      BEGIN_BATCH(2);
      OUT_BATCH(_3DSTATE_CLEAR_PARAMS << 16 | (2 - 2));
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DSTATE_DRAWING_RECTANGLE */
   {
      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_DRAWING_RECTANGLE << 16 | (4 - 2));
      OUT_BATCH(0);
      OUT_BATCH(((params->width - 1) & 0xffff) |
                ((params->height - 1) << 16));
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   /* 3DPRIMITIVE */
   {
     BEGIN_BATCH(6);
     OUT_BATCH(CMD_3D_PRIM << 16 | (6 - 2) |
               _3DPRIM_RECTLIST << GEN4_3DPRIM_TOPOLOGY_TYPE_SHIFT |
               GEN4_3DPRIM_VERTEXBUFFER_ACCESS_SEQUENTIAL);
     OUT_BATCH(3); /* vertex count per instance */
     OUT_BATCH(0);
     OUT_BATCH(1); /* instance count */
     OUT_BATCH(0);
     OUT_BATCH(0);
     ADVANCE_BATCH();
   }

   /* See comments above at first invocation of intel_flush() in
    * gen6_hiz_emit_batch_head().
    */
   intel_flush(ctx);

   /* Be safe. */
   brw->state.dirty.brw = ~0;
   brw->state.dirty.cache = ~0;
}

/**
 * \param out_offset is relative to
 *        CMD_STATE_BASE_ADDRESS.DynamicStateBaseAddress.
 */
void
gen6_hiz_emit_depth_stencil_state(struct brw_context *brw,
                                  const brw_blorp_params *params,
                                  uint32_t *out_offset)
{
   struct gen6_depth_stencil_state *state;
   state = (struct gen6_depth_stencil_state *)
      brw_state_batch(brw, AUB_TRACE_DEPTH_STENCIL_STATE,
                      sizeof(*state), 64,
                      out_offset);
   memset(state, 0, sizeof(*state));

   /* See the following sections of the Sandy Bridge PRM, Volume 1, Part2:
    *   - 7.5.3.1 Depth Buffer Clear
    *   - 7.5.3.2 Depth Buffer Resolve
    *   - 7.5.3.3 Hierarchical Depth Buffer Resolve
    */
   state->ds2.depth_write_enable = 1;
   if (params->op == GEN6_HIZ_OP_DEPTH_RESOLVE) {
      state->ds2.depth_test_enable = 1;
      state->ds2.depth_test_func = COMPAREFUNC_NEVER;
   }
}

/** \see intel_context::vtbl::resolve_hiz_slice */
void
gen6_resolve_hiz_slice(struct intel_context *intel,
                       struct intel_mipmap_tree *mt,
                       uint32_t level,
                       uint32_t layer)
{
   brw_hiz_resolve_params params(mt, mt->hiz_mt, level, layer, GEN6_HIZ_OP_HIZ_RESOLVE);
   gen6_hiz_exec(intel, &params);
}

/** \see intel_context::vtbl::resolve_depth_slice */
void
gen6_resolve_depth_slice(struct intel_context *intel,
                         struct intel_mipmap_tree *mt,
                         uint32_t level,
                         uint32_t layer)
{
   brw_hiz_resolve_params params(mt, mt->hiz_mt, level, layer, GEN6_HIZ_OP_DEPTH_RESOLVE);
   gen6_hiz_exec(intel, &params);
}
