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
try_blorp_blit(struct intel_context *intel,
               GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
               GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
               GLenum filter, GLbitfield buffer_bit)
{
   struct gl_context *ctx = &intel->ctx;

   /* Find buffers */
   const struct gl_framebuffer *read_fb = ctx->ReadBuffer;
   const struct gl_framebuffer *draw_fb = ctx->DrawBuffer;
   struct gl_renderbuffer *src_rb;
   struct gl_renderbuffer *dst_rb;
   switch (buffer_bit) {
   case GL_COLOR_BUFFER_BIT:
      src_rb = read_fb->_ColorReadBuffer;
      dst_rb =
         draw_fb->Attachment[
            draw_fb->_ColorDrawBufferIndexes[0]].Renderbuffer;
      break;
   case GL_DEPTH_BUFFER_BIT:
      src_rb = read_fb->Attachment[BUFFER_DEPTH].Renderbuffer;
      dst_rb = draw_fb->Attachment[BUFFER_DEPTH].Renderbuffer;
      break;
   case GL_STENCIL_BUFFER_BIT:
      src_rb = read_fb->Attachment[BUFFER_STENCIL].Renderbuffer;
      dst_rb = draw_fb->Attachment[BUFFER_STENCIL].Renderbuffer;
      break;
   default:
      assert(false);
   }

   /* Validate source */
   if (!src_rb) return false;
   struct intel_renderbuffer *src_irb = intel_renderbuffer(src_rb);
   struct intel_mipmap_tree *src_mt = src_irb->mt;
   if (!src_mt) return false; /* TODO: or is this guaranteed non-NULL? */
   if (!src_mt->num_samples > 0) return false; /* TODO: eliminate this restriction */
   if (buffer_bit == GL_STENCIL_BUFFER_BIT && src_mt->stencil_mt)
      src_mt = src_mt->stencil_mt; /* TODO: verify that this line is needed */

   /* Validate destination */
   if (!dst_rb) return false;
   struct intel_renderbuffer *dst_irb = intel_renderbuffer(dst_rb);
   struct intel_mipmap_tree *dst_mt = dst_irb->mt;
   if (!dst_mt) return false; /* TODO: or is this guaranteed non-NULL? */
   if (dst_mt->num_samples > 0) return false; /* TODO: eliminate this restriction */
   if (buffer_bit == GL_STENCIL_BUFFER_BIT && dst_mt->stencil_mt)
      dst_mt = dst_mt->stencil_mt; /* TODO: verify that this line is needed */

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

   /* Get ready to blit.  This includes depth resolving the src and dst
    * buffers if necessary.
    */
   intel_prepare_render(intel);
   intel_renderbuffer_resolve_depth(intel, src_irb);
   intel_renderbuffer_resolve_depth(intel, dst_irb);

   /* Do the blit */
   brw_blorp_blit_params params(src_mt, dst_mt,
                                srcX0, srcY0, dstX0, dstY0, width, height);
   params.exec(intel);

   /* Mark the dst buffer as needing a HiZ resolve if necessary. */
   intel_renderbuffer_set_needs_hiz_resolve(dst_irb);

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

   static GLbitfield buffer_bits[] = {
      GL_COLOR_BUFFER_BIT,
      GL_DEPTH_BUFFER_BIT,
      GL_STENCIL_BUFFER_BIT,
   };

   for (unsigned int i = 0; i < ARRAY_SIZE(buffer_bits); ++i) {
      if ((mask & buffer_bits[i]) &&
       try_blorp_blit(intel,
                      srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      filter, buffer_bits[i])) {
         mask &= ~buffer_bits[i];
      }
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

   brw_blorp_prog_data prog_data;

private:
   void alloc_regs();
   void alloc_push_const_regs(int base_reg);
   void emit_frag_coord_computation();
   void kill_if_out_of_range();
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

   /* Thread dispatch header */
   struct brw_reg R0;

   /* Pixel X/Y coordinates (always in R1). */
   struct brw_reg R1;

   /* Push constants */
   struct brw_reg dst_x0;
   struct brw_reg dst_x1;
   struct brw_reg dst_y0;
   struct brw_reg dst_y1;

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

   /* Temporaries */
   struct brw_reg t1;
   struct brw_reg t2;
   struct brw_reg t3;

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
   if (key->kill_out_of_range)
      kill_if_out_of_range();
   emit_texture_coord_computation();
   emit_texture_lookup();
   emit_render_target_write();
   return brw_get_program(&func, program_size);
}

void
brw_blorp_blit_program::alloc_push_const_regs(int base_reg)
{
#define CONST_LOC(name) offsetof(brw_blorp_wm_push_constants, name)
#define ALLOC_REG(name) \
   this->name = \
      brw_uw1_reg(BRW_GENERAL_REGISTER_FILE, base_reg, CONST_LOC(name) / 2)

   ALLOC_REG(dst_x0);
   ALLOC_REG(dst_x1);
   ALLOC_REG(dst_y0);
   ALLOC_REG(dst_y1);
#undef CONST_LOC
#undef ALLOC_REG
}

void
brw_blorp_blit_program::alloc_regs()
{
   int reg = 0;
   this->R0 = retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW);
   this->R1 = retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW);
   prog_data.first_curbe_grf = reg;
   alloc_push_const_regs(reg);
   reg += BRW_BLORP_NUM_PUSH_CONST_REGS;
   this->Rdata = vec16(brw_vec8_grf(reg, 0)); reg += 8;
   this->x_frag = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->y_frag = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->u_tex = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->v_tex = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->t1 = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->t2 = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->t3 = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   /* TODO: make more use of temporaries */

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

   if (key->adjust_coords_for_stencil) {
      /* The render target is W-tiled stencil data, but the surface state has
       * been set up for Y-tiled MESA_FORMAT_R8 data (this is necessary
       * because surface states don't support W tiling).  So we need to adjust
       * the coordinates by considering the memory location the output of
       * rendering will be written to.
       *
       * Since the render target has been set up for Y-tiled MESA_FORMAT_R8
       * data, the address where rendered output will be written, in terms of
       * the x_frag and y_frag coordinates compute above, will be:
       *
       *   Y-tiled MESA_FORMAT_R8:
       *   ttttttttttttttttttttxxxyyyyyxxxx                     (1)
       *
       * (That is, the first 20 bits of the memory address select which tile
       * we are rendering to (offset appropriately by the surface start
       * address), followed by bits 6-4 of the x coordinate within the tile,
       * followed by the y coordinate within the tile, followed by bits 3-0 of
       * the x coordinate).  See Graphics BSpec: vol1c Memory Interface and
       * Command Stream [SNB+] > Graphics Memory Interface Functions > Address
       * Tiling Function > W-Major Tile Format [DevIL+].
       *
       * However, since stencil data is actually W-tiled, the correct
       * interpretation of those address bits is:
       *
       *   W-tiled:
       *   ttttttttttttttttttttxxxyyyyxyxyx                     (2)
       *
       * Therefore, when the WM program appears to be generating a value for
       * the pixel coordinate given by:
       *
       *   x_frag = A << 7 | 0bBCDEFGH
       *   y_frag = J << 5 | 0bKLMNP                            (3)
       *
       * we can apply (1) to see that it is actually generating the byte that
       * will be stored at:
       *
       *   offset = (J * tile_pitch + A) << 12 | 0bBCDKLMNPEFGH (4)
       *
       * within the render target surface (where tile_pitch is the number of
       * tiles that cover the width of the render surface).  Rearranging these
       * bits according to (2), this corresponds to a true pixel coordinate
       * of:
       *
       *   x_stencil = A << 6 | 0bBCDPFH                        (5)
       *   y_stencil = J << 6 | 0bKLMNEG
       *
       * Combining (3) and (5), we see that to transform (x_frag, y_frag) to
       * (x_stencil, y_stencil), we need to make the following computation:
       *
       *   x_stencil = (x_frag & ~0b1011) >> 1
       *             | (y_frag & 0b1) << 2
       *             | x_frag & 0b1                             (6)
       *   y_stencil = (y_frag & ~0b1) << 1
       *             | (x_frag & 0b1000) >> 2
       *             | (x_frag & 0b10) >> 1
       */
      brw_AND(&func, t1, x_frag, brw_imm_uw(0xfff4)); /* x_frag & ~0b1011 */
      brw_SHR(&func, t1, t1, brw_imm_uw(1)); /* (x_frag & ~0b1011) >> 1 */
      brw_AND(&func, t2, y_frag, brw_imm_uw(1)); /* y_frag & 0b1 */
      brw_SHL(&func, t2, t2, brw_imm_uw(2)); /* (y_frag & 0b1) << 2 */
      brw_OR(&func, t1, t1, t2); /* (x_frag & ~0b1011) >> 1
                                    | (y_frag & 0b1) << 2 */
      brw_AND(&func, t2, x_frag, brw_imm_uw(1)); /* x_frag & 0b1 */
      brw_OR(&func, t1, t1, t2); /* x_stencil */
      brw_AND(&func, t2, y_frag, brw_imm_uw(0xfffe)); /* y_frag & ~0b1 */
      brw_SHL(&func, t2, t2, brw_imm_uw(1)); /* (y_frag & ~0b1) << 1 */
      brw_AND(&func, t3, x_frag, brw_imm_uw(8)); /* x_frag & 0b1000 */
      brw_SHR(&func, t3, t3, brw_imm_uw(2)); /* (x_frag & 0b1000) >> 2 */
      brw_OR(&func, t2, t2, t3); /* (y_frag & ~0b1) << 1
                                    | (x_frag & 0b1000) >> 2 */
      brw_AND(&func, t3, x_frag, brw_imm_uw(2)); /* x_frag & 0b10 */
      brw_SHR(&func, t3, t3, brw_imm_uw(1)); /* (x_frag & 0b10) >> 1 */
      brw_OR(&func, y_frag, t2, t3); /* y_stencil */
      brw_MOV(&func, x_frag, t1); /* x_stencil */
   }
}

