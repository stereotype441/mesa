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

#include "brw_vec4_surface_visitor.h"

using namespace brw;

namespace {
   vec4_instruction &
   exec_all(vec4_instruction &inst)
   {
      inst.force_writemask_all = true;
      return inst;
   }

   vec4_instruction &
   exec_predicated(backend_reg flag, vec4_instruction &inst)
   {
      if (flag.file != BAD_FILE)
         inst.predicate = BRW_PREDICATE_ALIGN16_ALL4H;

      return inst;
   }
}

brw_vec4_surface_visitor::brw_vec4_surface_visitor(vec4_visitor *v) :
   brw_surface_visitor(v), v(v)
{
}

vec4_instruction &
brw_vec4_surface_visitor::emit(opcode op, dst_reg dst,
                               src_reg src0,
                               src_reg src1,
                               src_reg src2) const
{
   return *v->emit(op, dst, src0, src1, src2);
}

src_reg
brw_vec4_surface_visitor::make_grf(unsigned type, unsigned size) const
{
   const unsigned num_registers = (size + 3) / 4;
   return retype(src_reg(GRF, v->virtual_grf_alloc(num_registers), NULL),
                 type);
}

src_reg
brw_vec4_surface_visitor::make_mrf(unsigned reg) const
{
   return retype(src_reg(MRF, reg, NULL), BRW_REGISTER_TYPE_UD);
}

void
brw_vec4_surface_visitor::emit_assign_vector(
   backend_reg dst, backend_reg src, unsigned size) const
{
   const unsigned mask = (1 << size) - 1;

   emit(BRW_OPCODE_MOV, writemask(dst, mask), src);
}

void
brw_vec4_surface_visitor::emit_assign_with_pad(
   dst_reg dst, src_reg src, unsigned size) const
{
   const unsigned mask = (1 << size) - 1;

   emit(BRW_OPCODE_MOV, writemask(dst, mask), src);

   if (dst.writemask & ~mask)
      emit(BRW_OPCODE_MOV, writemask(dst, ~mask), 0);
}

/**
 * Copy a SIMD4x2 vector to its transpose SIMD8x4 vector.
 */
void
brw_vec4_surface_visitor::emit_assign_to_transpose(
   dst_reg dst, src_reg src, unsigned size) const
{
   for (unsigned i = 0; i < size; ++i) {
      emit(BRW_OPCODE_MOV,
           writemask(offset(dst, i), WRITEMASK_X),
           swizzle(src, BRW_SWIZZLE4(i, i, i, i)));
   }
}

/**
 * Copy a SIMD4x2 vector from its transpose SIMD8x4 vector.
 */
void
brw_vec4_surface_visitor::emit_assign_from_transpose(
   dst_reg dst, src_reg src, unsigned size) const
{
   for (unsigned i = 0; i < size; ++i) {
      emit(BRW_OPCODE_MOV,
           writemask(dst, 1 << i),
           swizzle(offset(src, i), BRW_SWIZZLE_XXXX));
   }
}

/**
 * Initialize the header present in some surface access messages.
 */
void
brw_vec4_surface_visitor::emit_surface_header(struct dst_reg dst) const
{
   assert(dst.file == MRF);
   exec_all(emit(BRW_OPCODE_MOV, dst, 0));

   if (!v->brw->is_haswell) {
      /* The sample mask is used on IVB for the SIMD8 messages that
       * have no SIMD4x2 counterpart.  We only use the two X channels
       * in that case, mask everything else out.
       */
      exec_all(emit(BRW_OPCODE_MOV,
                    brw_writemask(brw_uvec_mrf(4, dst.reg, 4), WRITEMASK_W),
                    0x11));
   }
}

backend_reg
brw_vec4_surface_visitor::emit_coordinate_check(
   backend_reg image, backend_reg addr, unsigned dims) const
{
   src_reg size = offset(image, BRW_IMAGE_PARAM_SIZE_OFFSET / 4);
   struct brw_reg flag = brw_flag_reg(0, 0);

   /* Using swizzle_for_size() in the source values makes sure that
    * the flag register result has valid comparison bits replicated to
    * all four channels and we can use the ALL4H predication mode
    * later on.
    */
   emit(BRW_OPCODE_CMP, brw_writemask(brw_null_reg(), WRITEMASK_XYZW),
        swizzle(retype(addr, BRW_REGISTER_TYPE_UD), swizzle_for_size(dims)),
        swizzle(size, swizzle_for_size(dims)))
      .conditional_mod = BRW_CONDITIONAL_L;

   return flag;
}

