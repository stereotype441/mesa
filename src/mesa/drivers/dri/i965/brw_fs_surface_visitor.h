/*
 * Copyright 2013 Intel Corporation
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
 *
 * Authors:
 *    Francisco Jerez <currojerez@riseup.net>
 */
#ifndef BRW_FS_SURFACE_VISITOR_H
#define BRW_FS_SURFACE_VISITOR_H

#include "brw_backend_traits.h"
#include "brw_surface_visitor.h"
#include "brw_fs.h"

class brw_fs_surface_visitor : public brw_surface_visitor<fs_traits> {
public:
   brw_fs_surface_visitor(fs_visitor *v);

protected:
   fs_inst &
   emit(opcode op, fs_reg dst = fs_reg(),
        fs_reg src0 = fs_reg(),
        fs_reg src1 = fs_reg(),
        fs_reg src2 = fs_reg()) const;

   fs_reg
   make_grf(unsigned type, unsigned size) const;

   fs_reg
   make_mrf(unsigned reg) const;

   virtual void
   emit_assign_vector(backend_reg dst, backend_reg src, unsigned size) const;

   void
   emit_pack_vector_half(fs_reg dst, fs_reg src,
                         unsigned i, unsigned size) const;

   void
   emit_unpack_vector_half(fs_reg dst, fs_reg src,
                           unsigned i, unsigned size) const;

   void
   emit_surface_header(fs_reg dst) const;

   virtual backend_reg
   emit_coordinate_check(backend_reg image, backend_reg addr,
                         unsigned dims) const;

   virtual backend_reg
   emit_coordinate_address_calculation(backend_reg image, backend_reg addr,
                                       unsigned dims) const;

   virtual backend_reg
   emit_untyped_read(backend_reg flag, backend_reg surface,
                     backend_reg addr,
                     unsigned dims, unsigned size) const;

   virtual void
   emit_untyped_write(backend_reg flag, backend_reg surface,
                      backend_reg addr, backend_reg src,
                      unsigned dims, unsigned size) const;

   virtual backend_reg
   emit_untyped_atomic(backend_reg flag, backend_reg surface,
                       backend_reg addr,
                       backend_reg src0, backend_reg src1,
                       unsigned dims, unsigned op) const;

   virtual backend_reg
   emit_typed_read(backend_reg flag, backend_reg surface,
                   backend_reg addr,
                   unsigned dims, unsigned size) const;

   virtual void
   emit_typed_write(backend_reg flag, backend_reg surface,
                    backend_reg addr, backend_reg src,
                    unsigned dims, unsigned size) const;

   virtual backend_reg
   emit_typed_atomic(backend_reg flag, backend_reg surface,
                     backend_reg addr,
                     backend_reg src0, backend_reg src1,
                     unsigned dims, unsigned op) const;

   virtual void
   emit_memory_fence() const;

   virtual backend_reg
   emit_pad(backend_reg flag, backend_reg src, unsigned size) const;

   virtual backend_reg
   emit_pack_generic(backend_reg src,
                     unsigned shift_r = 0, unsigned width_r = 0,
                     unsigned shift_g = 0, unsigned width_g = 0,
                     unsigned shift_b = 0, unsigned width_b = 0,
                     unsigned shift_a = 0, unsigned width_a = 0) const;

   virtual backend_reg
   emit_unpack_generic(backend_reg src,
                       unsigned shift_r = 0, unsigned width_r = 0,
                       unsigned shift_g = 0, unsigned width_g = 0,
                       unsigned shift_b = 0, unsigned width_b = 0,
                       unsigned shift_a = 0, unsigned width_a = 0) const;

   virtual backend_reg
   emit_pack_homogeneous(backend_reg src,
                         unsigned shift_r = 0, unsigned width_r = 0,
                         unsigned shift_g = 0, unsigned width_g = 0,
                         unsigned shift_b = 0, unsigned width_b = 0,
                         unsigned shift_a = 0, unsigned width_a = 0) const;

   virtual backend_reg
   emit_unpack_homogeneous(backend_reg src,
                           unsigned shift_r = 0, unsigned width_r = 0,
                           unsigned shift_g = 0, unsigned width_g = 0,
                           unsigned shift_b = 0, unsigned width_b = 0,
                           unsigned shift_a = 0, unsigned width_a = 0) const;

   virtual backend_reg
   emit_convert_to_integer(backend_reg src,
                           unsigned mask0 = 0, unsigned width0 = 0,
                           unsigned mask1 = 0, unsigned width1 = 0) const;

   virtual backend_reg
   emit_convert_from_scaled(backend_reg src,
                            unsigned mask0 = 0, float scale0 = 0,
                            unsigned mask1 = 0, float scale1 = 0) const;

   virtual backend_reg
   emit_convert_to_scaled(backend_reg src, unsigned type,
                          unsigned mask0 = 0, float scale0 = 0,
                          unsigned mask1 = 0, float scale1 = 0) const;

   virtual backend_reg
   emit_convert_from_float(backend_reg src,
                           unsigned mask0 = 0, unsigned width0 = 0,
                           unsigned mask1 = 0, unsigned width1 = 0) const;

   virtual backend_reg
   emit_convert_to_float(backend_reg src,
                         unsigned mask0 = 0, unsigned width0 = 0,
                         unsigned mask1 = 0, unsigned width1 = 0) const;

   fs_visitor *v;
};

#endif
