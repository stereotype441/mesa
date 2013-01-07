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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file lower_varying_structs.cpp
 *
 * This lowering pass replaces varyings whose type is a struct (or an array of
 * structs) with equivalent varyings representing the structure elements, and
 * modifies code that refers to the structs to refer to the new varyings
 * instead.
 *
 * For example, the following shader:
 *
 *   struct Foo
 *   {
 *     float x;
 *     float y[3];
 *   };
 *   out Foo foo[4];
 *
 *   main()
 *   {
 *     ...
 *   }
 *
 * Is rewritten to the equivalent of*:
 *
 *   struct Foo
 *   {
 *     float x;
 *     float y[3];
 *   };
 *   Foo foo[4];
 *   out float foo_x[4];
 *   out float foo_y[12];
 *
 *   main()
 *   {
 *     ...
 *     for (uint i = 0; i < 4u; i++) {
 *       foo_x[i] = foo[i].x;
 *       for (uint j = 0; j < 3u; j++) {
 *         foo_y[i * 3 + j] = foo[i].y[j];
 *       }
 *     }
 *   }
 *
 * *The actual GLSL IR generated is slightly more verbose than this in order
 * to avoid complicating the algorithm.  But it is equivalent after
 * optimization.
 *
 * Note that in the case where there is an array of structs, and the struct
 * itself contains an array, the lowered varying is a flat array rather than
 * an array of arrays.  Rationale: arrays of arrays are prohibited in GLSL, so
 * some back-ends may not implement them properly.  Also, varying packing code
 * assumes arrays are one-dimensional.
 */

#include "glsl_types.h"
#include "ir.h"
#include "link_varyings.h"

namespace {


/**
 * Class that maintains state for a given shader while lowering a varying
 * struct.
 */
class shader_state
{
public:
   shader_state(/* TODO: add args */);
   shader_state(const shader_state &prev_state, unsigned array_length);
   shader_state(const shader_state &prev_state, const char *field);
   ir_variable *base_case(const glsl_type *type, const char *name,
                          ir_variable_mode mode, unsigned multiplicity) const;
   ir_rvalue *clone_index() const;
   ir_rvalue *clone_rvalue() const;

private:
   void *mem_ctx;
   ir_rvalue *index;
   ir_rvalue *rvalue;
   exec_list *toplevel;
   exec_list *statements;
};


shader_state::shader_state(const shader_state &prev_state,
                           unsigned array_length)
   : mem_ctx(prev_state.mem_ctx),
     toplevel(prev_state.toplevel)
{
   ir_variable *loop_var = new(this->mem_ctx)
      ir_variable(glsl_type::uint_type, "i", ir_var_auto);
   prev_state.statements->push_tail(loop_var);
   ir_loop *loop = new(this->mem_ctx) ir_loop();
   prev_state.statements->push_tail(loop);
   this->statements = &loop->body_instructions;
   ir_expression *loop_condition = new(this->mem_ctx)
      ir_expression(ir_binop_gequal,
                    new(this->mem_ctx) ir_dereference_variable(loop_var),
                    new(this->mem_ctx) ir_constant(array_length));
   ir_if *if_stmt = new(this->mem_ctx) ir_if(loop_condition);
   ir_jump *break_stmt = new(this->mem_ctx)
      ir_loop_jump(ir_loop_jump::jump_break);
   if_stmt->then_instructions.push_tail(break_stmt);
   this->statements->push_tail(if_stmt);
   ir_variable *index_var = new(this->mem_ctx)
      ir_variable(glsl_type::uint_type, "index", ir_var_auto);
   this->statements->push_head(index_var);
   ir_rvalue *scaled_old_index = new(this->mem_ctx)
      ir_expression(ir_binop_mul, prev_state.clone_index(),
                    new(this->mem_ctx) ir_constant(array_length));
   ir_rvalue *index_value = new(this->mem_ctx)
      ir_expression(ir_binop_add, scaled_old_index,
                    new(this->mem_ctx) ir_dereference_variable(loop_var));
   ir_assignment *index_assignment = new(this->mem_ctx)
      ir_assignment(new(this->mem_ctx) ir_dereference_variable(index_var),
                    index_value);
   this->statements->push_tail(index_assignment);
   this->index = new(this->mem_ctx) ir_dereference_variable(index_var);
   this->rvalue = new(this->mem_ctx)
      ir_dereference_array(
         prev_state.rvalue,
         new(this->mem_ctx) ir_dereference_variable(loop_var));
}


shader_state::shader_state(const shader_state &prev_state, const char *field)
   : mem_ctx(prev_state.mem_ctx),
     index(prev_state.index),
     toplevel(prev_state.toplevel),
     statements(prev_state.statements)
{
   this->rvalue = new(this->mem_ctx)
      ir_dereference_record(prev_state.rvalue, field);
}


ir_variable *
shader_state::base_case(const glsl_type *type, const char *name,
                        ir_variable_mode mode, unsigned multiplicity) const
{
   const glsl_type *new_varying_type;
   if (multiplicity == 1)
      new_varying_type = type;
   else
      new_varying_type = glsl_type::get_array_instance(type, multiplicity);
   ir_variable *new_varying = new(this->mem_ctx)
      ir_variable(new_varying_type, name, mode);
   this->toplevel->push_head(new_varying);
   ir_rvalue *varying_deref = new(this->mem_ctx)
      ir_dereference_variable(new_varying);
   if (multiplicity != 1) {
      varying_deref = new(this->mem_ctx)
         ir_dereference_array(varying_deref, this->clone_index());
   }
   ir_assignment *assignment;
   if (mode == ir_var_out) {
      assignment = new(this->mem_ctx)
         ir_assignment(varying_deref, this->clone_rvalue());
   } else {
      assignment = new(this->mem_ctx)
         ir_assignment(this->clone_rvalue(), varying_deref);
   }
   this->statements->push_tail(assignment);
   return new_varying;
}


ir_rvalue *
shader_state::clone_index() const
{
   return this->index->clone(this->mem_ctx, NULL);
}


ir_rvalue *
shader_state::clone_rvalue() const
{
   return this->rvalue->clone(this->mem_ctx, NULL);
}


/**
 * Class that maintains state while lowering accesses to varying structs.
 */
class lower_varying_structs
{
public:
   lower_varying_structs(varying_matches *matches, gl_shader *producer,
                         gl_shader *consumer);
   ~lower_varying_structs();
   void lower_varying(ir_variable *producer_var, ir_variable *consumer_var);
   void update_shaders();
private:
   void lower_rvalue(const glsl_type *type, const char *name,
                     unsigned multiplicity, const shader_state &producer_state,
                     const shader_state &consumer_state);