backend_reg
brw_vec4_surface_visitor::emit_coordinate_address_calculation(
   backend_reg image, backend_reg addr, unsigned dims) const
{
   const unsigned mask = (1 << dims) - 1;
   src_reg off = offset(image, BRW_IMAGE_PARAM_OFFSET_OFFSET / 4);
   src_reg stride = offset(image, BRW_IMAGE_PARAM_STRIDE_OFFSET / 4);
   src_reg tile = offset(image, BRW_IMAGE_PARAM_TILING_OFFSET / 4);
   src_reg swz = offset(image, BRW_IMAGE_PARAM_SWIZZLING_OFFSET / 4);
   src_reg dst = make_grf(BRW_REGISTER_TYPE_UD, 1);
   src_reg tmp = make_grf(BRW_REGISTER_TYPE_UD, 4);

   /* Shift the coordinates by the fixed surface offset. */
   emit(BRW_OPCODE_ADD, writemask(addr, WRITEMASK_XY & mask),
        addr, off);

   if (dims > 2) {
      /* Decompose z into a major (tmp.w) and a minor (tmp.z)
       * index.
       */
      emit(BRW_OPCODE_SHL, writemask(tmp, WRITEMASK_Z),
           addr, negate(tile));

      emit(BRW_OPCODE_SHR, writemask(tmp, WRITEMASK_Z),
           tmp, negate(tile));

      emit(BRW_OPCODE_SHR, writemask(tmp, WRITEMASK_W),
           swizzle(addr, BRW_SWIZZLE_ZZZZ),
           swizzle(tile, BRW_SWIZZLE_ZZZZ));

      /* Calculate the horizontal (tmp.z) and vertical (tmp.w) slice
       * offset.
       */
      emit(BRW_OPCODE_MUL, writemask(tmp, WRITEMASK_ZW),
           stride, tmp);
      emit(BRW_OPCODE_ADD, writemask(addr, WRITEMASK_XY),
           addr, swizzle(tmp, BRW_SWIZZLE_ZWZW));
   }

   if (dims > 1) {
      /* Calculate the minor x (tmp.x) and y (tmp.y) indices. */
      emit(BRW_OPCODE_SHL, writemask(tmp, WRITEMASK_XY),
           addr, negate(tile));

      emit(BRW_OPCODE_SHR, writemask(tmp, WRITEMASK_XY),
           tmp, negate(tile));

      /* Calculate the major x (tmp.z) and y (tmp.w) indices. */
      emit(BRW_OPCODE_SHR, writemask(tmp, WRITEMASK_ZW),
           swizzle(addr, BRW_SWIZZLE_XYXY),
           swizzle(tile, BRW_SWIZZLE_XYXY));

      /* Multiply the minor indices and the major x index (tmp.x,
       * tmp.y and tmp.w) by the Bpp, and the major y index (tmp.w) by
       * the vertical stride.
       */
      emit(BRW_OPCODE_MUL, writemask(tmp, WRITEMASK_XYZW),
           swizzle(stride, BRW_SWIZZLE_XXXY), tmp);

      /* Multiply by the tile dimensions using two shift instructions.
       * Equivalent to:
       *   minor.y = minor.y << tile.x
       *   major.x = major.x << tile.x << tile.y
       *   major.y = major.y << tile.y
       */
      emit(BRW_OPCODE_SHL, writemask(tmp, WRITEMASK_ZW),
           swizzle(tmp, BRW_SWIZZLE_ZWZW),
           swizzle(tile, BRW_SWIZZLE_YYYY));

      emit(BRW_OPCODE_SHL, writemask(tmp, WRITEMASK_YZ),
           swizzle(tmp, BRW_SWIZZLE_YYZZ),
           swizzle(tile, BRW_SWIZZLE_XXXX));

      /* Add everything up. */
      emit(BRW_OPCODE_ADD, writemask(tmp, WRITEMASK_XY),
           swizzle(tmp, BRW_SWIZZLE_XYXY),
           swizzle(tmp, BRW_SWIZZLE_ZWZW));

      emit(BRW_OPCODE_ADD, writemask(dst, WRITEMASK_X),
           swizzle(tmp, BRW_SWIZZLE_XXXX),
           swizzle(tmp, BRW_SWIZZLE_YYYY));

      if (v->brw->has_swizzling) {
         /* Take into account the two dynamically specified shifts. */
         emit(BRW_OPCODE_SHR, writemask(tmp, WRITEMASK_XY),
              swizzle(dst, BRW_SWIZZLE_XXXX), swz);

         /* XOR tmp.x and tmp.y with bit 6 of the memory address. */
         emit(BRW_OPCODE_XOR, writemask(tmp, WRITEMASK_X),
              swizzle(tmp, BRW_SWIZZLE_XXXX),
              swizzle(tmp, BRW_SWIZZLE_YYYY));

         emit(BRW_OPCODE_AND, writemask(tmp, WRITEMASK_X),
              tmp, 1 << 6);

         emit(BRW_OPCODE_XOR, writemask(dst, WRITEMASK_X),
              dst, tmp);
      }

   } else {
      /* Multiply by the Bpp value. */
      emit(BRW_OPCODE_MUL, writemask(dst, WRITEMASK_X),
           addr, stride);
   }

   return dst;
}

