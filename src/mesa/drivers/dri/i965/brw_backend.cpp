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

#include "brw_backend.h"
#include "brw_eu.h"
#include "brw_context.h"

backend_visitor_common::backend_visitor_common(struct brw_compile *p,
                                               struct gl_shader_program *prog,
                                               struct brw_shader *shader)
   : p(p),
     brw(p->brw),
     prog(prog),
     intel(&brw->intel),
     ctx(&intel->ctx),
     mem_ctx(ralloc_context(NULL)),
     shader(shader),
     failed(false),
     variable_ht(hash_table_ctor(0,
                                 hash_table_pointer_hash,
                                 hash_table_pointer_compare)),
     max_grf(intel->gen >= 7 ? GEN7_MRF_HACK_START : BRW_MAX_GRF),
     current_annotation(NULL),
     base_ir(NULL),
     virtual_grf_sizes(NULL),
     virtual_grf_array_size(0),
     virtual_grf_def(NULL),
     virtual_grf_use(NULL)
{
}

backend_visitor_common::~backend_visitor_common()
{
   ralloc_free(this->mem_ctx);
   hash_table_dtor(this->variable_ht);
}
