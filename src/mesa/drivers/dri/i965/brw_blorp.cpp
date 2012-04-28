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

#include "main/teximage.h"

#include "glsl/ralloc.h"

#include "intel_fbo.h"

#include "brw_blorp.h"
#include "brw_context.h"
#include "brw_eu.h"
#include "brw_state.h"

static bool
try_blorp_blit_color(struct intel_context *intel,
                     GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                     GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                     GLenum filter)
{
   struct gl_context *ctx = &intel->ctx;

   /* Validate source */
   const struct gl_framebuffer *read_fb = ctx->ReadBuffer;
   struct intel_renderbuffer *src_irb =
      intel_renderbuffer(read_fb->_ColorReadBuffer);
   if (!src_irb) return false;
   struct intel_mipmap_tree *src_mt = src_irb->mt;
   if (!src_mt) return false; /* TODO: or is this guaranteed non-NULL? */
   if (!src_mt->downsampled_mt) return false; /* TODO: eliminate this restriction */

   /* Validate destination */
   const struct gl_framebuffer *draw_fb = ctx->DrawBuffer;
   struct intel_renderbuffer *dst_irb =
      intel_renderbuffer(draw_fb->Attachment[draw_fb->_ColorDrawBufferIndexes[0]].Renderbuffer);
   if (!dst_irb) return false;
   struct intel_mipmap_tree *dst_mt = dst_irb->mt;
   if (!dst_mt) return false; /* TODO: or is this guaranteed non-NULL? */
   if (dst_mt->downsampled_mt) return false; /* TODO: eliminate this restriction */

   /* Make sure width and height match, and there is no mirroring.
    * TODO: allow mirroring.
    */
   if (srcX1 < srcX0) return false;
   if (srcY1 < srcY0) return false;
   GLsizei width = srcX1 - srcX0;
   GLsizei height = srcY1 - srcY0;
   if (width != dstX1 - dstX0) return false;
   if (height != dstY1 - dstY0) return false;

   /* Make sure width and height don't need to be clipped or scissored.
    * TODO: support clipping and scissoring.
    */
   if (srcX0 < 0 || (GLuint) srcX1 > read_fb->Width) return false;
   if (srcY0 < 0 || (GLuint) srcY1 > read_fb->Height) return false;
   if (dstX0 < 0 || (GLuint) dstX1 > draw_fb->Width) return false;
   if (dstY0 < 0 || (GLuint) dstY1 > draw_fb->Height) return false;
   if (ctx->Scissor.Enabled) return false;

   /* Do the blit */
   intel_prepare_render(intel);
   brw_blorp_blit_params params(src_mt, dst_mt,
                                srcX0, srcY0, dstX0, dstY0, width, height);
   params.exec(intel);
   return true;
}

GLbitfield
brw_blorp_framebuffer(struct intel_context *intel,
                      GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                      GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                      GLbitfield mask, GLenum filter)
{
   /* BLORP is only supported on GEN6 and above */
   if (intel->gen < 6)
      return mask;

   if ((mask & GL_COLOR_BUFFER_BIT) &&
       try_blorp_blit_color(intel,
                            srcX0, srcY0, srcX1, srcY1,
                            dstX0, dstY0, dstX1, dstY1,
                            filter)) {
      mask &= ~GL_COLOR_BUFFER_BIT;
   }

   return mask;
}

class brw_blorp_blit_program
{
public:
   brw_blorp_blit_program(struct brw_context *brw,
                          const brw_blorp_blit_prog_key *key);
   ~brw_blorp_blit_program();

   const GLuint *compile(struct brw_context *brw, GLuint *program_size);

private:
   void alloc_regs();
   void emit_frag_coord_computation();
   void emit_texture_coord_computation();
   void emit_texture_lookup();
   void emit_render_target_write();

   enum {
      TEXTURE_BINDING_TABLE_INDEX,
      RENDERBUFFER_BINDING_TABLE_INDEX,
      NUM_BINDING_TABLE_ENTRIES /* TODO: don't rely on the coincidence that this == GEN6_HIZ_NUM_BINDING_TABLE_ENTRIES. */
   };