backend_reg
brw_vec4_surface_visitor::emit_untyped_read(
   backend_reg flag, backend_reg surface, backend_reg addr,
   unsigned dims, unsigned size) const
{
   src_reg dst = make_grf(BRW_REGISTER_TYPE_UD, size);
   unsigned mlen = 0;

   /* Set the surface read address. */
   emit_assign_with_pad(make_mrf(mlen), addr, dims);
   mlen++;

   /* Emit the instruction. */
   vec4_instruction &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_UNTYPED_SURFACE_READ, dst,
                 surface, size));
   inst.base_mrf = 0;
   inst.mlen = mlen;

   return dst;
}

void
brw_vec4_surface_visitor::emit_untyped_write(
   backend_reg flag, backend_reg surface, backend_reg addr,
   backend_reg src, unsigned dims, unsigned size) const
{
   const unsigned mask = (v->brw->is_haswell ? (1 << size) - 1 : 1);
   unsigned mlen = 0;

   /* Set the surface write address. */
   if (v->brw->is_haswell) {
      emit_assign_with_pad(make_mrf(mlen), addr, dims);
      mlen++;
   } else {
      emit_assign_to_transpose(make_mrf(mlen), addr, dims);
      mlen += dims;
   }

   /* Set the source value. */
   if (v->brw->is_haswell) {
      emit_assign_with_pad(make_mrf(mlen), src, size);
      mlen++;
   } else {
      emit_assign_to_transpose(make_mrf(mlen), src, size);
      mlen += size;
   }

   /* Emit the instruction.  Note that this is translated into the
    * SIMD8 untyped surface write message on IVB because the
    * hardware lacks a SIMD4x2 counterpart.
    */
   vec4_instruction &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_UNTYPED_SURFACE_WRITE,
                 brw_writemask(brw_null_reg(), mask),
                 surface, size));
   inst.base_mrf = 0;
   inst.mlen = mlen;
}

backend_reg
brw_vec4_surface_visitor::emit_untyped_atomic(
   backend_reg flag, backend_reg surface, backend_reg addr,
   backend_reg src0, backend_reg src1,
   unsigned dims, unsigned op) const
{
   src_reg dst = make_grf(BRW_REGISTER_TYPE_UD, 1);
   unsigned mlen = 0;

   /* Set the atomic operation address. */
   if (v->brw->is_haswell) {
      emit_assign_with_pad(make_mrf(mlen), addr, dims);
      mlen++;
   } else {
      emit_assign_to_transpose(make_mrf(mlen), addr, dims);
      mlen += dims;
   }

   /* Set the source arguments. */
   if (v->brw->is_haswell) {
      if (src0.file != BAD_FILE)
         emit(BRW_OPCODE_MOV, writemask(make_mrf(mlen), WRITEMASK_X),
              src0);

      if (src1.file != BAD_FILE)
         emit(BRW_OPCODE_MOV, writemask(make_mrf(mlen), WRITEMASK_Y),
              swizzle(src1, BRW_SWIZZLE_XXXX));

      mlen++;

   } else {
      if (src0.file != BAD_FILE) {
         emit(BRW_OPCODE_MOV, writemask(make_mrf(mlen), WRITEMASK_X),
              src0);
         mlen++;
      }

      if (src1.file != BAD_FILE) {
         emit(BRW_OPCODE_MOV, writemask(make_mrf(mlen), WRITEMASK_X),
              src1);
         mlen++;
      }
   }

   /* Emit the instruction.  Note that this is translated into the
    * SIMD8 untyped atomic message on IVB because the hardware lacks a
    * SIMD4x2 counterpart.
    */
   vec4_instruction &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_UNTYPED_ATOMIC,
                 writemask(dst, WRITEMASK_X),
                 surface, op));
   inst.base_mrf = 0;
   inst.mlen = mlen;

   return dst;
}

