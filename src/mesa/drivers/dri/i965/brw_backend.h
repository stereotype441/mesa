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

#pragma once

#include "glsl/ir.h"

class backend_visitor_common : public ir_visitor
{
public:
   explicit backend_visitor_common(struct brw_compile *p);

   struct brw_compile * const p;
   struct brw_context * const brw;
};

template<class policy>
class backend_visitor : public backend_visitor_common
{
public:
   typedef typename policy::brw_gen_compile brw_gen_compile;
   typedef typename policy::gl_gen_program gl_gen_program;
   static const gl_shader_type MESA_SHADER_GEN = policy::MESA_SHADER_GEN;

   explicit backend_visitor(brw_gen_compile *c, struct gl_shader_program *prog);

   brw_gen_compile * const c;
   const gl_gen_program * const gp;
};

template<class policy>
backend_visitor<policy>::backend_visitor(brw_gen_compile *c,
                                         struct gl_shader_program *prog)
   : backend_visitor_common(&c->func),
     c(c),
     gp((gl_gen_program *) prog->_LinkedShaders[MESA_SHADER_GEN]->Program)
{
}
