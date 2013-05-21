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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

extern "C" {
#include "main/teximage.h"
#include "main/blend.h"
#include "main/fbobject.h"
#include "main/renderbuffer.h"
}

#include "glsl/ralloc.h"

#include "intel_fbo.h"

#include "brw_blorp.h"
#include "brw_context.h"
#include "brw_eu.h"
#include "brw_state.h"

struct brw_blorp_clear_prog_key
{
   bool use_simd16_replicated_data;
   bool pad[3];
};

class brw_blorp_clear_params : public brw_blorp_params
{
public:
   brw_blorp_clear_params(struct brw_context *brw,
                          struct gl_framebuffer *fb,
                          struct gl_renderbuffer *rb,
                          GLubyte *color_mask);

   virtual uint32_t get_wm_prog(struct brw_context *brw,
                                brw_blorp_prog_data **prog_data) const;

private:
   brw_blorp_clear_prog_key wm_prog_key;
};

class brw_blorp_clear_program
{
public:
   brw_blorp_clear_program(struct brw_context *brw,
                          const brw_blorp_clear_prog_key *key);
   ~brw_blorp_clear_program();

   const GLuint *compile(struct brw_context *brw, GLuint *program_size);

   brw_blorp_prog_data prog_data;

private:
   void alloc_regs();

   void *mem_ctx;
   struct brw_context *brw;
   const brw_blorp_clear_prog_key *key;
   struct brw_compile func;

   /* Thread dispatch header */
   struct brw_reg R0;

   /* Pixel X/Y coordinates (always in R1). */
   struct brw_reg R1;

   /* Register with push constants (a single vec4) */
   struct brw_reg clear_rgba;

   /* MRF used for render target writes */
   GLuint base_mrf;
};

brw_blorp_clear_program::brw_blorp_clear_program(
      struct brw_context *brw,
      const brw_blorp_clear_prog_key *key)
   : mem_ctx(ralloc_context(NULL)),
     brw(brw),
     key(key)
{
   brw_init_compile(brw, &func, mem_ctx);
}

brw_blorp_clear_program::~brw_blorp_clear_program()
{
   ralloc_free(mem_ctx);
}

brw_blorp_clear_params::brw_blorp_clear_params(struct brw_context *brw,
                                               struct gl_framebuffer *fb,
                                               struct gl_renderbuffer *rb,
                                               GLubyte *color_mask)
{
   struct intel_context *intel = &brw->intel;
   struct gl_context *ctx = &intel->ctx;
   struct intel_renderbuffer *irb = intel_renderbuffer(rb);

   dst.set(brw, irb->mt, irb->mt_level, irb->mt_layer);

   /* Override the surface format according to the context's sRGB rules. */
   gl_format format = _mesa_get_render_format(ctx, irb->mt->format);
   dst.brw_surfaceformat = brw->render_target_format[format];

   x0 = fb->_Xmin;
   x1 = fb->_Xmax;
   if (rb->Name != 0) {
      y0 = fb->_Ymin;
      y1 = fb->_Ymax;
   } else {
      y0 = rb->Height - fb->_Ymax;
      y1 = rb->Height - fb->_Ymin;
   }

   float *push_consts = (float *)&wm_push_consts;

   push_consts[0] = ctx->Color.ClearColor.f[0];
   push_consts[1] = ctx->Color.ClearColor.f[1];
   push_consts[2] = ctx->Color.ClearColor.f[2];
   push_consts[3] = ctx->Color.ClearColor.f[3];

   use_wm_prog = true;

   memset(&wm_prog_key, 0, sizeof(wm_prog_key));

   wm_prog_key.use_simd16_replicated_data = true;

   /* From the SNB PRM (Vol4_Part1):
    *
    *     "Replicated data (Message Type = 111) is only supported when
    *      accessing tiled memory.  Using this Message Type to access linear
    *      (untiled) memory is UNDEFINED."
    */
   struct intel_region *region =
      intel_miptree_get_region(intel, irb->mt, INTEL_MIPTREE_ACCESS_NONE);
   if (region->tiling == I915_TILING_NONE)
      wm_prog_key.use_simd16_replicated_data = false;

   /* Constant color writes ignore everyting in blend and color calculator
    * state.  This is not documented.
    */
   for (int i = 0; i < 4; i++) {
      if (!color_mask[i]) {
         color_write_disable[i] = true;
         wm_prog_key.use_simd16_replicated_data = false;
      }
   }
}

