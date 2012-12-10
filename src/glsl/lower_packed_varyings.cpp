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
 * \file lower_varyings_to_packed.cpp
 *
 * This lowering pass generates GLSL code that manually packs varyings into
 * vec4 slots, for the benefit of back-ends that don't support packed varyings
 * natively.
 *
 * For example, the following shader:
 *
 *   out mat3x2 foo;  // location=4, location_frac=0
 *   out vec3 bar[2]; // location=5, location_frac=2
 *
 *   main()
 *   {
 *     ...
 *   }
 *
 * Is rewritten to:
 *
 *   mat3x2 foo;
 *   vec3 bar[2];
 *   out vec4 packed4; // location=4, location_frac=0
 *   out vec4 packed5; // location=5, location_frac=0
 *   out vec4 packed6; // location=6, location_frac=0
 *
 *   main()
 *   {
 *     ...
 *     packed4.xy = foo[0];
 *     packed4.zw = foo[1];
 *     packed5.xy = foo[2];
 *     packed5.zw = bar[0].xy;
 *     packed6.x = bar[0].z;
 *     packed6.yzw = bar[1];
 *   }
 *
 * This lowering pass properly handles "double parking" (splitting of a
 * varying across two varying slots).  For example, in the code above, two of
 * the components of bar[0] are stored in packed5, and the remaining component
 * is stored in packed6.
 */

#include "glsl_symbol_table.h"
#include "ir.h"
#include "ir_optimization.h"

class lower_packed_varyings_visitor : public ir_hierarchical_visitor
{
public:
   lower_packed_varyings_visitor(void *mem_ctx, unsigned max_location,
                                 ir_variable_mode mode,
                                 exec_list *main_instructions);

   virtual ir_visitor_status visit(ir_variable *var);
   virtual ir_visitor_status visit_enter(ir_function *);

private:
   unsigned lower_rvalue(ir_rvalue *rvalue, unsigned fine_location,
                         ir_variable *unpacked_var);
   unsigned lower_arraylike(ir_rvalue *rvalue, unsigned array_size,
                            unsigned fine_location,
                            ir_variable *unpacked_var);
   ir_variable *get_packed_varying(unsigned location,
                                   ir_variable *unpacked_var);
   bool needs_lowering(ir_variable *var);

   void * const mem_ctx;
   ir_variable **packed_varyings;
   const ir_variable_mode mode;
   exec_list *main_instructions;
};

lower_packed_varyings_visitor::lower_packed_varyings_visitor(
      void *mem_ctx, unsigned locations_used, ir_variable_mode mode,
      exec_list *main_instructions)
   : mem_ctx(mem_ctx),
     packed_varyings((ir_variable **)
                     rzalloc_array_size(mem_ctx, sizeof(*packed_varyings),
                                        locations_used)),
     mode(mode),
     main_instructions(main_instructions)
{
}

ir_visitor_status
lower_packed_varyings_visitor::visit(ir_variable *var)
{
   if (var->mode != this->mode || var->location == -1 ||
       !this->needs_lowering(var))
      return visit_continue;

   /* Change the old varying into an ordinary global. */
   var->mode = ir_var_auto;

   ir_dereference_variable *deref
      = new(this->mem_ctx) ir_dereference_variable(var);
   this->lower_rvalue(deref, var->location * 4 + var->location_frac, var);

   return visit_continue;
}

ir_visitor_status
lower_packed_varyings_visitor::visit_enter(ir_function *)
{
   /* No need to recurse into functions, since all the variable declarations
    * we need to look at are at top level.
    */
   return visit_continue_with_parent;
}

