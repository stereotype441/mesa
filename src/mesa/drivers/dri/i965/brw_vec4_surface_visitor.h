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
#ifndef BRW_VEC4_SURFACE_VISITOR_H
#define BRW_VEC4_SURFACE_VISITOR_H

#include "brw_backend_traits.h"
#include "brw_surface_visitor.h"
#include "brw_vec4.h"

class brw_vec4_surface_visitor : public brw_surface_visitor<vec4_traits> {
public:
   brw_vec4_surface_visitor(brw::vec4_visitor *v);

protected:
   brw::vec4_instruction &
   emit(opcode op, brw::dst_reg dst = brw::dst_reg(),
        brw::src_reg src0 = brw::src_reg(),
        brw::src_reg src1 = brw::src_reg(),
        brw::src_reg src2 = brw::src_reg()) const;

   brw::src_reg
   make_grf(unsigned type, unsigned size) const;

   brw::src_reg
   make_mrf(unsigned reg) const;

   virtual void
   emit_assign_vector(dst_reg dst, src_reg src, unsigned size) const;

   void
   emit_assign_with_pad(brw::dst_reg dst, brw::src_reg src,
                        unsigned size) const;

   void
   emit_assign_to_transpose(brw::dst_reg dst, brw::src_reg src,
                            unsigned size) const;

   void
   emit_assign_from_transpose(brw::dst_reg dst, brw::src_reg src,
                              unsigned size) const;

   void
   emit_surface_header(brw::dst_reg dst) const;

   virtual src_reg
   emit_coordinate_check(src_reg image, src_reg addr,
                         unsigned dims) const;

   virtual src_reg
   emit_coordinate_address_calculation(src_reg image, src_reg addr,
                                       unsigned dims) const;

   virtual src_reg
   emit_untyped_read(src_reg flag, src_reg surface,
                     src_reg addr,
                     unsigned dims, unsigned size) const;

   virtual void
   emit_untyped_write(src_reg flag, src_reg surface,
                      src_reg addr, src_reg src,
                      unsigned dims, unsigned size) const;

   virtual src_reg
   emit_untyped_atomic(src_reg flag, src_reg surface,
                       src_reg addr,
                       src_reg src0, src_reg src1,
                       unsigned dims, unsigned op) const;

   virtual src_reg
   emit_typed_read(src_reg flag, src_reg surface,
                   src_reg addr,
                   unsigned dims, unsigned size) const;

   virtual void
   emit_typed_write(src_reg flag, src_reg surface,
                    src_reg addr, src_reg src,
                    unsigned dims, unsigned size) const;

   virtual src_reg
   emit_typed_atomic(src_reg flag, src_reg surface,
                     src_reg addr,
                     src_reg src0, src_reg src1,
                     unsigned dims, unsigned op) const;

   virtual void
   emit_memory_fence() const;

   virtual src_reg
   emit_pad(src_reg flag, src_reg src, unsigned size) const;

   virtual src_reg
   emit_pack_generic(src_reg src,
                     unsigned shift_r = 0, unsigned width_r = 0,
                     unsigned shift_g = 0, unsigned width_g = 0,
                     unsigned shift_b = 0, unsigned width_b = 0,
                     unsigned shift_a = 0, unsigned width_a = 0) const;

   virtual src_reg
   emit_unpack_generic(src_reg src,
                       unsigned shift_r = 0, unsigned width_r = 0,
                       unsigned shift_g = 0, unsigned width_g = 0,
                       unsigned shift_b = 0, unsigned width_b = 0,
                       unsigned shift_a = 0, unsigned width_a = 0) const;

   virtual src_reg
   emit_pack_homogeneous(src_reg src,
                         unsigned shift_r = 0, unsigned width_r = 0,
                         unsigned shift_g = 0, unsigned width_g = 0,
                         unsigned shift_b = 0, unsigned width_b = 0,
                         unsigned shift_a = 0, unsigned width_a = 0) const;

   virtual src_reg
   emit_unpack_homogeneous(src_reg src,
                           unsigned shift_r = 0, unsigned width_r = 0,
                           unsigned shift_g = 0, unsigned width_g = 0,
                           unsigned shift_b = 0, unsigned width_b = 0,
                           unsigned shift_a = 0, unsigned width_a = 0) const;

   virtual src_reg
   emit_convert_to_integer(src_reg src,
                           unsigned mask0 = 0, unsigned width0 = 0,
                           unsigned mask1 = 0, unsigned width1 = 0) const;

   virtual src_reg
   emit_convert_from_scaled(src_reg src,
                            unsigned mask0 = 0, float scale0 = 0,
                            unsigned mask1 = 0, float scale1 = 0) const;

   virtual src_reg
   emit_convert_to_scaled(src_reg src, unsigned type,
                          unsigned mask0 = 0, float scale0 = 0,
                          unsigned mask1 = 0, float scale1 = 0) const;

   virtual src_reg
   emit_convert_from_float(src_reg src,
                           unsigned mask0 = 0, unsigned width0 = 0,
                           unsigned mask1 = 0, unsigned width1 = 0) const;

   virtual src_reg
   emit_convert_to_float(src_reg src,
                         unsigned mask0 = 0, unsigned width0 = 0,
                         unsigned mask1 = 0, unsigned width1 = 0) const;
};

#endif
