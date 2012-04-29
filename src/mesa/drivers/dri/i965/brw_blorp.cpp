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
   if (buffer_bit == GL_STENCIL_BUFFER_BIT && src_mt->stencil_mt)
      src_mt = src_mt->stencil_mt; /* TODO: verify that this line is needed */

   /* Validate destination */
   if (!dst_rb) return false;
   struct intel_renderbuffer *dst_irb = intel_renderbuffer(dst_rb);
   struct intel_mipmap_tree *dst_mt = dst_irb->mt;
   if (!dst_mt) return false; /* TODO: or is this guaranteed non-NULL? */
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
   void emit_dst_coord_computation();
   void emit_src_coord_computation();
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
   struct brw_reg x_offset;
   struct brw_reg y_offset;

   /* Data returned from texture lookup (4 vec16's) */
   struct brw_reg Rdata;

   /* X/U coordinate */
   struct brw_reg x_or_u_coord[2];

   /* Y/V coordinate */
   struct brw_reg y_or_v_coord[2];

   /* Which element of x_or_u_coord is x; which element of y_or_v_coord is
    * y.
    */
   int xy_coord_index;

   /* Temporaries */
   struct brw_reg t1;
   struct brw_reg t2;

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
   emit_dst_coord_computation();
   if (key->kill_out_of_range)
      kill_if_out_of_range();
   emit_src_coord_computation();
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
   ALLOC_REG(x_offset);
   ALLOC_REG(y_offset);
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
   for (int i = 0; i < 2; ++i) {
      this->x_or_u_coord[i]
         = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
      this->y_or_v_coord[i]
         = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   }
   this->xy_coord_index = 0;
   this->t1 = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->t2 = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));

   int mrf = 2;
   this->base_mrf = mrf;
   this->mrf_u_float = vec16(brw_message_reg(mrf)); mrf += 2;
   this->mrf_v_float = vec16(brw_message_reg(mrf)); mrf += 2;
   this->mrf_r_float = vec16(brw_message_reg(mrf)); mrf += 2;
}

/* In the code that follows, X, Y, U, and V can be used to quickly refer to
 * the appropriate elements of x_or_u_coord and y_or_v_coord.
 */
#define X x_or_u_coord[xy_coord_index]
#define Y y_or_v_coord[xy_coord_index]
#define U x_or_u_coord[!xy_coord_index]
#define V y_or_v_coord[!xy_coord_index]

/* Quickly swap the roles of XY and UV.  Saves us from having to do a lot of
 * MOVs.
 */
#define SWAP_XY_UV() xy_coord_index = !xy_coord_index;

