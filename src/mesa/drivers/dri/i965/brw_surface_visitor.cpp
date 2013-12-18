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

#include "brw_surface_visitor.h"
#include "brw_fs_surface_visitor.h"
#include "brw_vec4_surface_visitor.h"
#include "brw_context.h"

template<class traits>
brw_surface_visitor<traits>::brw_surface_visitor(visitor *v) :
   v(v)
{
}

template<class traits>
void
brw_surface_visitor<traits>::visit_atomic_counter_intrinsic(ir_call *ir) const
{
   const char *callee = ir->callee->function_name();
   ir_dereference *deref = static_cast<ir_dereference *>(
      ir->actual_parameters.get_head());
   const src_reg offset = v->visit_result(deref);
   const src_reg surface(
      brw_imm_ud(v->stage_prog_data->binding_table.abo_start +
                 deref->variable_referenced()->atomic.buffer_index));
   src_reg tmp;

   if (!strcmp("__intrinsic_atomic_read", callee)) {
      tmp = emit_untyped_read(src_reg(), surface, offset, 1, 1);

   } else if (!strcmp("__intrinsic_atomic_increment", callee)) {
      tmp = emit_untyped_atomic(src_reg(), surface, offset,
                                src_reg(), src_reg(),
                                1, BRW_AOP_INC);

   } else if (!strcmp("__intrinsic_atomic_predecrement", callee)) {
      tmp = emit_untyped_atomic(src_reg(), surface, offset,
                                src_reg(), src_reg(),
                                1, BRW_AOP_PREDEC);
   }

   if (ir->return_deref) {
      dst_reg dst(v->visit_result(ir->return_deref));
      emit_assign_vector(dst, tmp, 1);
   }
}

namespace {
   /**
    * Process the parameters passed to an image intrinsic call.
    */
   template<class traits>
   struct image_intrinsic_parameters {
      typedef typename traits::src_reg src_reg;
      typedef typename traits::dst_reg dst_reg;
      typedef typename traits::visitor visitor;

      image_intrinsic_parameters(visitor *v, ir_call *ir)
      {
         exec_list_iterator it = ir->actual_parameters.iterator();

         image_var = static_cast<ir_dereference *>(it.get())->
            variable_referenced();

         image = visit_next(v, it);
         addr = visit_next(v, it);

         if (image_var->type->fields.image.dimension == GLSL_IMAGE_DIM_MS)
            sample = visit_next(v, it);

         for (int i = 0; it.has_next(); ++i)
            src[i] = visit_next(v, it);

         if (ir->return_deref)
            dst = dst_reg(v->visit_result(ir->return_deref));
      }

      ir_variable *image_var;

      src_reg image;
      src_reg addr;
      src_reg sample;
      src_reg src[2];
      dst_reg dst;

   private:
      src_reg
      visit_next(visitor *v, exec_list_iterator &it) const
      {
         ir_dereference *deref = static_cast<ir_dereference *>(it.get());
         it.next();
         return v->visit_result(deref);
      }
   };

   /**
    * Get the appropriate atomic op for an image atomic intrinsic.
    */
   unsigned
   get_image_atomic_op(const char *callee, ir_variable *image)
   {
      const glsl_base_type base_type = image->type->fields.image.type;

      if (!strcmp("__intrinsic_image_atomic_add", callee))
         return BRW_AOP_ADD;

      else if (!strcmp("__intrinsic_image_atomic_min", callee))
         return (base_type == GLSL_TYPE_UINT ? BRW_AOP_UMIN : BRW_AOP_IMIN);

      else if (!strcmp("__intrinsic_image_atomic_max", callee))
         return (base_type == GLSL_TYPE_UINT ? BRW_AOP_UMAX : BRW_AOP_IMAX);

      else if (!strcmp("__intrinsic_image_atomic_and", callee))
         return BRW_AOP_AND;

      else if (!strcmp("__intrinsic_image_atomic_or", callee))
         return BRW_AOP_OR;

      else if (!strcmp("__intrinsic_image_atomic_xor", callee))
         return BRW_AOP_XOR;

      else if (!strcmp("__intrinsic_image_atomic_exchange", callee))
         return BRW_AOP_MOV;

      else if (!strcmp("__intrinsic_image_atomic_comp_swap", callee))
         return BRW_AOP_CMPWR;

      else
         unreachable();
   }
}