backend_reg
brw_vec4_surface_visitor::emit_typed_read(
   backend_reg flag, backend_reg surface, backend_reg addr,
   unsigned dims, unsigned size) const
{
   const unsigned rlen = size * (v->brw->is_haswell ? 1 : 8);
   src_reg tmp = make_grf(BRW_REGISTER_TYPE_UD, rlen);
   src_reg dst = make_grf(BRW_REGISTER_TYPE_UD, size);
   unsigned mlen = 0;

   /* Initialize the message header. */
   emit_surface_header(make_mrf(mlen));
   mlen++;

   /* Set the surface read address. */
   if (v->brw->is_haswell) {
      emit_assign_with_pad(make_mrf(mlen), addr, dims);
      mlen++;
   } else {
      emit_assign_to_transpose(make_mrf(mlen), addr, dims);
      mlen += dims;
   }

   /* Emit the instruction.  Note that this is translated into the
    * SIMD8 typed surface read message on IVB because the hardware
    * lacks a SIMD4x2 counterpart.
    */
   vec4_instruction &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_TYPED_SURFACE_READ, tmp,
                 surface, size));
   inst.base_mrf = 0;
   inst.mlen = mlen;

   /* Transpose the result. */
   if (v->brw->is_haswell)
      dst = tmp;
   else
      emit_assign_from_transpose(dst, tmp, size);

   return dst;
}

void
brw_vec4_surface_visitor::emit_typed_write(
   backend_reg flag, backend_reg surface, backend_reg addr,
   backend_reg src, unsigned dims, unsigned size) const
{
   unsigned mlen = 0;

   /* Initialize the message header. */
   emit_surface_header(make_mrf(mlen));
   mlen++;

   /* Set the surface write address. */
   if (v->brw->is_haswell) {
      emit_assign_with_pad(make_mrf(mlen), addr, dims);
      mlen++;
   } else {
      emit_assign_to_transpose(make_mrf(mlen), addr, dims);
      mlen += dims;
   }

   /* Set the source value. */
   if (v->brw->is_haswell) {
      emit_assign_with_pad(make_mrf(mlen), src, size);
      mlen++;
   } else {
      emit_assign_to_transpose(make_mrf(mlen), src, size);
      mlen += size;
   }

   /* Emit the instruction.  Note that this is translated into the
    * SIMD8 typed surface write message on IVB because the hardware
    * lacks a SIMD4x2 counterpart.
    */
   vec4_instruction &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_TYPED_SURFACE_WRITE, brw_null_reg(),
                 surface, size));
   inst.base_mrf = 0;
   inst.mlen = mlen;
}