void
brw_blorp_blit_program::emit_dst_coord_computation()
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
   brw_ADD(&func, X, stride(suboffset(R1, 4), 2, 4, 0), brw_imm_v(0x10101010));

   /* Similarly, Y coordinates for subspans come from R1.2[31:16] through
    * R1.5[31:16], so to get pixel Y coordinates we need to start at the 5th
    * 16-bit value instead of the 4th (R1.5<2;4,0>UW instead of
    * R1.4<2;4,0>UW).
    *
    * And we need to add the repeating sequence (0, 0, 1, 1, ...), since
    * pixels n+2 and n+3 are in the bottom half of the subspan.
    */
   brw_ADD(&func, Y, stride(suboffset(R1, 5), 2, 4, 0), brw_imm_v(0x11001100));

   if (key->adjust_coords_for_stencil) {
      /* The WM stage has been configured to render to a Y-tiled surface, but
       * the actual data is W-tiled.  Therefore the X and Y pixel delivered to
       * the WM program aren't the true coordinates in the W-tiled
       * surface--they are "swizzled" around based on the differences between
       * W and Y tiling.  To convert to the true coordinates, we need to
       * determine the memory address that the output will be written to
       * (using Y-tiled formulas), and then work out the true coordinates of
       * the data represented by that memory address (using W-tiled formulas).
       *
       * Let X and Y represent the swizzled Y-tiled coordinates, and U and V
       * represent the true W-tiled coordinates.
       *
       * The interpretation of memory addresses when Y-tiling is given by the
       * bit pattern:
       *
       *   Y-tiled MESA_FORMAT_R8:
       *   ttttttttttttttttttttxxxyyyyyxxxx                           (1)
       *
       * (That is, the first 20 bits of the memory address select which tile
       * we are rendering to (offset appropriately by the surface start
       * address), followed by bits 6-4 of the x coordinate within the tile,
       * followed by the y coordinate within the tile, followed by bits 3-0 of
       * the x coordinate).  See Graphics BSpec: vol1c Memory Interface and
       * Command Stream [SNB+] > Graphics Memory Interface Functions > Address
       * Tiling Function > W-Major Tile Format [DevIL+].
       *
       * Therefore, if we break down the low order bits of X and Y, using a
       * single letter to represent each low-order bit:
       *
       *   X = A << 7 | 0bBCDEFGH
       *   Y = J << 5 | 0bKLMNP                                       (2)
       *
       * Then we can apply (1) to see the memory location being addressed (as
       * an offset from the origin of the surface the surface):
       *
       *   offset = (J * tile_pitch + A) << 12 | 0bBCDKLMNPEFGH       (3)
       *
       * (where tile_pitch is the number of tiles that cover the width of the
       * render surface).
       *
       * The interpretation of memory addresses when W-tiling is given by the
       * bit pattern:
       *
       *   W-tiled:
       *   ttttttttttttttttttttuuuvvvvuvuvu                           (4)
       *
       * If we apply this to the memory location computed in (3), we see that
       * the corresponding U and V coordinates are:
       *
       *   U = A << 6 | 0bBCDPFH                                      (5)
       *   V = J << 6 | 0bKLMNEG
       *
       * Combining (2) and (5), we see that to transform (X, Y) to (U, V), we
       * need to make the following computation:
       *
       *   U = (X & ~0b1011) >> 1 | (Y & 0b1) << 2 | X & 0b1          (6)
       *   V = (Y & ~0b1) << 1 | (X & 0b1000) >> 2 | (X & 0b10) >> 1
       */
      brw_AND(&func, t1, X, brw_imm_uw(0xfff4)); /* X & ~0b1011 */
      brw_SHR(&func, t1, t1, brw_imm_uw(1)); /* (X & ~0b1011) >> 1 */
      brw_AND(&func, t2, Y, brw_imm_uw(1)); /* Y & 0b1 */
      brw_SHL(&func, t2, t2, brw_imm_uw(2)); /* (Y & 0b1) << 2 */
      brw_OR(&func, t1, t1, t2); /* (X & ~0b1011) >> 1 | (Y & 0b1) << 2 */
      brw_AND(&func, t2, X, brw_imm_uw(1)); /* X & 0b1 */
      brw_OR(&func, U, t1, t2);
      brw_AND(&func, t1, Y, brw_imm_uw(0xfffe)); /* Y & ~0b1 */
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (Y & ~0b1) << 1 */
      brw_AND(&func, t2, X, brw_imm_uw(8)); /* X & 0b1000 */
      brw_SHR(&func, t2, t2, brw_imm_uw(2)); /* (X & 0b1000) >> 2 */
      brw_OR(&func, t1, t1, t2); /* (Y & ~0b1) << 1 | (X & 0b1000) >> 2 */
      brw_AND(&func, t2, X, brw_imm_uw(2)); /* X & 0b10 */
      brw_SHR(&func, t2, t2, brw_imm_uw(1)); /* (X & 0b10) >> 1 */
      brw_OR(&func, V, t1, t2); /* y_stencil */
      SWAP_XY_UV();
   }
}

void
brw_blorp_blit_program::emit_src_coord_computation()
{
   brw_ADD(&func, U, X, x_offset);
   brw_ADD(&func, V, Y, y_offset);
   SWAP_XY_UV();
}