void
brw_blorp_blit_program::kill_if_out_of_range()
{
   struct brw_reg f0 = brw_flag_reg();
   struct brw_reg g1 = retype(brw_vec1_grf(1, 7), BRW_REGISTER_TYPE_UW);
   struct brw_reg null16 = vec16(retype(brw_null_reg(), BRW_REGISTER_TYPE_UW));

   brw_CMP(&func, null16, BRW_CONDITIONAL_GE, x_frag, dst_x0);
   brw_CMP(&func, null16, BRW_CONDITIONAL_GE, y_frag, dst_y0);
   brw_CMP(&func, null16, BRW_CONDITIONAL_L, x_frag, dst_x1);
   brw_CMP(&func, null16, BRW_CONDITIONAL_L, y_frag, dst_y1);

   brw_set_predicate_control(&func, BRW_PREDICATE_NONE);
   brw_push_insn_state(&func);
   brw_set_mask_control(&func, BRW_MASK_DISABLE);
   brw_AND(&func, g1, f0, g1);
   brw_pop_insn_state(&func);
}

void
brw_blorp_blit_program::emit_texture_coord_computation()
{
   if (key->blend) {
      /* When looking up samples in an MSAA texture using the SAMPLE message,
       * Gen6 requires the texture coordinates to be odd integers (so that
       * they correspond to the center of a 2x2 block representing the four
       * samples that maxe up a pixel).  So we need to multiply our X and Y
       * coordinates each by 2 and then add 1.
       */
      brw_SHL(&func, u_tex, x_frag, brw_imm_w(1));
      brw_SHL(&func, v_tex, y_frag, brw_imm_w(1));
      brw_ADD(&func, u_tex, u_tex, brw_imm_w(1));
      brw_ADD(&func, v_tex, v_tex, brw_imm_w(1));
   } else if (key->manual_downsample) {
      /* We are looking up samples in an MSAA texture, but that texture is not
       * flagged as multisampled in the surface state description (we do this
       * when reading from a stencil buffer).  So we need to manually adjust
       * the coordinates to pick up just sample 0 from each multisampled
       * pixel.
       *
       * To convert from single-sampled x and y coordinates to the u and v
       * coordinates we need to look up data in the MSAA stencil surface, we
       * need to apply the following formulas (inferred from the diagrams in
       * Graphics BSpec: vol1a GPU Overview [All projects] > Memory Data
       * Formats > Surface Layout and Tiling [DevSKL+] > Stencil Buffer
       * Layout):
       *
       *   u_tex = (x_frag & ~0b1) << 1
       *         | (sample_num & 0b1) << 1
       *         | (x_frag & 0b1)
       *   v_tex = (y_frag & ~0b1) << 1
       *         | sample_num & 0b10
       *         | (y_frag & 0b1)
       *
       * Since we just want to look up sample_num=0, this simplifies to:
       *
       *   u_tex = (x_frag & ~0b1) << 1
       *         | (x_frag & 0b1)
       *   v_tex = (y_frag & ~0b1) << 1
       *         | (y_frag & 0b1)
       */
      brw_AND(&func, u_tex, x_frag, brw_imm_uw(0xfffe)); /* x_frag & ~0b1 */
      brw_SHL(&func, u_tex, u_tex, brw_imm_uw(1)); /* (x_frag & ~0b1) << 1 */
      brw_AND(&func, x_frag, x_frag, brw_imm_uw(1)); /* x_frag & 0b1 */
      brw_OR(&func, u_tex, u_tex, x_frag); /* u_tex */
      brw_AND(&func, v_tex, y_frag, brw_imm_uw(0xfffe)); /* y_frag & ~0b1 */
      brw_SHL(&func, v_tex, v_tex, brw_imm_uw(1)); /* (y_frag & ~0b1) << 1 */
      brw_AND(&func, y_frag, y_frag, brw_imm_uw(1)); /* y_frag & 0b1 */
      brw_OR(&func, v_tex, v_tex, y_frag); /* v_tex */
   } else {
      /* When looking up samples in an MSAA texture using the SAMPLE_LD message,
       * Gen6 just needs the integer texture coordinates.
       */
      brw_MOV(&func, u_tex, x_frag);
      brw_MOV(&func, v_tex, y_frag);
   }

   if (key->adjust_coords_for_stencil) {
      /* The texture is W-tiled stencil data, but the surface state has
       * been set up for Y-tiled MESA_FORMAT_R8 data (this is necessary
       * because surface states don't support W tiling).  So we need to adjust
       * the coordinates by considering the memory location the output of
       * rendering will be written to.
       *
       * In emit_frag_coord_computation(), we had to translate x and y
       * coordinates which were incorrect (because they assumed Y tiling
       * instead of W tiling) into correct ones.  Now, we need to translate
       * correct u and v coordinates into incorrect ones so that we can use
       * them for texture lookup.  So we simply reverse the computation:
       *
       * u_stencil =  AAABCDPFH  000001001
       * u_tex =     AAABCDEFGH 0000010011
       * v_stencil =  JJJKLMNEG  000001001
       * v_tex =       JJJKLMNP
       *
       * u_tex = (u_stencil & ~0b101) << 1
       *       | (v_stencil & 0b10) << 2
       *       | (v_stencil & 0b1) << 1
       *       | u_stencil & 0b1
       * v_tex = (v_stencil & ~0b11) >> 1
       *       | (u_stencil & 0b100) >> 2
       */
      brw_AND(&func, t1, u_tex, brw_imm_uw(0xfffa)); /* u_stencil & ~0b101 -- 000001000 */
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (u_stencil & ~0b101) << 1 -- 000010000 */
      brw_AND(&func, t2, v_tex, brw_imm_uw(2)); /* v_stencil & 0b10 -- */
      brw_SHL(&func, t2, t2, brw_imm_uw(2)); /* (v_stencil & 0b10) << 2 */
      brw_OR(&func, t1, t1, t2); /* (u_stencil & ~0b101) << 1
                                    | (v_stencil & 0b10) << 2 */
      brw_AND(&func, t2, v_tex, brw_imm_uw(1)); /* v_stencil & 0b1 */
      brw_SHL(&func, t2, t2, brw_imm_uw(1)); /* (v_stencil & 0b1) << 1 */
      brw_OR(&func, t1, t1, t2); /* (u_stencil & ~0b101) << 1
                                    | (v_stencil & 0b10) << 2
                                    | (v_stencil & 0b1) << 1 */
      brw_AND(&func, t2, u_tex, brw_imm_uw(1)); /* u_stencil & 0b1 */
      brw_OR(&func, t1, t1, t2); /* u_tex */
      brw_AND(&func, t2, v_tex, brw_imm_uw(0xfffc)); /* v_stencil & ~0b11 */
      brw_SHR(&func, t2, t2, brw_imm_uw(1)); /* (v_stencil & ~0b11) >> 1 */
      brw_AND(&func, t3, u_tex, brw_imm_uw(4)); /* u_stencil & 0b100 */
      brw_SHR(&func, t3, t3, brw_imm_uw(2)); /* (u_stencil & 0b100) >> 2 */
      brw_OR(&func, v_tex, t2, t3); /* v_tex */
      brw_MOV(&func, u_tex, t1); /* u_tex */
   }
}