template<class traits>
void
brw_surface_visitor<traits>::visit_image_intrinsic(ir_call *ir) const
{
   image_intrinsic_parameters<traits> p(v, ir);
   const char *callee = ir->callee->function_name();
   const unsigned dims = p.image_var->type->coordinate_components();
   const GLenum format = (p.image_var->image.write_only ? GL_NONE :
                          p.image_var->image.format);
   src_reg tmp;

   if (!strcmp("__intrinsic_image_load", callee))
      tmp = emit_image_load(p.image, p.addr, format, dims);

   else if (!strcmp("__intrinsic_image_store", callee))
      emit_image_store(p.image, p.addr, p.src[0], format, dims);

   else
      tmp = emit_image_atomic(p.image, p.addr, p.src[0], p.src[1],
                              format, get_image_atomic_op(callee, p.image_var),
                              dims);

   if (ir->return_deref) {
      const unsigned size = (ir->return_deref->variable_referenced()->
                             type->components());
      emit_assign_vector(p.dst, tmp, size);
   }
}

template<class traits>
void
brw_surface_visitor<traits>::visit_barrier_intrinsic(ir_call *ir) const
{
   emit_memory_fence();
}

template<class traits>
typename traits::src_reg
brw_surface_visitor<traits>::emit_image_load(src_reg image,
                                             src_reg addr,
                                             GLenum format,
                                             unsigned dims) const
{
   src_reg flag, tmp;

   switch (format) {
   case GL_RGBA32F:
      /* Hardware surface format: RAW */
      flag = emit_coordinate_check(image, addr, dims);
      addr = emit_coordinate_address_calculation(image, addr, dims);
      tmp = emit_untyped_read(flag, image, addr, 1, 4);
      tmp = retype(tmp, BRW_REGISTER_TYPE_F);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RGBA16F:
      flag = emit_coordinate_check(image, addr, dims);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: RAW */
         addr = emit_coordinate_address_calculation(image, addr, dims);
         tmp = emit_untyped_read(flag, image, addr, 1, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
      }

      tmp = emit_convert_from_float(tmp, WRITEMASK_XYZW, 16);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RG32F:
      flag = emit_coordinate_check(image, addr, dims);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
      } else {
         /* Hardware surface format: RAW */
         addr = emit_coordinate_address_calculation(image, addr, dims);
         tmp = emit_untyped_read(flag, image, addr, 1, 2);
      }

      tmp = retype(tmp, BRW_REGISTER_TYPE_F);
      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_RG16F:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16);
      }

      tmp = emit_convert_from_float(tmp, WRITEMASK_XY, 16);
      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_R11F_G11F_B10F:
      /* Hardware surface format: R32_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      tmp = emit_unpack_generic(tmp, 0, 11, 11, 11, 22, 10);
      tmp = emit_convert_from_float(tmp, WRITEMASK_XY, 11,
                                    WRITEMASK_Z, 10);
      tmp = emit_pad(flag, tmp, 3);
      return tmp;

   case GL_R32F:
      /* Hardware surface format: R32_FLOAT */
      tmp = emit_typed_read(flag, image, addr, dims, 4);
      tmp = retype(tmp, BRW_REGISTER_TYPE_F);
      return tmp;

   case GL_R16F:
      /* Hardware surface format: R16_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      tmp = emit_convert_from_float(tmp, WRITEMASK_X, 16);
      tmp = emit_pad(flag, tmp, 1);
      return tmp;

   case GL_RGBA32UI:
      /* Hardware surface format: RAW */
      flag = emit_coordinate_check(image, addr, dims);
      addr = emit_coordinate_address_calculation(image, addr, dims);
      tmp = emit_untyped_read(flag, image, addr, 1, 4);
      tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RGBA16UI:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: RAW */
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         tmp = emit_untyped_read(flag, image, addr, 1, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         tmp = emit_pad(flag, tmp, 4);
      }
      return tmp;

   case GL_RGB10_A2UI:
      /* Hardware surface format: R32_UINT */
      flag = emit_coordinate_check(image, addr, dims);
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      tmp = emit_unpack_generic(tmp, 0, 10, 10, 10, 20, 10, 30, 2);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RGBA8UI:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8B8A8_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: R32_UINT */
         flag = emit_coordinate_check(image, addr, dims);
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 8, 8, 16, 8, 24, 8);
         tmp = emit_pad(flag, tmp, 4);
      }
      return tmp;

   case GL_RG32UI:
      flag = emit_coordinate_check(image, addr, dims);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
      } else {
         /* Hardware surface format: RAW */
         addr = emit_coordinate_address_calculation(image, addr, dims);
         tmp = emit_untyped_read(flag, image, addr, 1, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      }

      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_RG16UI:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16);
         tmp = emit_pad(flag, tmp, 2);
      }
      return tmp;

   case GL_RG8UI:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: R16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 8, 8);
         tmp = emit_pad(flag, tmp, 2);
      }
      return tmp;

   case GL_R32UI:
      /* Hardware surface format: R32_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 4);
      tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      return tmp;

   case GL_R16UI:
      /* Hardware surface format: R16_UINT */
      if (v->brw->is_haswell) {
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 16);
         tmp = emit_pad(flag, tmp, 1);
      }
      return tmp;

   case GL_R8UI:
      /* Hardware surface format: R8_UINT */
      if (v->brw->is_haswell) {
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 8);
         tmp = emit_pad(flag, tmp, 1);
      }
      return tmp;

   case GL_RGBA32I:
      /* Hardware surface format: RAW */
      flag = emit_coordinate_check(image, addr, dims);
      addr = emit_coordinate_address_calculation(image, addr, dims);
      tmp = emit_untyped_read(flag, image, addr, 1, 4);
      tmp = retype(tmp, BRW_REGISTER_TYPE_D);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RGBA16I:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 32, 16, 64, 16, 96, 16);
      } else {
         /* Hardware surface format: RAW */
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         tmp = emit_untyped_read(flag, image, addr, 1, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         tmp = emit_pad(flag, tmp, 4);
      }
      return tmp;

   case GL_RGBA8I:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8B8A8_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 32, 8, 64, 8, 96, 8);
      } else {
         /* Hardware surface format: R32_UINT */
         flag = emit_coordinate_check(image, addr, dims);
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 8, 8, 16, 8, 24, 8);
         tmp = emit_pad(flag, tmp, 4);
      }
      return tmp;

   case GL_RG32I:
      flag = emit_coordinate_check(image, addr, dims);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
      } else {
         /* Hardware surface format: RAW */
         addr = emit_coordinate_address_calculation(image, addr, dims);
         tmp = emit_untyped_read(flag, image, addr, 1, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
      }

      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_RG16I:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 32, 16);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16);
      }

      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_RG8I:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 32, 8);
      } else {
         /* Hardware surface format: R16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 8, 8);
      }

      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_R32I:
      /* Hardware surface format: R32_INT */
      tmp = emit_typed_read(flag, image, addr, dims, 4);
      tmp = retype(tmp, BRW_REGISTER_TYPE_D);
      return tmp;

   case GL_R16I:
      /* Hardware surface format: R16_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_D);
      tmp = emit_unpack_homogeneous(tmp, 0, 16);
      tmp = emit_pad(flag, tmp, 1);
      return tmp;

   case GL_R8I:
      /* Hardware surface format: R8_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_D);
      tmp = emit_unpack_homogeneous(tmp, 0, 8);
      tmp = emit_pad(flag, tmp, 1);
      return tmp;

   case GL_RGBA16:
      flag = emit_coordinate_check(image, addr, dims);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: RAW */
         addr = emit_coordinate_address_calculation(image, addr, dims);
         tmp = emit_untyped_read(flag, image, addr, 1, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
      }

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XYZW, 65535.0);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RGB10_A2:
      /* Hardware surface format: R32_UINT */
      flag = emit_coordinate_check(image, addr, dims);
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      tmp = emit_unpack_generic(tmp, 0, 10, 10, 10, 20, 10, 30, 2);
      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XYZ, 1023.0,
                                     WRITEMASK_W, 3.0);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RGBA8:
      flag = emit_coordinate_check(image, addr, dims);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8B8A8_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 8, 8, 16, 8, 24, 8);
      }

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XYZW, 255.0);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RG16:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16);
      }

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XY, 65535.0);
      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_RG8:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
      } else {
         /* Hardware surface format: R16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 8, 8);
      }

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XY, 255.0);
      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_R16:
      /* Hardware surface format: R16_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_UD);

      if (!v->brw->is_haswell)
         tmp = emit_unpack_homogeneous(tmp, 0, 16);

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_X, 65535.0);
      tmp = emit_pad(flag, tmp, 1);
      return tmp;

   case GL_R8:
      /* Hardware surface format: R8_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_UD);

      if (!v->brw->is_haswell)
         tmp = emit_unpack_homogeneous(tmp, 0, 8);

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_X, 255.0);
      tmp = emit_pad(flag, tmp, 1);
      return tmp;

   case GL_RGBA16_SNORM:
      flag = emit_coordinate_check(image, addr, dims);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 32, 16, 64, 16, 96, 16);
      } else {
         /* Hardware surface format: RAW */
         addr = emit_coordinate_address_calculation(image, addr, dims);
         tmp = emit_untyped_read(flag, image, addr, 1, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
      }

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XYZW, 32767.0);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RGBA8_SNORM:
      flag = emit_coordinate_check(image, addr, dims);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8B8A8_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 4);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 32, 8, 64, 8, 96, 8);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 8, 8, 16, 8, 24, 8);
      }

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XYZW, 127.0);
      tmp = emit_pad(flag, tmp, 4);
      return tmp;

   case GL_RG16_SNORM:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 32, 16);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16);
      }

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XY, 32767.0);
      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_RG8_SNORM:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 2);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 32, 8);
      } else {
         /* Hardware surface format: R16_UINT */
         tmp = emit_typed_read(flag, image, addr, dims, 1);
         tmp = retype(tmp, BRW_REGISTER_TYPE_D);
         tmp = emit_unpack_homogeneous(tmp, 0, 8, 8, 8);
      }

      tmp = emit_convert_from_scaled(tmp, WRITEMASK_XY, 127.0);
      tmp = emit_pad(flag, tmp, 2);
      return tmp;

   case GL_R16_SNORM:
      /* Hardware surface format: R16_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_D);
      tmp = emit_unpack_homogeneous(tmp, 0, 16);
      tmp = emit_convert_from_scaled(tmp, WRITEMASK_X, 32767.0);
      tmp = emit_pad(flag, tmp, 1);
      return tmp;

   case GL_R8_SNORM:
      /* Hardware surface format: R8_UINT */
      tmp = emit_typed_read(flag, image, addr, dims, 1);
      tmp = retype(tmp, BRW_REGISTER_TYPE_D);
      tmp = emit_unpack_homogeneous(tmp, 0, 8);
      tmp = emit_convert_from_scaled(tmp, WRITEMASK_X, 127.0);
      tmp = emit_pad(flag, tmp, 1);
      return tmp;

   default:
      unreachable();
   }
}