unsigned
lower_packed_varyings_visitor::lower_rvalue(ir_rvalue *rvalue,
                                            unsigned fine_location,
                                            ir_variable *unpacked_var)
{
   /* FINISHME: Support for "varying" records in GLSL 1.50. */
   assert(!rvalue->type->is_record());

   if (rvalue->type->is_array()) {
      return this->lower_arraylike(rvalue, rvalue->type->array_size(),
                                   fine_location, unpacked_var);
   } else if (rvalue->type->is_matrix()) {
      return this->lower_arraylike(rvalue, rvalue->type->matrix_columns,
                                   fine_location, unpacked_var);
   } else if (rvalue->type->vector_elements + fine_location % 4 > 4) {
      /* The rvalue is going to be "double parked", so split it into two
       * pieces and pack each one separately.
       */
      unsigned left_components = 4 - fine_location % 4;
      unsigned right_components
         = rvalue->type->vector_elements - left_components;
      unsigned left_swizzle_values[4] = { 0, 0, 0, 0 };
      unsigned right_swizzle_values[4] = { 0, 0, 0, 0 };
      for (unsigned i = 0; i < left_components; i++)
         left_swizzle_values[i] = i;
      for (unsigned i = 0; i < right_components; i++)
         right_swizzle_values[i] = i + left_components;
      ir_swizzle *left_swizzle = new(this->mem_ctx)
         ir_swizzle(rvalue, left_swizzle_values, left_components);
      ir_swizzle *right_swizzle = new(this->mem_ctx)
         ir_swizzle(rvalue->clone(this->mem_ctx, NULL), right_swizzle_values,
                    right_components);
      fine_location = this->lower_rvalue(left_swizzle, fine_location,
                                         unpacked_var);
      return this->lower_rvalue(right_swizzle, fine_location, unpacked_var);
   } else {
      /* No special handling is necessary; pack the rvalue into the
       * varying.
       */
      /* TODO: skip vec4s */
      unsigned swizzle_values[4] = { 0, 0, 0, 0 };
      unsigned components = rvalue->type->vector_elements;
      unsigned location = fine_location / 4;
      unsigned location_frac = fine_location % 4;
      for (unsigned i = 0; i < components; ++i)
         swizzle_values[i] = i + location_frac;
      ir_dereference_variable *packed_deref = new(this->mem_ctx)
         ir_dereference_variable(this->get_packed_varying(location,
                                                          unpacked_var));
      ir_swizzle *swizzle = new(this->mem_ctx)
         ir_swizzle(packed_deref, swizzle_values, components);
      if (this->mode == ir_var_out) {
         ir_assignment *assignment = new(this->mem_ctx)
            ir_assignment(swizzle, rvalue);
         this->main_instructions->push_tail(assignment);
      } else {
         ir_assignment *assignment = new(this->mem_ctx)
            ir_assignment(rvalue, swizzle);
         this->main_instructions->push_head(assignment);
      }
      return fine_location + components;
   }
}

unsigned
lower_packed_varyings_visitor::lower_arraylike(ir_rvalue *rvalue,
                                               unsigned array_size,
                                               unsigned fine_location,
                                               ir_variable *unpacked_var)
{
   for (unsigned i = 0; i < array_size; i++) {
      if (i != 0)
         rvalue = rvalue->clone(this->mem_ctx, NULL);
      ir_constant *constant = new(this->mem_ctx) ir_constant(i);
      ir_dereference_array *dereference_array = new(this->mem_ctx)
         ir_dereference_array(rvalue, constant);
      fine_location = this->lower_rvalue(dereference_array, fine_location,
                                         unpacked_var);
   }
   return fine_location;
}

ir_variable *
lower_packed_varyings_visitor::get_packed_varying(unsigned location,
                                                  ir_variable *unpacked_var)
{
   if (this->packed_varyings[location] == NULL) {
      char name[10];
      sprintf(name, "packed%d", location);
      const glsl_type *packed_type;
      switch (unpacked_var->type->get_scalar_type()->base_type) {
      case GLSL_TYPE_UINT:
         packed_type = glsl_type::uvec4_type;
         break;
      case GLSL_TYPE_INT:
         packed_type = glsl_type::ivec4_type;
         break;
      case GLSL_TYPE_FLOAT:
         packed_type = glsl_type::vec4_type;
         break;
      case GLSL_TYPE_BOOL:
         packed_type = glsl_type::bvec4_type;
         break;
      default:
         assert(!"Unexpected varying type while packing");
         packed_type = glsl_type::vec4_type;
         break;
      }
      ir_variable *packed_var = new(this->mem_ctx)
         ir_variable(packed_type, name, this->mode);
      packed_var->centroid = unpacked_var->centroid;
      packed_var->interpolation = unpacked_var->interpolation;
      packed_var->location = location;
      this->base_ir->insert_before(packed_var);
      this->packed_varyings[location] = packed_var;
   }
   return this->packed_varyings[location];
}

bool
lower_packed_varyings_visitor::needs_lowering(ir_variable *var)
{
   /* Things composed of vec4's don't need lowering.  Everything else does. */
   const glsl_type *type = var->type;
   if (type->is_array())
      type = type->fields.array;
   if (type->vector_elements == 4)
      return false;
   return true;
}

void
lower_packed_varyings(void *mem_ctx, unsigned locations_used,
                      ir_variable_mode mode, gl_shader *shader)
{
   exec_list *instructions = shader->ir;
   ir_function *main_func = shader->symbols->get_function("main");
   exec_list void_parameters;
   ir_function_signature *main_func_sig
      = main_func->matching_signature(&void_parameters);
   exec_list *main_instructions = &main_func_sig->body;
   lower_packed_varyings_visitor visitor(mem_ctx, locations_used, mode,
                                         main_instructions);
   visitor.run(instructions);
}