void
brw_blorp_blit_program::kill_if_out_of_range()
{
   struct brw_reg f0 = brw_flag_reg();
   struct brw_reg g1 = retype(brw_vec1_grf(1, 7), BRW_REGISTER_TYPE_UW);
   struct brw_reg null16 = vec16(retype(brw_null_reg(), BRW_REGISTER_TYPE_UW));

   brw_CMP(&func, null16, BRW_CONDITIONAL_GE, X, dst_x0);
   brw_CMP(&func, null16, BRW_CONDITIONAL_GE, Y, dst_y0);
   brw_CMP(&func, null16, BRW_CONDITIONAL_L, X, dst_x1);
   brw_CMP(&func, null16, BRW_CONDITIONAL_L, Y, dst_y1);

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
      brw_SHL(&func, t1, X, brw_imm_w(1));
      brw_SHL(&func, t2, Y, brw_imm_w(1));
      brw_ADD(&func, U, t1, brw_imm_w(1));
      brw_ADD(&func, V, t2, brw_imm_w(1));
      SWAP_XY_UV();
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
       *   U = (X & ~0b1) << 1 | (sample_num & 0b1) << 1 | (X & 0b1)
       *   V = (Y & ~0b1) << 1 | sample_num & 0b10 | (Y & 0b1)
       *
       * Since we just want to look up sample_num=0, this simplifies to:
       *
       *   U = (X & ~0b1) << 1 | (X & 0b1)
       *   V = (Y & ~0b1) << 1 | (Y & 0b1)
       */
      brw_AND(&func, t1, X, brw_imm_uw(0xfffe)); /* X & ~0b1 */
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (X & ~0b1) << 1 */
      brw_AND(&func, t2, X, brw_imm_uw(1)); /* X & 0b1 */
      brw_OR(&func, U, t1, t2);
      brw_AND(&func, t1, Y, brw_imm_uw(0xfffe)); /* Y & ~0b1 */
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (Y & ~0b1) << 1 */
      brw_AND(&func, t2, Y, brw_imm_uw(1)); /* Y & 0b1 */
      brw_OR(&func, V, t1, t2);
      SWAP_XY_UV();
   } else {
      /* We're just looking up samples using simple integer texture
       * coordinates.  Nothing to do.
       */
   }

   if (key->adjust_coords_for_stencil) {
      /* The texture is W-tiled stencil data, but the surface state has
       * been set up for Y-tiled MESA_FORMAT_R8 data (this is necessary
       * because surface states don't support W tiling).  So we need to adjust
       * the coordinates by considering the memory location the output of
       * rendering will be written to.
       *
       * We simply reverse the computation from emit_dst_coord_computation():
       *
       * U = (X & ~0b101) << 1 | (Y & 0b10) << 2 | (Y & 0b1) << 1 | X & 0b1
       * V = (Y & ~0b11) >> 1 | (X & 0b100) >> 2
       */
      brw_AND(&func, t1, X, brw_imm_uw(0xfffa)); /* X & ~0b101 */
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (X & ~0b101) << 1 */
      brw_AND(&func, t2, Y, brw_imm_uw(2)); /* Y & 0b10 */
      brw_SHL(&func, t2, t2, brw_imm_uw(2)); /* (Y & 0b10) << 2 */
      brw_OR(&func, t1, t1, t2); /* (X & ~0b101) << 1 | (Y & 0b10) << 2 */
      brw_AND(&func, t2, Y, brw_imm_uw(1)); /* Y & 0b1 */
      brw_SHL(&func, t2, t2, brw_imm_uw(1)); /* (Y & 0b1) << 1 */
      brw_OR(&func, t1, t1, t2); /* (X & ~0b101) << 1 | (Y & 0b10) << 2
                                    | (Y & 0b1) << 1 */
      brw_AND(&func, t2, X, brw_imm_uw(1)); /* X & 0b1 */
      brw_OR(&func, U, t1, t2);
      brw_AND(&func, t1, Y, brw_imm_uw(0xfffc)); /* Y & ~0b11 */
      brw_SHR(&func, t1, t1, brw_imm_uw(1)); /* (Y & ~0b11) >> 1 */
      brw_AND(&func, t2, X, brw_imm_uw(4)); /* X & 0b100 */
      brw_SHR(&func, t2, t2, brw_imm_uw(2)); /* (X & 0b100) >> 2 */
      brw_OR(&func, V, t1, t2);
      SWAP_XY_UV();
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
   brw_MOV(&func, vec8(mrf_u), vec8(X));
   brw_MOV(&func, offset(vec8(mrf_u), 1), suboffset(vec8(X), 8));
   brw_MOV(&func, vec8(mrf_v), vec8(Y));
   brw_MOV(&func, offset(vec8(mrf_v), 1), suboffset(vec8(Y), 8));

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

#undef X
#undef Y
#undef U
#undef V
#undef SWAP_XY_UV

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
                                             GLuint src_x0, GLuint src_y0,
                                             GLuint dst_x0, GLuint dst_y0,
                                             GLuint width, GLuint height)
{
   src.set(src_mt, 0, 0);
   dst.set(dst_mt, 0, 0);

   /* Temporary implementation restrictions.  TODO: eliminate. */
   {
      assert(dst_mt->num_samples == 0 || src_mt->num_samples == 0);
   }

   this->x0 = dst_x0;
   this->y0 = dst_y0;
   this->x1 = dst_x0 + width;
   this->y1 = dst_y0 + height;

   wm_push_consts.dst_x0 = dst_x0;
   wm_push_consts.dst_y0 = dst_y0;
   wm_push_consts.dst_x1 = dst_x0 + width;
   wm_push_consts.dst_y1 = dst_y0 + height;
   wm_push_consts.x_offset = src_x0 - dst_x0;
   wm_push_consts.y_offset = src_y0 - dst_y0;

   use_wm_prog = true;
   memset(&wm_prog_key, 0, sizeof(wm_prog_key));
   if (src_mt->format == MESA_FORMAT_S8) {
      wm_prog_key.blend = false;
      src_multisampled = false;
      dst_multisampled = false;
      wm_prog_key.manual_downsample = src_mt->num_samples > 0;
      src.map_stencil_as_y_tiled = true;
      dst.map_stencil_as_y_tiled = true;
      wm_prog_key.adjust_coords_for_stencil = true;
      if ((this->x0 & 63) != 0 || (this->y0 & 63) != 0 ||
          (this->x1 & 63) != 0 || (this->y1 & 63) != 0) {
         /* The destination rectangle is not tile-aligned.  We need to send a
          * tile-aligned rectangle down the pipeline (since we've mapped the
          * destination buffer as Y-tiled instead of W-tiled), so compute an
          * expanded rectangle, and tell the WM program to kill any pixels
          * that are outside the region we really want to blit to.
          */
         this->x0 &= ~63;
         this->y0 &= ~63;
         this->x1 = ALIGN(this->x1, 64);
         this->y1 = ALIGN(this->y1, 64);
         wm_prog_key.kill_out_of_range = true;
      }

      /* Adjust coords to compensate for the fact that src and dst will be
       * mapped as Y tiled instead of W tiled.
       */
      this->x0 *= 2; /* TODO: what if this makes the x values too large? */
      this->y0 /= 2;
      this->x1 *= 2; /* TODO: what if this makes the x values too large? */
      this->y1 /= 2;
   } else if (_mesa_get_format_base_format(src_mt->format) == GL_DEPTH_COMPONENT) {
      /* TODO: test all depth formats */
      wm_prog_key.blend = false;
      wm_prog_key.manual_downsample = false;
      wm_prog_key.adjust_coords_for_stencil = false;
      src_multisampled = src_mt->num_samples > 0;
      dst_multisampled = dst_mt->num_samples > 0;
   } else { /* Color buffer */
      wm_prog_key.blend = src_mt->num_samples > 0; /* TODO: shouldn't blend in multisample-to-multisample blit */
      wm_prog_key.manual_downsample = false;
      wm_prog_key.adjust_coords_for_stencil = false;
      src_multisampled = src_mt->num_samples > 0;
      dst_multisampled = dst_mt->num_samples > 0;
   }
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