template<class traits>
void
brw_surface_visitor<traits>::emit_image_store(src_reg image,
                                              src_reg addr,
                                              src_reg src,
                                              GLenum format,
                                              unsigned dims) const
{
   src_reg flag, tmp;

   switch (format) {
   case GL_NONE:
      emit_typed_write(flag, image, addr, src, dims, 4);
      return;

   case GL_RGBA32F:
      /* Hardware surface format: RAW */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      flag = emit_coordinate_check(image, addr, dims);
      addr = emit_coordinate_address_calculation(image, addr, dims);
      emit_untyped_write(flag, image, addr, tmp, 1, 4);
      return;

   case GL_RGBA16F:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_float(tmp, WRITEMASK_XYZW, 16);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: RAW */
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         emit_untyped_write(flag, image, addr, tmp, 1, 2);
      }
      return;

   case GL_RG32F:
      tmp = retype(src, BRW_REGISTER_TYPE_UD);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: RAW */
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         emit_untyped_write(flag, image, addr, tmp, 1, 2);
      }
      return;

   case GL_RG16F:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_float(tmp, WRITEMASK_XY, 16);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_R11F_G11F_B10F:
      /* Hardware surface format: R32_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_float(tmp, WRITEMASK_XY, 11,
                                  WRITEMASK_Z, 10);
      tmp = emit_pack_generic(tmp, 0, 11, 11, 11, 22, 10);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_R32F:
      /* Hardware surface format: R32_FLOAT */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_R16F:
      /* Hardware surface format: R16_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_float(tmp, WRITEMASK_X, 16);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_RGBA32UI:
      /* Hardware surface format: RAW */
      tmp = retype(src, BRW_REGISTER_TYPE_UD);
      flag = emit_coordinate_check(image, addr, dims);
      addr = emit_coordinate_address_calculation(image, addr, dims);
      emit_untyped_write(flag, image, addr, tmp, 1, 4);
      return;

   case GL_RGBA16UI:
      tmp = retype(src, BRW_REGISTER_TYPE_UD);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: RAW */
         tmp = emit_convert_to_integer(tmp, WRITEMASK_XYZW, 16);
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         emit_untyped_write(flag, image, addr, tmp, 1, 2);
      }
      return;

   case GL_RGB10_A2UI:
      /* Hardware surface format: R32_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_UD);
      tmp = emit_convert_to_integer(tmp, WRITEMASK_XYZ, 10,
                                    WRITEMASK_W, 2);
      tmp = emit_pack_generic(tmp, 0, 10, 10, 10, 20, 10, 30, 2);
      flag = emit_coordinate_check(image, addr, dims);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_RGBA8UI:
      tmp = retype(src, BRW_REGISTER_TYPE_UD);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8B8A8_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_convert_to_integer(tmp, WRITEMASK_XYZW, 8);
         tmp = emit_pack_homogeneous(tmp, 0, 8, 8, 8, 16, 8, 24, 8);
         flag = emit_coordinate_check(image, addr, dims);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_RG32UI:
      tmp = retype(src, BRW_REGISTER_TYPE_UD);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: RAW */
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         emit_untyped_write(flag, image, addr, tmp, 1, 2);
      }
      return;

   case GL_RG16UI:
      tmp = retype(src, BRW_REGISTER_TYPE_UD);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_convert_to_integer(tmp, WRITEMASK_XY, 16);
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_RG8UI:
      tmp = retype(src, BRW_REGISTER_TYPE_UD);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R16_UINT */
         tmp = emit_convert_to_integer(tmp, WRITEMASK_XY, 8);
         tmp = emit_pack_homogeneous(tmp, 0, 8, 8, 8);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_R32UI:
      /* Hardware surface format: R32_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_UD);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_R16UI:
      /* Hardware surface format: R16_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_UD);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_R8UI:
      /* Hardware surface format: R8_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_UD);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_RGBA32I:
      /* Hardware surface format: RAW */
      tmp = retype(src, BRW_REGISTER_TYPE_D);
      flag = emit_coordinate_check(image, addr, dims);
      addr = emit_coordinate_address_calculation(image, addr, dims);
      emit_untyped_write(flag, image, addr, tmp, 1, 4);
      return;

   case GL_RGBA16I:
      tmp = retype(src, BRW_REGISTER_TYPE_D);
      tmp = emit_convert_to_integer(tmp, WRITEMASK_XYZW, 16);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: RAW */
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         emit_untyped_write(flag, image, addr, tmp, 1, 2);
      }
      return;

   case GL_RGBA8I:
      tmp = retype(src, BRW_REGISTER_TYPE_D);
      tmp = emit_convert_to_integer(tmp, WRITEMASK_XYZW, 8);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8B8A8_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 8, 8, 8, 16, 8, 24, 8);
         flag = emit_coordinate_check(image, addr, dims);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_RG32I:
      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         tmp = retype(src, BRW_REGISTER_TYPE_UD);
         tmp = emit_unpack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: RAW */
         tmp = retype(src, BRW_REGISTER_TYPE_D);
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         emit_untyped_write(flag, image, addr, tmp, 1, 2);
      }
      return;

   case GL_RG16I:
      tmp = retype(src, BRW_REGISTER_TYPE_D);
      tmp = emit_convert_to_integer(tmp, WRITEMASK_XY, 16);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_RG8I:
      tmp = retype(src, BRW_REGISTER_TYPE_D);
      tmp = emit_convert_to_integer(tmp, WRITEMASK_XY, 8);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R16_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 8, 8, 8);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_R32I:
      /* Hardware surface format: R32_INT */
      tmp = retype(src, BRW_REGISTER_TYPE_D);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_R16I:
      /* Hardware surface format: R16_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_D);
      tmp = emit_convert_to_integer(tmp, WRITEMASK_X, 16);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_R8I:
      /* Hardware surface format: R8_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_D);
      tmp = emit_convert_to_integer(tmp, WRITEMASK_X, 8);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_RGBA16:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_UD,
                                   WRITEMASK_XYZW, 65535.0);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: RAW */
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         emit_untyped_write(flag, image, addr, tmp, 1, 2);
      }
      return;

   case GL_RGB10_A2:
      /* Hardware surface format: R32_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_UD,
                                   WRITEMASK_XYZ, 1023.0,
                                   WRITEMASK_W, 3.0);
      tmp = emit_pack_generic(tmp, 0, 10, 10, 10, 20, 10, 30, 2);
      flag = emit_coordinate_check(image, addr, dims);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_RGBA8:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_UD,
                                   WRITEMASK_XYZW, 255.0);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8B8A8_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 8, 8, 8, 16, 8, 24, 8);
         flag = emit_coordinate_check(image, addr, dims);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_RG16:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_UD,
                                   WRITEMASK_XY, 65535.0);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_RG8:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_UD,
                                   WRITEMASK_XY, 255.0);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R16_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 8, 8, 8);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_R16:
      /* Hardware surface format: R16_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_UD,
                                   WRITEMASK_X, 65535.0);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_R8:
      /* Hardware surface format: R8_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_UD,
                                   WRITEMASK_X, 255.0);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_RGBA16_SNORM:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_D,
                                   WRITEMASK_XYZW, 32767.0);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16B16A16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: RAW */
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16, 32, 16, 48, 16);
         flag = emit_coordinate_check(image, addr, dims);
         addr = emit_coordinate_address_calculation(image, addr, dims);
         emit_untyped_write(flag, image, addr, tmp, 1, 2);
      }
      return;

   case GL_RGBA8_SNORM:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_D,
                                   WRITEMASK_XYZW, 127.0);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8B8A8_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 4);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 8, 8, 8, 16, 8, 24, 8);
         flag = emit_coordinate_check(image, addr, dims);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_RG16_SNORM:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_D,
                                   WRITEMASK_XY, 32767.0);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R16G16_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R32_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 16, 16, 16);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_RG8_SNORM:
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_D,
                                   WRITEMASK_XY, 127.0);

      if (v->brw->is_haswell) {
         /* Hardware surface format: R8G8_UINT */
         emit_typed_write(flag, image, addr, tmp, dims, 2);
      } else {
         /* Hardware surface format: R16_UINT */
         tmp = emit_pack_homogeneous(tmp, 0, 8, 8, 8);
         emit_typed_write(flag, image, addr, tmp, dims, 1);
      }
      return;

   case GL_R16_SNORM:
      /* Hardware surface format: R16_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_D,
                                   WRITEMASK_X, 32767.0);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   case GL_R8_SNORM:
      /* Hardware surface format: R8_UINT */
      tmp = retype(src, BRW_REGISTER_TYPE_F);
      tmp = emit_convert_to_scaled(tmp, BRW_REGISTER_TYPE_D,
                                   WRITEMASK_X, 127.0);
      emit_typed_write(flag, image, addr, tmp, dims, 1);
      return;

   default:
      unreachable();
   }
}

