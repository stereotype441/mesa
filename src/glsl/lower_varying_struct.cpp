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
 * \file lower_varying_struct.cpp
 *
 * This lowering pass replaces a varying whose type is a struct (or an array
 * of structs) with equivalent varyings representing the structure elements,
 * and modifies code that refers to the structs to refer to the new varyings
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

namespace {

/**
 * Visitor that lowers accesses to varying structs.
 */
class lower_varying_structs_visitor
{
public:
   lower_varying_structs_visitor();
   void lower_
};

lower_varying_structs_visitor::lower_varying_structs_visitor()
{
}

void
lower_varying_structs_visitor::run(exec_list *instructions)
{
   foreach_list (node, instructions) {
      ir_variable *var = ((ir_instruction *) node)->as_variable();
      if (var == NULL)
         continue;

