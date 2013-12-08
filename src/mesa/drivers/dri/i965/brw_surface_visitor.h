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
#ifndef BRW_SURFACE_VISITOR_H
#define BRW_SURFACE_VISITOR_H

#include "brw_shader.h"

class brw_surface_visitor {
public:
   brw_surface_visitor(backend_visitor *v);

   /**
    * Lower an atomic counter intrinsic call.
    */
   void
   visit_atomic_counter_intrinsic(ir_call *ir) const;

   /**
    * Lower an image intrinsic call.
    */
   void
   visit_image_intrinsic(ir_call *ir) const;

   /**
    * Lower a memory barrier intrinsic call.
    */
   void
   visit_barrier_intrinsic(ir_call *ir) const;

protected:
   backend_reg
   emit_image_load(backend_reg image, backend_reg addr,
                   GLenum format, unsigned dims) const;

   void
   emit_image_store(backend_reg image, backend_reg addr,
                    backend_reg src,
                    GLenum format, unsigned dims) const;

   backend_reg
   emit_image_atomic(backend_reg image, backend_reg addr,
                     backend_reg src0, backend_reg src1,
                     GLenum format, unsigned op, unsigned dims) const;

   virtual void
   emit_assign_vector(backend_reg dst, backend_reg src,
                      unsigned size) const = 0;

   /**
    * Check if the surface coordinates \p addr are within the bounds
    * of the surface \p image and return the comparison result in a
    * flag register.
    */
   virtual backend_reg
   emit_coordinate_check(backend_reg image, backend_reg addr,
                         unsigned dims) const = 0;

   /**
    * Calculate the memory offset for surface coordinate \p addr.
    */
   virtual backend_reg
   emit_coordinate_address_calculation(backend_reg surface, backend_reg addr,
                                       unsigned dims) const = 0;

   virtual backend_reg
   emit_untyped_read(backend_reg flag, backend_reg surface,
                     backend_reg addr,
                     unsigned dims, unsigned size) const = 0;

   virtual void
   emit_untyped_write(backend_reg flag, backend_reg surface,
                      backend_reg addr, backend_reg src,
                      unsigned dims, unsigned size) const = 0;

   virtual backend_reg
   emit_untyped_atomic(backend_reg flag, backend_reg surface,
                       backend_reg addr,
                       backend_reg src0, backend_reg src1,
                       unsigned dims, unsigned op) const = 0;

   virtual backend_reg
   emit_typed_read(backend_reg flag, backend_reg surface,
                   backend_reg addr,
                   unsigned dims, unsigned size) const = 0;

   virtual void
   emit_typed_write(backend_reg flag, backend_reg surface,
                    backend_reg addr, backend_reg src,
                    unsigned dims, unsigned size) const = 0;

   virtual backend_reg
   emit_typed_atomic(backend_reg flag, backend_reg surface,
                     backend_reg addr,
                     backend_reg src0, backend_reg src1,
                     unsigned dims, unsigned op) const = 0;

   virtual void
   emit_memory_fence() const = 0;

   /**
    * If the flag register evaluates to true, extend the input vector
    * \p src from \p size components to four components padding with
    * (0, 0, 0, 1).  Otherwise discard the input and return
    * (0, 0, 0, 1).
    */
   virtual backend_reg
   emit_pad(backend_reg flag, backend_reg src, unsigned size) const = 0;

   /**
    * Pack up to four vector components into a scalar value using the
    * specified bit field positions.
    */
   virtual backend_reg
   emit_pack_generic(backend_reg src,
                     unsigned shift_r = 0, unsigned width_r = 0,
                     unsigned shift_g = 0, unsigned width_g = 0,
                     unsigned shift_b = 0, unsigned width_b = 0,
                     unsigned shift_a = 0, unsigned width_a = 0) const = 0;

   /**
    * Unpack up to four vector components from a scalar value using the
    * specified bit field positions.
    */
   virtual backend_reg
   emit_unpack_generic(backend_reg src,
                       unsigned shift_r = 0, unsigned width_r = 0,
                       unsigned shift_g = 0, unsigned width_g = 0,
                       unsigned shift_b = 0, unsigned width_b = 0,
                       unsigned shift_a = 0, unsigned width_a = 0) const = 0;

   /**
    * Pack up to four vector components into a scalar value using the
    * specified bit field positions.  The widths are assumed to be
    * equal to each other and to the size of a supported register data
    * type.  The shifts are assumed to be width-aligned.
    */
   virtual backend_reg
   emit_pack_homogeneous(backend_reg src,
                         unsigned shift_r = 0, unsigned width_r = 0,
                         unsigned shift_g = 0, unsigned width_g = 0,
                         unsigned shift_b = 0, unsigned width_b = 0,
                         unsigned shift_a = 0, unsigned width_a = 0) const = 0;

   /**
    * Unpack up to four vector components from a scalar value using
    * the specified bit field positions.  The widths are assumed to be
    * equal to each other and to the size of a supported register data
    * type.  The shifts are assumed to be width-aligned.
    */
   virtual backend_reg
   emit_unpack_homogeneous(backend_reg src,
                           unsigned shift_r = 0, unsigned width_r = 0,
                           unsigned shift_g = 0, unsigned width_g = 0,
                           unsigned shift_b = 0, unsigned width_b = 0,
                           unsigned shift_a = 0, unsigned width_a = 0) const = 0;

   /**
    * Convert to an integer data type of variable width, clamping the
    * source as necessary.  Different width values can be specified
    * for two different subsets of the input components.
    */
   virtual backend_reg
   emit_convert_to_integer(backend_reg src,
                           unsigned mask0 = 0, unsigned width0 = 0,
                           unsigned mask1 = 0, unsigned width1 = 0) const = 0;

   /**
    * Convert from a signed or unsigned normalized fixed point
    * fraction.  Different normalization constants can be specified
    * for two different subsets of the input components.
    */
   virtual backend_reg
   emit_convert_from_scaled(backend_reg src,
                            unsigned mask0 = 0, float scale0 = 0,
                            unsigned mask1 = 0, float scale1 = 0) const = 0;

   /**
    * Convert to a signed or unsigned normalized fixed point fraction.
    * Different normalization constants can be specified for two
    * different subsets of the input components.
    */
   virtual backend_reg
   emit_convert_to_scaled(backend_reg src, unsigned type,
                          unsigned mask0 = 0, float scale0 = 0,
                          unsigned mask1 = 0, float scale1 = 0) const = 0;

   /**
    * Convert from a packed floating point number of variable width.
    * Different width values can be specified for two different
    * subsets of the input components.
    */
   virtual backend_reg
   emit_convert_from_float(backend_reg src,
                           unsigned mask0 = 0, unsigned width0 = 0,
                           unsigned mask1 = 0, unsigned width1 = 0) const = 0;

   /**
    * Convert to a packed floating point number of variable width.
    * Different width values can be specified for two different
    * subsets of the input components.
    */
   virtual backend_reg
   emit_convert_to_float(backend_reg src,
                         unsigned mask0 = 0, unsigned width0 = 0,
                         unsigned mask1 = 0, unsigned width1 = 0) const = 0;

   backend_visitor *v;
};

#endif