backend_reg
brw_vec4_surface_visitor::emit_typed_atomic(
   backend_reg flag, backend_reg surface, backend_reg addr,
   backend_reg src0, backend_reg src1,
   unsigned dims, unsigned op) const
{
   src_reg dst = make_grf(BRW_REGISTER_TYPE_UD, 1);
   unsigned mlen = 0;

   /* Initialize the message header. */
   emit_surface_header(make_mrf(mlen));
   mlen++;

   /* Set the atomic operation address. */
   if (v->brw->is_haswell) {
      emit_assign_with_pad(make_mrf(mlen), addr, dims);
      mlen++;
   } else {
      emit_assign_to_transpose(make_mrf(mlen), addr, dims);
      mlen += dims;
   }

   /* Set the source arguments. */
   if (v->brw->is_haswell) {
      if (src0.file != BAD_FILE)
         emit(BRW_OPCODE_MOV, writemask(make_mrf(mlen), WRITEMASK_X),
              src0);

      if (src1.file != BAD_FILE)
         emit(BRW_OPCODE_MOV, writemask(make_mrf(mlen), WRITEMASK_Y),
              swizzle(src1, BRW_SWIZZLE_XXXX));

      mlen++;

   } else {
      if (src0.file != BAD_FILE) {
         emit(BRW_OPCODE_MOV, writemask(make_mrf(mlen), WRITEMASK_X),
              src0);
         mlen++;
      }

      if (src1.file != BAD_FILE) {
         emit(BRW_OPCODE_MOV, writemask(make_mrf(mlen), WRITEMASK_X),
              src1);
         mlen++;
      }
   }

   /* Emit the instruction.  Note that this is translated into the
    * SIMD8 typed atomic message on IVB because the hardware lacks a
    * SIMD4x2 counterpart.
    */
   vec4_instruction &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_TYPED_ATOMIC,
                 writemask(dst, WRITEMASK_X),
                 surface, op));
   inst.base_mrf = 0;
   inst.mlen = mlen;

   return dst;
}

void
brw_vec4_surface_visitor::emit_memory_fence() const
{
   emit(SHADER_OPCODE_MEMORY_FENCE);
}

backend_reg
brw_vec4_surface_visitor::emit_pad(
   backend_reg flag, backend_reg src, unsigned size) const
{
   const unsigned src_mask = (1 << size) - 1;
   const unsigned pad_mask = (0xf & ~src_mask);
   struct brw_reg pad = brw_imm_vf4(0, 0, 0, 1);

   if (flag.file != BAD_FILE) {
      src_reg dst = make_grf(src.type, 4);

      emit(BRW_OPCODE_MOV, writemask(dst, WRITEMASK_XYZW), pad);
      exec_predicated(flag, emit(BRW_OPCODE_SEL, writemask(dst, src_mask),
                                 src, dst));
      return dst;

   } else {
      if (pad_mask)
         emit(BRW_OPCODE_MOV, writemask(src, pad_mask), pad);

      return src;
   }
}

backend_reg
brw_vec4_surface_visitor::emit_pack_generic(
   backend_reg src,
   unsigned shift_r, unsigned width_r,
   unsigned shift_g, unsigned width_g,
   unsigned shift_b, unsigned width_b,
   unsigned shift_a, unsigned width_a) const
{
   const unsigned mask = (!!width_r << 0 | !!width_g << 1 |
                          !!width_b << 2 | !!width_a << 3);
   const bool homogeneous = ((!width_g || width_r == width_g) &&
                             (!width_b || width_g == width_b) &&
                             (!width_a || width_b == width_a));
   const unsigned bits = width_r + width_g + width_b + width_a;
   src_reg shift = make_grf(BRW_REGISTER_TYPE_UD, 4);

   /* Shift left to discard the most significant bits. */
   emit(BRW_OPCODE_MOV, writemask(shift, mask),
        (homogeneous ? brw_imm_ud(32 - width_r) :
         brw_imm_vf4(32 - width_r, 32 - width_g,
                     32 - width_b, 32 - width_a)));

   emit(BRW_OPCODE_SHL, writemask(src, mask), src, shift);

   /* Shift right to the final bit field positions. */
   emit(BRW_OPCODE_MOV, writemask(shift, mask),
        brw_imm_vf4(32 - shift_r % 32 - width_r,
                    32 - shift_g % 32 - width_g,
                    32 - shift_b % 32 - width_b,
                    32 - shift_a % 32 - width_a));

   emit(BRW_OPCODE_SHR, writemask(src, mask), src, shift);

   /* Add everything up. */
   if (mask >> 2)
      emit(BRW_OPCODE_OR,
           writemask(src, WRITEMASK_XY),
           swizzle(src, BRW_SWIZZLE_XZXZ),
           swizzle(src, (mask >> 3 ? BRW_SWIZZLE_YWYW :
                         BRW_SWIZZLE_YZYZ)));

   if (mask >> 1 && bits <= 32)
      emit(BRW_OPCODE_OR,
           writemask(src, WRITEMASK_X),
           swizzle(src, BRW_SWIZZLE_XXXX),
           swizzle(src, BRW_SWIZZLE_YYYY));

   return src;
}

