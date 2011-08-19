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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file lower_clip_distance.cpp
 *
 * This pass accounts for the difference between the way
 * gl_ClipDistance is declared in standard GLSL (as an array of
 * floats), and the way it is frequently implemented in hardware (as
 * an array of vec4s, with four clip distances packed into each).
 *
 * The declaration of gl_ClipDistance is replaced with a declaration
 * of gl_ClipDistanceMESA, and any references to gl_ClipDistance are
 * translated to refer to gl_ClipDistanceMESA with the appropriate
 * swizzling of array indices.  For instance:
 *
 *   gl_ClipDistance[i]
 *
 * is translated into:
 *
 *   gl_ClipDistanceMESA[i/4][i%4]
 */

#include "ir_hierarchical_visitor.h"
#include "ir.h"

class lower_clip_distance_visitor : public ir_hierarchical_visitor {
public:
   lower_clip_distance_visitor()
      : progress(false), old_clip_distance_var(NULL),
        new_clip_distance_var(NULL)
   {
   }

   virtual ir_visitor_status visit(ir_variable *);
   void create_indices(ir_rvalue*, ir_rvalue *&, ir_rvalue *&);
   virtual ir_visitor_status visit_leave(ir_dereference_array *);

   bool progress;
   ir_variable *old_clip_distance_var;
   ir_variable *new_clip_distance_var;
};


ir_visitor_status
lower_clip_distance_visitor::visit(ir_variable *ir)
{
   /* No point in looking for the declaration of gl_ClipDistance if
    * we've already found it. */
   if (this->old_clip_distance_var)
      return visit_continue;

   if (strcmp(ir->name, "gl_ClipDistance") == 0) {
      this->progress = true;
      this->old_clip_distance_var = ir;
      assert (ir->type->is_array());
      assert (ir->type->element_type() == glsl_type::float_type);
      unsigned new_size = (ir->type->array_size() + 3) / 4;

      /* Clone the old var so that we inherit all of its properties */
      this->new_clip_distance_var = ir->clone(ralloc_parent(ir), NULL);

      /* And change the properties that we need to change */
      this->new_clip_distance_var->name
         = ralloc_strdup(this->new_clip_distance_var, "gl_ClipDistanceMESA");
      this->new_clip_distance_var->type
         = glsl_type::get_array_instance(glsl_type::vec4_type, new_size);
      this->new_clip_distance_var->max_array_access = ir->max_array_access / 4;

      ir->replace_with(this->new_clip_distance_var);
   }
   return visit_continue;
}


void
lower_clip_distance_visitor::create_indices(ir_rvalue *old_index,
                                            ir_rvalue *&inner_index,
                                            ir_rvalue *&outer_index)
{
   assert (old_index->type == glsl_type::int_type); /* TODO: can I rely on this? */

   void *ctx = ralloc_parent(old_index);

   /* TODO: optimize the constant case */

   /* Create a variable to hold the value of old_index (so that we
    * don't compute it twice) */
   ir_variable *old_index_var = new(ctx) ir_variable(
      glsl_type::int_type, "clip_distance_index", ir_var_temporary);
   this->base_ir->insert_before(old_index_var);
   this->base_ir->insert_before(new(ctx) ir_assignment(
      new(ctx) ir_dereference_variable(old_index_var), old_index));

   /* Create the expression clip_distance_index / 4 */
   inner_index = new(ctx) ir_expression(
      ir_binop_div, glsl_type::int_type,
      new(ctx) ir_dereference_variable(old_index_var),
      new(ctx) ir_constant(4));

   /* Create the expression clip_distance_index % 4.  This is tricky
    * since ir_binop_mod doesn't support ints.  TODO: will it soon? */
   outer_index = new(ctx) ir_expression(
      ir_unop_f2i, glsl_type::int_type,
      new(ctx) ir_expression(
         ir_binop_mod, glsl_type::float_type,
         new(ctx) ir_expression(
            ir_unop_i2f, glsl_type::float_type,
            new(ctx) ir_dereference_variable(old_index_var)),
         new(ctx) ir_constant(float(4.0))));
}


ir_visitor_status
lower_clip_distance_visitor::visit_leave(ir_dereference_array *ir)
{
   /* If the gl_ClipDistance var hasn't been declared yet, then
    * there's no way this deref can refer to it. */
   if (!this->old_clip_distance_var)
      return visit_continue;

   ir_dereference_variable *old_var_ref = ir->array->as_dereference_variable();
   if (old_var_ref && old_var_ref->variable_referenced()
       == this->old_clip_distance_var) {
      this->progress = true;
      ir_rvalue *inner_index;
      ir_rvalue *outer_index;
      this->create_indices(ir->array_index, inner_index, outer_index);
      void *mem_ctx = ralloc_parent(ir);
      ir->array = new(mem_ctx) ir_dereference_array(this->new_clip_distance_var,
                                                    inner_index);
      ir->array_index = outer_index;
   }

   return visit_continue;
}


bool
lower_clip_distance(exec_list *instructions)
{
   /* TODO: remove this once I'm confident */
   validate_ir_tree(instructions);

   lower_clip_distance_visitor v;

   visit_list_elements(&v, instructions);

   /* TODO: remove this once I'm confident */
   validate_ir_tree(instructions);

   return v.progress;
}
