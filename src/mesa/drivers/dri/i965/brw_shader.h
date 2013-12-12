/*
 * Copyright © 2010 Intel Corporation
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

#include <stdint.h>
#include "brw_defines.h"
#include "brw_reg.h"
#include "glsl/ir.h"

#pragma once

enum register_file {
   BAD_FILE,
   GRF,
   MRF,
   IMM,
   HW_REG, /* a struct brw_reg */
   ATTR,
   UNIFORM, /* prog_data->params[reg] */
};

#ifdef __cplusplus

class backend_reg {
public:
   backend_reg();
   backend_reg(struct brw_reg reg);

   bool is_zero() const;
   bool is_one() const;
   bool is_null() const;

   /** Register file: GRF, MRF, IMM. */
   enum register_file file;

   /**
    * Register number.  For MRF, it's the hardware register.  For
    * GRF, it's a virtual register number until register allocation
    */
   int reg;

   /**
    * Offset from the start of the contiguous register block.
    *
    * For pre-register-allocation GRFs, this is in units of a float per pixel
    * (1 hardware register for SIMD8 mode, or 2 registers for SIMD16 mode).
    * For uniforms, this is in units of 1 float.
    */
   int reg_offset;

   /** Register type.  BRW_REGISTER_TYPE_* */
   int type;
   struct brw_reg fixed_hw_reg;

   /** Value for file == BRW_IMMMEDIATE_FILE */
   union {
      int32_t i;
      uint32_t u;
      float f;
   } imm;
};

class backend_instruction : public exec_node {
public:
   bool is_tex();
   bool is_math();
   bool is_control_flow();
   bool can_do_source_mods();

   /**
    * True if the instruction has side effects other than writing to
    * its destination registers.  You are expected not to reorder or
    * optimize these out unless you know what you are doing.
    */
   bool has_side_effects() const;

   enum opcode opcode; /* BRW_OPCODE_* or FS_OPCODE_* */

   uint32_t predicate;
   bool predicate_inverse;
};

enum instruction_scheduler_mode {
   SCHEDULE_PRE,
   SCHEDULE_PRE_NON_LIFO,
   SCHEDULE_PRE_LIFO,
   SCHEDULE_POST,
};

class backend_visitor : public ir_visitor {
public:

   struct brw_context *brw;
   struct gl_context *ctx;
   struct brw_shader *shader;
   struct gl_shader_program *shader_prog;
   struct gl_program *prog;
   struct brw_stage_prog_data *stage_prog_data;

   /** ralloc context for temporary data used during compile */
   void *mem_ctx;

   /**
    * List of either fs_inst or vec4_instruction (inheriting from
    * backend_instruction)
    */
   exec_list instructions;

   virtual void dump_instruction(backend_instruction *inst) = 0;
   void dump_instructions();

   void assign_common_binding_table_offsets(uint32_t next_binding_table_offset);

   virtual void invalidate_live_intervals() = 0;
};

uint32_t brw_texture_offset(struct gl_context *ctx, ir_constant *offset);

#endif /* __cplusplus */

int brw_type_for_base_type(const struct glsl_type *type);
uint32_t brw_conditional_for_comparison(unsigned int op);
uint32_t brw_math_function(enum opcode op);
const char *brw_instruction_name(enum opcode op);