   void *mem_ctx;
   struct brw_context *brw;
   const brw_blorp_blit_prog_key *key;
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

brw_blorp_blit_program::brw_blorp_blit_program(
      struct brw_context *brw,
      const brw_blorp_blit_prog_key *key)
   : mem_ctx(ralloc_context(NULL)),
     brw(brw),
     key(key)
{
   brw_init_compile(brw, &func, mem_ctx);
}

brw_blorp_blit_program::~brw_blorp_blit_program()
{
   ralloc_free(mem_ctx);
}

const GLuint *
brw_blorp_blit_program::compile(struct brw_context *brw,
                                GLuint *program_size)
{
   brw_set_compression_control(&func, BRW_COMPRESSION_NONE);

   alloc_regs();
   emit_frag_coord_computation();
   emit_texture_coord_computation();
   emit_texture_lookup();
   emit_render_target_write();
   return brw_get_program(&func, program_size);
}

void
brw_blorp_blit_program::alloc_regs()
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

void
brw_blorp_blit_program::emit_frag_coord_computation()
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
brw_blorp_blit_program::emit_texture_coord_computation()
{
   /* When looking up samples in an MSAA texture, Gen6 requires the texture
    * coordinates to be odd integers (so that they correspond to the center of
    * a 2x2 block representing the four samples that maxe up a pixel).  So we
    * need to multiply our X and Y coordinates each by 2 and then add 1.
    */
   brw_SHL(&func, u_tex, x_frag, brw_imm_w(1));
   brw_SHL(&func, v_tex, y_frag, brw_imm_w(1));
   brw_ADD(&func, u_tex, u_tex, brw_imm_w(1));
   brw_ADD(&func, v_tex, v_tex, brw_imm_w(1));
}

void
brw_blorp_blit_program::emit_texture_lookup()
{
   /* TODO: can we do some of this faster with a compressed instruction? */
   /* TODO: do we need to use 2NDHALF compression mode? */
   brw_MOV(&func, vec8(mrf_u_float), vec8(u_tex));
   brw_MOV(&func, offset(vec8(mrf_u_float), 1), suboffset(vec8(u_tex), 8));
   brw_MOV(&func, vec8(mrf_v_float), vec8(v_tex));
   brw_MOV(&func, offset(vec8(mrf_v_float), 1), suboffset(vec8(v_tex), 8));

   /* TODO: is this necessary? */
   /* TODO: what does this mean for LD mode? */
   brw_MOV(&func, mrf_r_float, brw_imm_f(0.5));

   brw_SAMPLE(&func,
              retype(Rdata, BRW_REGISTER_TYPE_UW) /* dest */,
              base_mrf /* msg_reg_nr */,
              vec8(mrf_u_float) /* src0 */,
              TEXTURE_BINDING_TABLE_INDEX,
              0 /* sampler -- ignored for SAMPLE_LD message */,
              WRITEMASK_XYZW,
              GEN5_SAMPLER_MESSAGE_SAMPLE,
              8 /* response_length */,
              6 /* msg_length */,
              0 /* header_present */,
              BRW_SAMPLER_SIMD_MODE_SIMD16,
              BRW_SAMPLER_RETURN_FORMAT_FLOAT32);
}

void
brw_blorp_blit_program::emit_render_target_write()
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
                RENDERBUFFER_BINDING_TABLE_INDEX,
                8 /* msg_length */,
                0 /* response_length */,
                true /* eot */,
                false /* header_present */);
}

brw_blorp_blit_params::brw_blorp_blit_params(struct intel_mipmap_tree *src_mt,
                                             struct intel_mipmap_tree *dst_mt,
                                             GLuint src_x, GLuint src_y,
                                             GLuint dst_x, GLuint dst_y,
                                             GLuint width, GLuint height)
{
   src.set(src_mt, 0, 0);
   dst.set(dst_mt, 0, 0);

   /* Temporary implementation restrictions.  TODO: eliminate. */
   {
      assert(src_mt->downsampled_mt);
      assert(!dst_mt->downsampled_mt);
      assert(src_x == 0);
      assert(src_y == 0);
      assert(dst_x == 0);
      assert(dst_y == 0);

      uint32_t src_w, src_h;
      src.get_miplevel_dims(&src_w, &src_h);
      assert(src_w == width);
      assert(src_h == height);

      uint32_t dst_w, dst_h;
      dst.get_miplevel_dims(&dst_w, &dst_h);
      assert(dst_w == width);
      assert(dst_h == height);
   }

   this->width = width;
   this->height = height;
   use_wm_prog = true;
   memset(&wm_prog_key, 0, sizeof(wm_prog_key));
   src_multisampled = true;
}

uint32_t
brw_blorp_blit_params::get_wm_prog(struct brw_context *brw) const
{
   uint32_t prog_offset;
   if (!brw_search_cache(&brw->cache, BRW_BLORP_BLIT_PROG,
                         &this->wm_prog_key, sizeof(this->wm_prog_key),
                         &prog_offset, NULL)) {
      brw_blorp_blit_program prog(brw, &this->wm_prog_key);
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