void
brw_blorp_blit_program::emit_texture_lookup()
{
   struct brw_reg mrf_u, mrf_v;
   if (key->blend) {
      /* We'll be using a SAMPLE message, which expects floating point texture
       * coordinates.
       */
      mrf_u = mrf_u_float;
      mrf_v = mrf_v_float;
   } else {
      /* We'll be using a SAMPLE_LD message, which expects integer texture
       * coordinates.
       */
      mrf_u = retype(mrf_u_float, BRW_REGISTER_TYPE_UD);
      mrf_v = retype(mrf_v_float, BRW_REGISTER_TYPE_UD);
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
              TEXTURE_BINDING_TABLE_INDEX,
              0 /* sampler -- ignored for SAMPLE_LD message */,
              WRITEMASK_XYZW,
              key->blend ? GEN5_SAMPLER_MESSAGE_SAMPLE
                    : GEN5_SAMPLER_MESSAGE_SAMPLE_LD,
              8 /* response_length.  TODO: should be smaller for non-RGBA formats? */,
              6 /* msg_length */,
              0 /* header_present */,
              BRW_SAMPLER_SIMD_MODE_SIMD16,
              BRW_SAMPLER_RETURN_FORMAT_FLOAT32);
}

void
brw_blorp_blit_program::emit_render_target_write()
{
   struct brw_reg mrf_rt_write = vec16(brw_message_reg(base_mrf));
   int mrf_offset = 0;

   /* If we may have killed pixels, then we need to send R0 and R1 in a header
    * so that the render target knows which pixels we killed.
    */
   bool use_header = key->kill_out_of_range;
   if (use_header) {
      /* Copy R0/1 to MRF */
      brw_MOV(&func, retype(mrf_rt_write, BRW_REGISTER_TYPE_UD),
              retype(R0, BRW_REGISTER_TYPE_UD));
      mrf_offset += 2;
   }

   /* Copy texture data to MRFs */
   for (int i = 0; i < 4; ++i) {
      /* E.g. mov(16) m2.0<1>:f r2.0<8;8,1>:f { Align1, H1 } */
      brw_MOV(&func, offset(mrf_rt_write, mrf_offset), offset(vec8(Rdata), 2*i));
      mrf_offset += 2;
   }

   /* Now write to the render target and terminate the thread */
   brw_fb_WRITE(&func,
                16 /* dispatch_width */,
                base_mrf /* msg_reg_nr */,
                mrf_rt_write /* src0 */,
                RENDERBUFFER_BINDING_TABLE_INDEX,
                mrf_offset /* msg_length.  Should be smaller for non-RGBA formats. */,
                0 /* response_length */,
                true /* eot */,
                use_header);
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
      assert(src_mt->num_samples > 0);
      assert(!dst_mt->num_samples > 0);
      assert(src_x == 0);
      assert(src_y == 0);
      assert(dst_x == 0);
      assert(dst_y == 0);

      uint32_t src_w, src_h;
      src.get_miplevel_dims(&src_w, &src_h);
      assert(src_w == width * 2);
      assert(src_h == height * 2);

      uint32_t dst_w, dst_h;
      dst.get_miplevel_dims(&dst_w, &dst_h);
      assert(dst_w == width);
      assert(dst_h == height);
   }

   this->width = width;
   this->height = height;
   use_wm_prog = true;
   memset(&wm_prog_key, 0, sizeof(wm_prog_key));
   if (src_mt->format == MESA_FORMAT_S8) {
      wm_prog_key.blend = false;
      src_multisampled = false;
      wm_prog_key.manual_downsample = true;
      src.map_stencil_as_y_tiled = true;
      dst.map_stencil_as_y_tiled = true;
      wm_prog_key.adjust_coords_for_stencil = true;
      wm_prog_key.kill_out_of_range = true; /* TODO: only do when necessary */

      /* Adjust width/height to compensate for the fact that src and dst will
       * be mapped as Y tiled instead of W tiled.
       */
      assert((this->width & 63) == 0); /* TODO: because discarding is not implemented yet */
      assert((this->height & 63) == 0); /* TODO: because discarding is not implemented yet */
      this->width *= 2; /* TODO: what if this makes the width too large? */
      this->height /= 2;
   } else if (_mesa_get_format_base_format(src_mt->format) == GL_DEPTH_COMPONENT) {
      /* TODO: test all depth formats */
      wm_prog_key.blend = false;
      wm_prog_key.manual_downsample = false;
      wm_prog_key.adjust_coords_for_stencil = false;
      src_multisampled = true;
   } else { /* Color buffer */
      wm_prog_key.blend = true;
      wm_prog_key.manual_downsample = false;
      wm_prog_key.adjust_coords_for_stencil = false;
      src_multisampled = true;
   }

   /* TODO: as a tempoarary measure use a static destination rect */
   wm_push_consts.dst_x0 = 64;
   wm_push_consts.dst_y0 = 64;
   wm_push_consts.dst_x1 = 128;
   wm_push_consts.dst_y1 = 128;
}

uint32_t
brw_blorp_blit_params::get_wm_prog(struct brw_context *brw,
                                   brw_blorp_prog_data **prog_data) const
{
   uint32_t prog_offset;
   if (!brw_search_cache(&brw->cache, BRW_BLORP_BLIT_PROG,
                         &this->wm_prog_key, sizeof(this->wm_prog_key),
                         &prog_offset, prog_data)) {
      brw_blorp_blit_program prog(brw, &this->wm_prog_key);
      GLuint program_size;
      const GLuint *program = prog.compile(brw, &program_size);
      brw_upload_cache(&brw->cache, BRW_MSAA_WM_PROG,
                       &this->wm_prog_key, sizeof(this->wm_prog_key),
                       program, program_size,
                       &prog.prog_data, sizeof(prog.prog_data),
                       &prog_offset, prog_data);
   }
   return prog_offset;
}