backend_reg
brw_vec4_surface_visitor::emit_unpack_generic(
   backend_reg src,
   unsigned shift_r, unsigned width_r,
   unsigned shift_g, unsigned width_g,
   unsigned shift_b, unsigned width_b,
   unsigned shift_a, unsigned width_a) const
{
   const unsigned mask = (!!width_r << 0 | !!width_g << 1 |
                          !!width_b << 2 | !!width_a << 3);
   const bool homogeneous = ((!width_g || width_r == width_g) &&
                             (!width_b || width_g == width_b) &&
                             (!width_a || width_b == width_a));
   src_reg shift = make_grf(BRW_REGISTER_TYPE_UD, 4);
   src_reg dst = make_grf(src.type, 4);

   /* Shift left to discard the most significant bits. */
   emit(BRW_OPCODE_MOV, writemask(shift, mask),
        brw_imm_vf4(32 - shift_r % 32 - width_r,
                    32 - shift_g % 32 - width_g,
                    32 - shift_b % 32 - width_b,
                    32 - shift_a % 32 - width_a));

   emit(BRW_OPCODE_SHL, writemask(dst, mask),
        swizzle(src, BRW_SWIZZLE4(shift_r / 32, shift_g / 32,
                                  shift_b / 32, shift_a / 32)),
        shift);

   /* Shift back to the least significant bits using an arithmetic
    * shift to get sign extension on signed types.
    */
   emit(BRW_OPCODE_MOV, writemask(shift, mask),
        (homogeneous ? brw_imm_ud(32 - width_r) :
         brw_imm_vf4(32 - width_r, 32 - width_g,
                     32 - width_b, 32 - width_a)));

   emit(BRW_OPCODE_ASR, writemask(dst, mask), dst, shift);

   return dst;
}

backend_reg
brw_vec4_surface_visitor::emit_pack_homogeneous(
   backend_reg src,
   unsigned shift_r, unsigned width_r,
   unsigned shift_g, unsigned width_g,
   unsigned shift_b, unsigned width_b,
   unsigned shift_a, unsigned width_a) const
{
   /* We could do the same with less instructions if we had some way
    * to use Align1 addressing in the VEC4 visitor.  Just use the
    * general path for now...
    */
   return emit_pack_generic(src, shift_r, width_r, shift_g, width_g,
                            shift_b, width_b, shift_a, width_a);
}

backend_reg
brw_vec4_surface_visitor::emit_unpack_homogeneous(
   backend_reg src,
   unsigned shift_r, unsigned width_r,
   unsigned shift_g, unsigned width_g,
   unsigned shift_b, unsigned width_b,
   unsigned shift_a, unsigned width_a) const
{
   /* We could do the same with less instructions if we had some way
    * to use Align1 addressing in the VEC4 visitor.  Just use the
    * general path for now...
    */
   return emit_unpack_generic(src, shift_r, width_r, shift_g, width_g,
                              shift_b, width_b, shift_a, width_a);
}

backend_reg
brw_vec4_surface_visitor::emit_convert_to_integer(
   backend_reg src,
   unsigned mask0, unsigned width0,
   unsigned mask1, unsigned width1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const unsigned width[] = { width0, width1 };

   for (unsigned i = 0; i < Elements(mask); ++i) {
      if (mask[i]) {
         const int32_t max = (type_is_signed(src.type) ?
                              (1 << (width[i] - 1)) - 1 :
                              (1 << width[i]) - 1);

         /* Clamp to the minimum value. */
         if (type_is_signed(src.type))
            emit(BRW_OPCODE_SEL, writemask(src, mask[i]),
                 src, - max - 1)
               .conditional_mod = BRW_CONDITIONAL_G;

         /* Clamp to the maximum value. */
         emit(BRW_OPCODE_SEL, writemask(src, mask[i]),
              src, max)
            .conditional_mod = BRW_CONDITIONAL_L;
      }
   }

   return src;
}

