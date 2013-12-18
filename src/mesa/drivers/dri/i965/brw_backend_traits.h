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

/** \file
 *
 * This file provides traits classes that provide details about the two i965
 * back-ends (the vec4 and the fs backends).  These classes are meant to be
 * used as template parameters so that an algorithm can be generalized to work
 * with both backends.
 *
 * Each trait class provides the following typedefs:
 *
 * - trait::src_reg is the type of the object that represents source
 *   registers.
 *
 * - trait::dst_reg is the type of the object that represents destination
 *   registers.
 *
 * - trait::visitor is the type of the backend visitor class.
 */


#ifndef BRW_BACKEND_TRAITS_H
#define BRW_BACKEND_TRAITS_H

/* Forward declarations of the classes referred to below */

class fs_reg;
class fs_visitor;

namespace brw {

class src_reg;
class dst_reg;
class vec4_visitor;

}


/**
 * Traits of the fs backend.
 */
class fs_traits {
public:
   typedef fs_reg src_reg;
   typedef fs_reg dst_reg;
   typedef fs_visitor visitor;
};


/**
 * Traits of the vec4 backend.
 */
class vec4_traits {
public:
   typedef brw::src_reg src_reg;
   typedef brw::dst_reg dst_reg;
   typedef brw::vec4_visitor visitor;
};


#endif /* BRW_BACKEND_TRAITS_H */
