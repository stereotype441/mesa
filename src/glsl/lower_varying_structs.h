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
 * \file lower_varying_structs.h
 *
 * Header for lower_varying_structs.cpp
 */

#pragma once
#ifndef LOWER_VARYING_STRUCTS_H
#define LOWER_VARYING_STRUCTS_H


#include "ir_hierarchical_visitor.h"
#include "ir.h"


struct gl_shader;
struct hash_table;


class lower_varying_structs_visitor : public ir_hierarchical_visitor
{
public:
   explicit lower_varying_structs_visitor(gl_shader *shader,
                                          ir_variable_mode mode);
   ~lower_varying_structs_visitor();

   virtual ir_visitor_status visit(ir_variable *variable);
   virtual ir_visitor_status visit(ir_dereference_variable *deref);
   virtual ir_visitor_status visit(ir_dereference_array *deref);
   virtual ir_visitor_status visit(ir_dereference_record *deref);
   virtual ir_visitor_status visit(ir_assignment *assignment);
   virtual ir_visitor_status visit(ir_call *call);

private:
   ir_variable_mode mode;
   hash_table *decompositions;
};


#endif /* LOWER_VARYING_STRUCTS_H */