backend_reg
brw_vec4_surface_visitor::emit_convert_from_scaled(
   backend_reg src,
   unsigned mask0, float scale0,
   unsigned mask1, float scale1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const unsigned full_mask = mask0 | mask1;
   const float scale[] = { scale0, scale1 };
   src_reg dst = retype(src, BRW_REGISTER_TYPE_F);

   /* Convert to float. */
   emit(BRW_OPCODE_MOV, writemask(dst, full_mask), src);

   /* Divide by the normalization constants. */
   for (unsigned i = 0; i < Elements(mask); ++i) {
      if (mask[i])
         emit(BRW_OPCODE_MUL, writemask(dst, mask[i]),
              dst, 1.0f / scale[i]);
   }

   /* Clamp to the minimum value. */
   if (type_is_signed(src.type))
      emit(BRW_OPCODE_SEL, writemask(dst, full_mask),
           dst, -1.0f)
         .conditional_mod = BRW_CONDITIONAL_G;

   return dst;
}

backend_reg
brw_vec4_surface_visitor::emit_convert_to_scaled(
   backend_reg src, unsigned type,
   unsigned mask0, float scale0,
   unsigned mask1, float scale1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const unsigned full_mask = mask0 | mask1;
   const float scale[] = { scale0, scale1 };
   src_reg dst = retype(src, type);

   /* Clamp to the minimum value. */
   if (type_is_signed(type))
      emit(BRW_OPCODE_SEL, writemask(src, full_mask),
           src, -1.0f)
         .conditional_mod = BRW_CONDITIONAL_G;

   /* Clamp to the maximum value. */
   emit(BRW_OPCODE_SEL, writemask(src, full_mask),
        src, 1.0f)
      .conditional_mod = BRW_CONDITIONAL_L;

   /* Multiply by the normalization constants. */
   for (unsigned i = 0; i < Elements(mask); ++i) {
      if (mask[i])
         emit(BRW_OPCODE_MUL, writemask(src, mask[i]),
              src, scale[i]);
   }

   /* Convert to integer. */
   emit(BRW_OPCODE_MOV, writemask(dst, full_mask), src);

   return dst;
}

backend_reg
brw_vec4_surface_visitor::emit_convert_from_float(
   backend_reg src,
   unsigned mask0, unsigned width0,
   unsigned mask1, unsigned width1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const unsigned full_mask = mask0 | mask1;
   const unsigned width[] = { width0, width1 };
   src_reg dst = retype(src, BRW_REGISTER_TYPE_F);

   /* Extend 10-bit and 11-bit floating point numbers to 15 bits.
    * This works because they have a 5-bit exponent just like the
    * 16-bit floating point format, and they have no sign bit.
    */
   for (unsigned i = 0; i < Elements(mask); ++i) {
      if (mask[i] && width[i] < 16)
         emit(BRW_OPCODE_SHL, writemask(src, mask[i]),
              src, 15 - width[i]);
   }

   /* Convert to 32-bit floating point. */
   emit(BRW_OPCODE_F16TO32, writemask(dst, full_mask), src);

   return dst;
}

backend_reg
brw_vec4_surface_visitor::emit_convert_to_float(
   backend_reg src,
   unsigned mask0, unsigned width0,
   unsigned mask1, unsigned width1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const unsigned width[] = { width0, width1 };
   const unsigned full_mask = mask0 | mask1;
   const unsigned clamp_mask = ((width0 < 16 ? mask0 : 0) |
                                (width1 < 16 ? mask1 : 0));
   src_reg dst = retype(src, BRW_REGISTER_TYPE_UD);

   /* Clamp to the minimum value. */
   if (clamp_mask)
      emit(BRW_OPCODE_SEL, writemask(src, clamp_mask),
           src, 0.0f)
         .conditional_mod = BRW_CONDITIONAL_G;

   /* Convert to 16-bit floating-point. */
   emit(BRW_OPCODE_F32TO16, writemask(dst, full_mask), src);

   /* Discard the least significant bits to get floating point numbers
    * of the requested width.  This works because the 10-bit and
    * 11-bit floating point formats have a 5-bit exponent just like
    * the 16-bit format, and they have no sign bit.
    */
   for (unsigned i = 0; i < Elements(mask); ++i) {
      if (mask[i] && width[i] < 16)
         v->emit(BRW_OPCODE_SHR, writemask(dst, mask[i]),
                 dst, 15 - width[i]);
   }

   return dst;
}