template<class traits>
typename traits::src_reg
brw_surface_visitor<traits>::emit_image_atomic(src_reg image,
                                               src_reg addr,
                                               src_reg src0,
                                               src_reg src1,
                                               GLenum format, unsigned op,
                                               unsigned dims) const
{
   switch (format) {
   case GL_R32UI:
      /* Hardware surface format: R32_UINT */
      return emit_typed_atomic(src_reg(), image, addr, src0, src1,
                               dims, op);

   case GL_R32I:
      /* Hardware surface format: R32_INT */
      return emit_typed_atomic(src_reg(), image, addr, src0, src1,
                               dims, op);

   default:
      unreachable();
   }
}


/* Since a lot of templatized brw_surface_visitor functions are defined in
 * this file, we need to instantiate all the variants of brw_surface_visitor
 * that we need, so that all the variants of each of those functions gets
 * created.
 */
template class brw_surface_visitor<fs_traits>;
template class brw_surface_visitor<vec4_traits>;


/* TESTING STUFF */
class test_src_reg;
class test_dst_reg
{
public:
   test_dst_reg()
   {
   }

   explicit test_dst_reg(const test_src_reg &)
   {
   }
};

class test_src_reg
{
public:
   test_src_reg()
   {
   }

   explicit test_src_reg(const struct brw_reg &)
   {
   }
};

static inline test_src_reg
retype(test_src_reg reg, unsigned)
{
   return reg;
}

class test_visitor
{
public:
   test_src_reg &visit_result(ir_instruction *ir)
   {
      return r;
   }

   struct brw_stage_prog_data *stage_prog_data;
   struct brw_context *brw;

private:
   test_src_reg r;
};

class test_traits
{
public:
   typedef test_src_reg src_reg;
   typedef test_dst_reg dst_reg;
   typedef test_visitor visitor;
};

template class brw_surface_visitor<test_traits>;
