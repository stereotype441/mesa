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
 *     for (int i = 0; i < 4; i++) {
 *       foo[i].x = 1.0;
 *       for (int j = 0; j < 3; j++) {
 *         foo[i].y[j] = 1.0;
 *       }
 *     }
 *   }
 *
 * Is rewritten to the equivalent of*:
 *
 *   out float foo_x[4];
 *   out float foo_y[12];
 *
 *   main()
 *   {
 *     for (int i = 0; i < 4; i++) {
 *       foo_x[i] = 1.0;
 *       for (int j = 0; j < 3; j++) {
 *         foo_y[i * 3 + j] = 1.0;
 *       }
 *     }
 *   }
 *
 * *The newly generated varyings use "." in their names rather than "_".
 * Although this would be illegal in GLSL, it is fine in GLSL IR, and aids in
 * debugging by making it clear that these varyings are the result of
 * lowering.
 *
 * Note that in the case where there is an array of structs, and the struct
 * itself contains an array, the lowered varying is a flat array rather than
 * an array of arrays.  Rationale: arrays of arrays are prohibited in GLSL, so
 * some back-ends may not implement them properly.  Also, varying packing code
 * assumes arrays are one-dimensional.
 */


#include "lower_varying_structs.h"
#include "program/hash_table.h"
#include "ir.h"
#include "link_varyings.h"
#include "main/mtypes.h"
#include "ir_hierarchical_visitor.h"


namespace {



/**
 * Recursive data structure indicating how an old varying (whose type contains
 * structs) has been decomposed into new varyings without structs.  It
 * contains pointers to the newly generating varyings.
 */
class varying_decomposition
{
};


} /* anonymous namespace */


lower_varying_structs_visitor::lower_varying_structs_visitor(
   gl_shader *shader, ir_variable_mode mode)
   : mode(mode),
     decompositions(hash_table_ctor(0, hash_table_pointer_hash,
                                    hash_table_pointer_compare))
{
}


void
delete_decomposition(const void *key, void *data, void *closure)
{
   (void) key;
   (void) closure;

   delete (varying_decomposition *) data;
}


lower_varying_structs_visitor::~lower_varying_structs_visitor()
{
   hash_table_call_foreach(this->decompositions, delete_decomposition, NULL);
   hash_table_dtor(this->decompositions);
}


ir_visitor_status
lower_varying_structs_visitor::visit(ir_variable *variable)
{
   if (variable->mode != this->mode)
      return visit_continue;

   const glsl_type *type = variable->type;
   while (type->base_type == GLSL_TYPE_ARRAY)
      type = type->fields.array;
   if (type->base_type != GLSL_TYPE_STRUCT)
      return visit_continue;

   assert(false); /* TODO */
}


ir_visitor_status
lower_varying_structs_visitor::visit(ir_dereference_variable *deref)
{
   varying_decomposition *decomposition = (varying_decomposition *)
      hash_table_find(this->decompositions, deref->variable_referenced());
   if (decomposition == NULL)
      return visit_continue;

   assert(false); /* TODO */
}


ir_visitor_status
lower_varying_structs_visitor::visit(ir_dereference_array *deref)
{
   assert(false); /* TODO */
}


ir_visitor_status
lower_varying_structs_visitor::visit(ir_dereference_record *deref)
{
   assert(false); /* TODO */
}


ir_visitor_status
lower_varying_structs_visitor::visit(ir_assignment *assignment)
{
   assert(false); /* TODO */
}


ir_visitor_status
lower_varying_structs_visitor::visit(ir_call *call)
{
   assert(false); /* TODO */
}
