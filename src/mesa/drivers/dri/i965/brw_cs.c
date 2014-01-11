/*
 * Copyright © 2014 Intel Corporation
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


#include "glsl/ralloc.h"
#include "brw_context.h"
#include "brw_cs.h"
#include "brw_eu.h"
#include "brw_state.h"


static void
do_cs_prog(struct brw_context *brw,
           struct gl_shader_program *prog,
           struct brw_compute_program *fp,
           struct brw_cs_prog_key *key)
{
   /* TODO: this is a stub that just compiles a do-nothing program.  It needs
    * to be replaced with something that compiles the user-provided compute
    * program.
    */
   struct brw_compile func;
   const GLuint *program;
   void *mem_ctx;
   GLuint program_size;
   struct brw_cs_prog_data prog_data;
   struct brw_reg R0 = retype(brw_vec8_grf(0, 0), BRW_REGISTER_TYPE_UD);

   memset(&func, 0, sizeof(func));
   memset(&prog_data, 0, sizeof(prog_data));
   mem_ctx = ralloc_context(NULL);
   brw_init_compile(brw, &func, mem_ctx);
   brw_set_mask_control(&func, BRW_MASK_DISABLE);
   brw_cs_terminate(&func, 0, R0);

   program = brw_get_program(&func, &program_size);

   if (unlikely(INTEL_DEBUG & DEBUG_CS)) {
      int i;

      printf("cs:\n");
      for (i = 0; i < program_size / sizeof(struct brw_instruction); i++)
	 brw_disasm(stdout, &((struct brw_instruction *)program)[i],
		    brw->gen);
      printf("\n");
   }

   brw_upload_cache(&brw->cache, BRW_CS_PROG,
                    key, sizeof(*key),
                    program, program_size,
                    &prog_data, sizeof(prog_data),
                    &brw->cs.base.prog_offset, &brw->cs.prog_data);
   ralloc_free(mem_ctx);
}


static void
brw_cs_populate_key(struct brw_context *brw, struct brw_cs_prog_key *key)
{
   /* BRW_NEW_COMPUTE_PROGRAM */
   const struct brw_compute_program *cp =
      (struct brw_compute_program *) brw->compute_program;

   memset(key, 0, sizeof(*key));

   /* The unique compute program ID */
   key->program_string_id = cp->id;
}


static void
brw_upload_cs_prog(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   struct brw_cs_prog_key key;
   struct brw_compute_program *cp = (struct brw_compute_program *)
      brw->compute_program;

   brw_cs_populate_key(brw, &key);

   if (!brw_search_cache(&brw->cache, BRW_CS_PROG,
                         &key, sizeof(key),
                         &brw->cs.base.prog_offset, &brw->cs.prog_data)) {
      do_cs_prog(brw, ctx->Shader.CurrentProgram[MESA_SHADER_COMPUTE], cp,
                 &key);
   }
   brw->cs.base.prog_data = &brw->cs.prog_data->base;
}


const struct brw_tracked_state brw_cs_prog = {
   .dirty = {
      .brw   = BRW_NEW_COMPUTE_PROGRAM
   },
   .emit = brw_upload_cs_prog
};