   void *mem_ctx;

   varying_matches *matches;
   //producer;
   //void *producer_ctx;
   //exec_list new_producer_statements;
   //exec_list new_producer_variables;
   //consumer;
   //void *consumer_ctx;
   //exec_list new_consumer_statements;
   //exec_list new_consumer_variables;
};


void
lower_varying_structs::lower_rvalue(const glsl_type *type, const char *name,
                                    unsigned multiplicity,
                                    const shader_state &producer_state,
                                    const shader_state &consumer_state)
{
   switch (type->base_type) {
   case GLSL_TYPE_ARRAY: {
      shader_state new_producer_state(producer_state, type->length);
      shader_state new_consumer_state(consumer_state, type->length);
      this->lower_rvalue(type->fields.array, name, multiplicity * type->length, new_producer_state, new_consumer_state);
      break;
   }
   case GLSL_TYPE_STRUCT:
      for (unsigned i = 0; i < type->length; i++) {
         shader_state new_producer_state(producer_state, type->fields.structure[i].name);
         shader_state new_consumer_state(consumer_state, type->fields.structure[i].name);
         const char *member_name
            = ralloc_asprintf(this->mem_ctx, "%s.%s", name,
                              type->fields.structure[i].name);
         this->lower_rvalue(type->fields.array, member_name, multiplicity, new_producer_state, new_consumer_state);
      }
      break;
   default: {
      ir_variable *producer_var
         = producer_state.base_case(type, name, ir_var_out, multiplicity);
      ir_variable *consumer_var
         = consumer_state.base_case(type, name, ir_var_in, multiplicity);
      if (consumer_var)
         matches->record(producer_var, consumer_var);
      break;
   }
   }
}


} /* anonymous namespace */