uint32_t
brw_blorp_clear_params::get_wm_prog(struct brw_context *brw,
                                   brw_blorp_prog_data **prog_data) const
{
   uint32_t prog_offset;
   if (!brw_search_cache(&brw->cache, BRW_BLORP_CLEAR_PROG,
                         &this->wm_prog_key, sizeof(this->wm_prog_key),
                         &prog_offset, prog_data)) {
      brw_blorp_clear_program prog(brw, &this->wm_prog_key);
      GLuint program_size;
      const GLuint *program = prog.compile(brw, &program_size);
      brw_upload_cache(&brw->cache, BRW_BLORP_CLEAR_PROG,
                       &this->wm_prog_key, sizeof(this->wm_prog_key),
                       program, program_size,
                       &prog.prog_data, sizeof(prog.prog_data),
                       &prog_offset, prog_data);
   }
   return prog_offset;
}

void
brw_blorp_clear_program::alloc_regs()
{
   int reg = 0;
   this->R0 = retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW);
   this->R1 = retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW);

   prog_data.first_curbe_grf = reg;
   clear_rgba = retype(brw_vec4_grf(reg++, 0), BRW_REGISTER_TYPE_F);
   reg += BRW_BLORP_NUM_PUSH_CONST_REGS;

   /* Make sure we didn't run out of registers */
   assert(reg <= GEN7_MRF_HACK_START);

   this->base_mrf = 2;
}

const GLuint *
brw_blorp_clear_program::compile(struct brw_context *brw,
                                 GLuint *program_size)
{
   /* Set up prog_data */
   memset(&prog_data, 0, sizeof(prog_data));
   prog_data.persample_msaa_dispatch = false;

   alloc_regs();

   brw_set_compression_control(&func, BRW_COMPRESSION_NONE);

   struct brw_reg mrf_rt_write =
      retype(vec16(brw_message_reg(base_mrf)), BRW_REGISTER_TYPE_F);

   uint32_t mlen, msg_type;
   if (key->use_simd16_replicated_data) {
      /* The message payload is a single register with the low 4 floats/ints
       * filled with the constant clear color.
       */
      brw_set_mask_control(&func, BRW_MASK_DISABLE);
      brw_MOV(&func, vec4(brw_message_reg(base_mrf)), clear_rgba);
      brw_set_mask_control(&func, BRW_MASK_ENABLE);

      msg_type = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE_REPLICATED;
      mlen = 1;
   } else {
      for (int i = 0; i < 4; i++) {
         /* The message payload is pairs of registers for 16 pixels each of r,
          * g, b, and a.
          */
         brw_set_compression_control(&func, BRW_COMPRESSION_COMPRESSED);
         brw_MOV(&func,
                 brw_message_reg(base_mrf + i * 2),
                 brw_vec1_grf(clear_rgba.nr, i));
         brw_set_compression_control(&func, BRW_COMPRESSION_NONE);
      }

      msg_type = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE;
      mlen = 8;
   }

   /* Now write to the render target and terminate the thread */
   brw_fb_WRITE(&func,
                16 /* dispatch_width */,
                base_mrf /* msg_reg_nr */,
                mrf_rt_write /* src0 */,
                msg_type,
                BRW_BLORP_RENDERBUFFER_BINDING_TABLE_INDEX,
                mlen,
                0 /* response_length */,
                true /* eot */,
                false /* header present */);

   if (unlikely(INTEL_DEBUG & DEBUG_BLORP)) {
      printf("Native code for BLORP clear:\n");
      brw_dump_compile(&func, stdout, 0, func.next_insn_offset);
      printf("\n");
   }
   return brw_get_program(&func, program_size);
}

extern "C" {
bool
brw_blorp_clear_color(struct intel_context *intel, struct gl_framebuffer *fb)
{
   struct gl_context *ctx = &intel->ctx;
   struct brw_context *brw = brw_context(ctx);

   /* The constant color clear code doesn't work for multisampled surfaces, so
    * we need to support falling back to other clear mechanisms.
    * Unfortunately, our clear code is based on a bitmask that doesn't
    * distinguish individual color attachments, so we walk the attachments to
    * see if any require fallback, and fall back for all if any of them need
    * to.
    */
   for (unsigned buf = 0; buf < ctx->DrawBuffer->_NumColorDrawBuffers; buf++) {
      struct gl_renderbuffer *rb = ctx->DrawBuffer->_ColorDrawBuffers[buf];
      struct intel_renderbuffer *irb = intel_renderbuffer(rb);

      if (irb && irb->mt->msaa_layout != INTEL_MSAA_LAYOUT_NONE)
         return false;
   }

   for (unsigned buf = 0; buf < ctx->DrawBuffer->_NumColorDrawBuffers; buf++) {
      struct gl_renderbuffer *rb = ctx->DrawBuffer->_ColorDrawBuffers[buf];

      /* If this is an ES2 context or GL_ARB_ES2_compatibility is supported,
       * the framebuffer can be complete with some attachments missing.  In
       * this case the _ColorDrawBuffers pointer will be NULL.
       */
      if (rb == NULL)
         continue;

      brw_blorp_clear_params params(brw, fb, rb, ctx->Color.ColorMask[buf]);
      brw_blorp_exec(intel, &params);
   }

   return true;
}

} /* extern "C" */
