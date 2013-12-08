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

#include "brw_fs_surface_visitor.h"

namespace {
   fs_inst &
   exec_all(fs_inst &inst)
   {
      inst.force_writemask_all = true;
      return inst;
   }

   fs_inst &
   exec_half(unsigned half, fs_inst &inst)
   {
      if (half == 1)
         inst.force_sechalf = true;
      else
         inst.force_uncompressed = true;

      return inst;
   }

   fs_inst &
   exec_predicated(backend_reg flag, fs_inst &inst)
   {
      if (flag.file != BAD_FILE) {
         inst.predicate = BRW_PREDICATE_NORMAL;
         inst.flag_subreg = flag.fixed_hw_reg.subnr / 2;
      }

      return inst;
   }

   struct brw_reg
   get_sample_mask(fs_visitor *v)
   {
      if (v->fp->UsesKill) {
         return brw_flag_reg(0, 1);
      } else {
         if (v->brw->gen >= 6)
            return retype(brw_vec1_grf(1, 7), BRW_REGISTER_TYPE_UD);
         else
            return retype(brw_vec1_grf(0, 0), BRW_REGISTER_TYPE_UD);
      }
   }
}

brw_fs_surface_visitor::brw_fs_surface_visitor(fs_visitor *v) :
   brw_surface_visitor(v), v(v)
{
}

fs_inst &
brw_fs_surface_visitor::emit(opcode op, fs_reg dst,
                             fs_reg src0,
                             fs_reg src1,
                             fs_reg src2) const
{
   fs_inst inst(op, dst, src0, src1, src2);

   return *v->emit(inst);
}

fs_reg
brw_fs_surface_visitor::make_grf(unsigned type, unsigned size) const
{
   return fs_reg(GRF, v->virtual_grf_alloc(size), type);
}

fs_reg
brw_fs_surface_visitor::make_mrf(unsigned reg) const
{
   return fs_reg(MRF, reg, BRW_REGISTER_TYPE_UD);
}

void
brw_fs_surface_visitor::emit_assign_vector(
   backend_reg dst, backend_reg src, unsigned size) const
{
   for (unsigned i = 0; i < size; ++i)
      emit(BRW_OPCODE_MOV, offset(dst, i), offset(src, i));
}

/**
 * Copy one of the halves of a SIMD16 vector to a SIMD8 vector.
 */
void
brw_fs_surface_visitor::emit_pack_vector_half(
   fs_reg dst, fs_reg src,
   unsigned i, unsigned size) const
{
   const unsigned w = v->dispatch_width / 8;

   for (unsigned j = 0; j < size; ++j)
      exec_half(i,
                emit(BRW_OPCODE_MOV,
                     half(offset(dst, j / w), j % w),
                     half(offset(src, j), i)));
}

/**
 * Copy a SIMD8 vector to one of the halves of a SIMD16 vector.
 */
void
brw_fs_surface_visitor::emit_unpack_vector_half(
   fs_reg dst, fs_reg src,
   unsigned i, unsigned size) const
{
   const unsigned w = v->dispatch_width / 8;

   for (unsigned j = 0; j < size; ++j)
      exec_half(i,
                emit(BRW_OPCODE_MOV,
                     half(offset(dst, j), i),
                     half(offset(src, j / w), j % w)));
}

/**
 * Initialize the header present in some surface access messages.
 */
void
brw_fs_surface_visitor::emit_surface_header(struct fs_reg dst) const
{
   assert(dst.file == MRF);
   exec_all(exec_half(0, emit(BRW_OPCODE_MOV, dst, 0)));
   exec_all(emit(BRW_OPCODE_MOV, brw_uvec_mrf(1, dst.reg, 7),
                 get_sample_mask(v)));
}

backend_reg
brw_fs_surface_visitor::emit_coordinate_check(
   backend_reg image, backend_reg addr, unsigned dims) const
{
   fs_reg size = offset(image, BRW_IMAGE_PARAM_SIZE_OFFSET);

   for (unsigned i = 0; i < dims; ++i) {
      fs_inst &inst = emit(BRW_OPCODE_CMP, reg_null_d,
                           offset(retype(addr, BRW_REGISTER_TYPE_UD), i),
                           offset(size, i));

      if (i > 0)
         inst.predicate = BRW_PREDICATE_NORMAL;

      inst.conditional_mod = BRW_CONDITIONAL_L;
      inst.flag_subreg = 0;
   }

   return brw_flag_reg(0, 0);
}

backend_reg
brw_fs_surface_visitor::emit_coordinate_address_calculation(
   backend_reg image, backend_reg addr, unsigned dims) const
{
   fs_reg x = retype(offset(addr, 0), BRW_REGISTER_TYPE_UD);
   fs_reg y = retype(offset(addr, 1), BRW_REGISTER_TYPE_UD);
   fs_reg z = retype(offset(addr, 2), BRW_REGISTER_TYPE_UD);
   fs_reg offset_x = offset(image, BRW_IMAGE_PARAM_OFFSET_OFFSET + 0);
   fs_reg offset_y = offset(image, BRW_IMAGE_PARAM_OFFSET_OFFSET + 1);
   fs_reg stride_x = offset(image, BRW_IMAGE_PARAM_STRIDE_OFFSET + 0);
   fs_reg stride_y = offset(image, BRW_IMAGE_PARAM_STRIDE_OFFSET + 1);
   fs_reg stride_z = offset(image, BRW_IMAGE_PARAM_STRIDE_OFFSET + 2);
   fs_reg stride_w = offset(image, BRW_IMAGE_PARAM_STRIDE_OFFSET + 3);
   fs_reg tile_x = offset(image, BRW_IMAGE_PARAM_TILING_OFFSET + 0);
   fs_reg tile_y = offset(image, BRW_IMAGE_PARAM_TILING_OFFSET + 1);
   fs_reg tile_z = offset(image, BRW_IMAGE_PARAM_TILING_OFFSET + 2);
   fs_reg swz_x = offset(image, BRW_IMAGE_PARAM_SWIZZLING_OFFSET + 0);
   fs_reg swz_y = offset(image, BRW_IMAGE_PARAM_SWIZZLING_OFFSET + 1);
   fs_reg high_x = make_grf(BRW_REGISTER_TYPE_UD, 1);
   fs_reg high_y = make_grf(BRW_REGISTER_TYPE_UD, 1);
   fs_reg high_z = make_grf(BRW_REGISTER_TYPE_UD, 1);
   fs_reg dst = make_grf(BRW_REGISTER_TYPE_UD, 1);
   fs_reg zero = make_grf(BRW_REGISTER_TYPE_UD, 1)
      .apply_stride(0);

   exec_all(emit(BRW_OPCODE_MOV, zero, 0));

   /* Shift the coordinates by the fixed surface offset. */
   emit(BRW_OPCODE_ADD, x, x, offset_x);
   if (dims > 1)
      emit(BRW_OPCODE_ADD, y, y, offset_y);

   if (dims > 2) {
      /* Decompose z into a major and a minor index. */
      emit(BRW_OPCODE_SHR, high_z, z, tile_z);
      emit(BRW_OPCODE_BFE, z, tile_z, zero, z);

      /* Calculate the vertical slice offset. */
      emit(BRW_OPCODE_MUL, high_z, stride_w, high_z);
      emit(BRW_OPCODE_ADD, y, y, high_z);

      /* Calculate the horizontal slice offset. */
      emit(BRW_OPCODE_MUL, z, stride_z, z);
      emit(BRW_OPCODE_ADD, x, x, z);
   }

   if (dims > 1) {
      /* Decompose x and y into major and minor indices. */
      emit(BRW_OPCODE_SHR, high_x, x, tile_x);
      emit(BRW_OPCODE_SHR, high_y, y, tile_y);

      emit(BRW_OPCODE_BFE, x, tile_x, zero, x);
      emit(BRW_OPCODE_BFE, y, tile_y, zero, y);

      /* Calculate the pixel index from the start of the tile row.
       * Equivalent to:
       *   dst = (high_x << tile_y << tile_x) + (low_y << tile_x) + low_x
       */
      emit(BRW_OPCODE_SHL, high_x, high_x, tile_y);
      emit(BRW_OPCODE_ADD, dst, high_x, y);
      emit(BRW_OPCODE_SHL, dst, dst, tile_x);
      emit(BRW_OPCODE_ADD, dst, dst, x);

      /* Multiply by the Bpp value. */
      emit(BRW_OPCODE_MUL, dst, dst, stride_x);

      /* Add it to the start offset of the tile row. */
      emit(BRW_OPCODE_MUL, high_y, stride_y, high_y);
      emit(BRW_OPCODE_SHL, high_y, high_y, tile_y);
      emit(BRW_OPCODE_ADD, dst, dst, high_y);

      if (v->brw->has_swizzling) {
         fs_reg bit_x = make_grf(BRW_REGISTER_TYPE_UD, 1);
         fs_reg bit_y = make_grf(BRW_REGISTER_TYPE_UD, 1);

         /* Take into account the two dynamically specified shifts. */
         emit(BRW_OPCODE_SHR, bit_x, dst, swz_x);
         emit(BRW_OPCODE_SHR, bit_y, dst, swz_y);

         /* XOR bit_x and bit_y with bit 6 of the memory address. */
         emit(BRW_OPCODE_XOR, bit_x, bit_x, bit_y);
         emit(BRW_OPCODE_AND, bit_x, bit_x, 1 << 6);
         emit(BRW_OPCODE_XOR, dst, dst, bit_x);
      }

   } else {
      /* Multiply by the Bpp value. */
      emit(BRW_OPCODE_MUL, dst, x, stride_x);
   }

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_untyped_read(
   backend_reg flag, backend_reg surface, backend_reg addr,
   unsigned dims, unsigned size) const
{
   fs_reg dst = make_grf(BRW_REGISTER_TYPE_UD, size);
   unsigned mlen = 0;

   /* Initialize the message header. */
   emit_surface_header(make_mrf(mlen));
   mlen++;

   /* Set the surface read offset. */
   emit_assign_vector(make_mrf(mlen), addr, dims);
   mlen += dims * v->dispatch_width / 8;

   /* Emit the instruction. */
   fs_inst &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_UNTYPED_SURFACE_READ, dst,
                 surface, size));
   inst.base_mrf = 0;
   inst.mlen = mlen;
   inst.regs_written = size;

   return dst;
}

void
brw_fs_surface_visitor::emit_untyped_write(
   backend_reg flag, backend_reg surface, backend_reg addr,
   backend_reg src, unsigned dims, unsigned size) const
{
   unsigned mlen = 0;

   /* Initialize the message header. */
   emit_surface_header(make_mrf(mlen));
   mlen++;

   /* Set the surface write offset. */
   emit_assign_vector(make_mrf(mlen), addr, dims);
   mlen += dims * v->dispatch_width / 8;

   /* Set the source value. */
   emit_assign_vector(make_mrf(mlen), src, size);
   mlen += size * v->dispatch_width / 8;

   /* Emit the instruction. */
   fs_inst &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_UNTYPED_SURFACE_WRITE, fs_reg(),
                 surface, size));
   inst.base_mrf = 0;
   inst.mlen = mlen;
}

backend_reg
brw_fs_surface_visitor::emit_untyped_atomic(
   backend_reg flag, backend_reg surface, backend_reg addr,
   backend_reg src0, backend_reg src1,
   unsigned dims, unsigned op) const
{
   fs_reg dst = make_grf(BRW_REGISTER_TYPE_UD, 1);
   unsigned mlen = 0;

   /* Initialize the message header. */
   emit_surface_header(make_mrf(mlen));
   mlen++;

   /* Set the atomic operation offset. */
   emit_assign_vector(make_mrf(mlen), addr, dims);
   mlen += dims * v->dispatch_width / 8;

   /* Set the atomic operation arguments. */
   if (src0.file != BAD_FILE) {
      emit(BRW_OPCODE_MOV, make_mrf(mlen), src0);
      mlen += v->dispatch_width / 8;
   }

   if (src1.file != BAD_FILE) {
      emit(BRW_OPCODE_MOV, make_mrf(mlen), src1);
      mlen += v->dispatch_width / 8;
   }

   /* Emit the instruction. */
   fs_inst &inst = exec_predicated(
      flag, emit(SHADER_OPCODE_UNTYPED_ATOMIC, dst,
                 surface, op));
   inst.base_mrf = 0;
   inst.mlen = mlen;

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_typed_read(
   backend_reg flag, backend_reg surface, backend_reg addr,
   unsigned dims, unsigned size) const
{
   fs_reg dst = make_grf(BRW_REGISTER_TYPE_UD, size);
   const unsigned w = v->dispatch_width / 8;

   for (unsigned i = 0; i < w; ++i) {
      const unsigned rlen = (size + w - 1) / w;
      fs_reg tmp = make_grf(BRW_REGISTER_TYPE_UD, rlen);
      unsigned mlen = 0;

      /* Initialize the message header. */
      emit_surface_header(make_mrf(mlen));
      mlen++;

      /* Set the surface read address. */
      emit_pack_vector_half(make_mrf(mlen), addr, i, dims);
      mlen += dims;

      /* Emit the instruction. */
      fs_inst &inst = exec_half(i, exec_predicated(flag,
         emit(SHADER_OPCODE_TYPED_SURFACE_READ, tmp,
              surface, size)));
      inst.base_mrf = 0;
      inst.mlen = mlen;
      inst.regs_written = rlen;

      /* Unpack the result. */
      emit_unpack_vector_half(dst, tmp, i, size);
   }

   return dst;
}

void
brw_fs_surface_visitor::emit_typed_write(
   backend_reg flag, backend_reg surface, backend_reg addr,
   backend_reg src, unsigned dims, unsigned size) const
{
   for (unsigned i = 0; i < v->dispatch_width / 8; ++i) {
      unsigned mlen = 0;

      /* Initialize the message header. */
      emit_surface_header(make_mrf(mlen));
      mlen++;

      /* Set the surface write address. */
      emit_pack_vector_half(make_mrf(mlen), addr, i, dims);
      mlen += dims;

      /* Set the source value. */
      emit_pack_vector_half(make_mrf(mlen), src, i, size);
      mlen += size;

      /* Emit the instruction. */
      fs_inst &inst = exec_half(i, exec_predicated(flag,
         emit(SHADER_OPCODE_TYPED_SURFACE_WRITE, fs_reg(),
              surface, size)));
      inst.base_mrf = 0;
      inst.mlen = mlen;
   }
}

backend_reg
brw_fs_surface_visitor::emit_typed_atomic(
   backend_reg flag, backend_reg surface, backend_reg addr,
   backend_reg src0, backend_reg src1,
   unsigned dims, unsigned op) const
{
   fs_reg dst = make_grf(BRW_REGISTER_TYPE_UD, 1);

   for (unsigned i = 0; i < v->dispatch_width / 8; ++i) {
      unsigned mlen = 0;

      /* Initialize the message header. */
      emit_surface_header(make_mrf(mlen));
      mlen++;

      /* Set the atomic operation address. */
      emit_pack_vector_half(make_mrf(mlen), addr, i, dims);
      mlen += dims;

      /* Set the source arguments. */
      if (src0.file != BAD_FILE) {
         emit_pack_vector_half(make_mrf(mlen), src0, i, 1);
         mlen++;
      }

      if (src1.file != BAD_FILE) {
         emit_pack_vector_half(make_mrf(mlen), src1, i, 1);
         mlen++;
      }

      /* Emit the instruction. */
      fs_inst &inst = exec_half(i, exec_predicated(flag,
         emit(SHADER_OPCODE_TYPED_ATOMIC, half(dst, i),
              surface, op)));
      inst.base_mrf = 0;
      inst.mlen = mlen;
   }

   return dst;
}

void
brw_fs_surface_visitor::emit_memory_fence() const
{
   emit(SHADER_OPCODE_MEMORY_FENCE);
}

backend_reg
brw_fs_surface_visitor::emit_pad(
   backend_reg flag, backend_reg src, unsigned size) const
{
   fs_reg dst = make_grf(src.type, 4);

   for (unsigned i = 0; i < 4; ++i) {
      unsigned x = (i == 3 ? 1 : 0);
      fs_reg pad = (src.type == BRW_REGISTER_TYPE_F ?
                    fs_reg(float(x)) : fs_reg(x));

      if (i < size) {
         if (flag.file != BAD_FILE)
            exec_predicated(flag, emit(BRW_OPCODE_SEL, offset(dst, i),
                                       offset(src, i), pad));
         else
            emit(BRW_OPCODE_MOV, offset(dst, i), offset(src, i));

      } else {
         emit(BRW_OPCODE_MOV, offset(dst, i), pad);
      }
   }

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_pack_generic(
   backend_reg src,
   unsigned shift_r, unsigned width_r,
   unsigned shift_g, unsigned width_g,
   unsigned shift_b, unsigned width_b,
   unsigned shift_a, unsigned width_a) const
{
   const unsigned shift[] = { shift_r, shift_g, shift_b, shift_a };
   const unsigned width[] = { width_r, width_g, width_b, width_a };
   const unsigned bits = width_r + width_g + width_b + width_a;
   fs_reg dst = make_grf(BRW_REGISTER_TYPE_UD, bits / 32);
   bool seen[4] = {};

   for (unsigned i = 0; i < Elements(width); ++i) {
      if (width[i]) {
         const unsigned j = shift[i] / 32;
         const unsigned k = shift[i] % 32;
         const unsigned m = (1ull << width[i]) - 1;
         fs_reg tmp = make_grf(BRW_REGISTER_TYPE_UD, 1);

         if (seen[j]) {
            /* Insert the source value into the bit field if we have
             * already written to this dword.
             */
            emit(BRW_OPCODE_MOV, tmp, m << k);
            emit(BRW_OPCODE_BFI2, offset(dst, j),
                 tmp, offset(src, i), offset(dst, j));

         } else {
            /* Otherwise just mask and copy the value over. */
            emit(BRW_OPCODE_AND, offset(dst, j),
                 offset(src, i), m);

            if (k)
               emit(BRW_OPCODE_SHL, offset(dst, j),
                    offset(dst, j), k);

            seen[j] = true;
         }
      }
   }

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_unpack_generic(
   backend_reg src,
   unsigned shift_r, unsigned width_r,
   unsigned shift_g, unsigned width_g,
   unsigned shift_b, unsigned width_b,
   unsigned shift_a, unsigned width_a) const
{
   const unsigned shift[] = { shift_r, shift_g, shift_b, shift_a };
   const unsigned width[] = { width_r, width_g, width_b, width_a };
   const unsigned n = !!width_r + !!width_g + !!width_b + !!width_a;
   fs_reg dst = make_grf(src.type, n);

   for (unsigned i = 0; i < Elements(width); ++i) {
      if (width[i]) {
         /* Discard the most significant bits. */
         emit(BRW_OPCODE_SHL, offset(dst, i),
              offset(src, shift[i] / 32),
              32 - shift[i] % 32 - width[i]);

         /* Shift it back to the least significant bits using an
          * arithmetic shift to get sign extension on signed types.
          */
         emit(BRW_OPCODE_ASR, offset(dst, i),
              offset(dst, i), 32 - width[i]);
      }
   }

   return dst;
}

namespace {
   unsigned
   type_for_width(unsigned width)
   {
      switch (width) {
      case 8:
         return BRW_REGISTER_TYPE_UB;
      case 16:
         return BRW_REGISTER_TYPE_UW;
      case 32:
         return BRW_REGISTER_TYPE_UD;
      default:
         unreachable();
      }
   }
}

backend_reg
brw_fs_surface_visitor::emit_pack_homogeneous(
   backend_reg src,
   unsigned shift_r, unsigned width_r,
   unsigned shift_g, unsigned width_g,
   unsigned shift_b, unsigned width_b,
   unsigned shift_a, unsigned width_a) const
{
   const unsigned shift[] = { shift_r, shift_g, shift_b, shift_a };
   const unsigned width[] = { width_r, width_g, width_b, width_a };
   const unsigned type = type_for_width(width[0]);
   fs_reg dst = make_grf(BRW_REGISTER_TYPE_UD, type_sz(type));
   fs_reg csrc = retype(fs_reg(src), type).apply_stride(4 / type_sz(type));
   fs_reg cdst = retype(dst, type).apply_stride(4 / type_sz(type));
   bool seen[4] = {};

   for (unsigned i = 0; i < 4; ++i) {
      if (width[i]) {
         const unsigned j = shift[i] / 32;
         const unsigned k = shift[i] % 32;

         if (seen[j]) {
            /* Insert the source value into the bit field if we have
             * already written to this dword.
             */
            emit(BRW_OPCODE_MOV, offset(byte_offset(cdst, k / 8), j),
                 offset(csrc, i));

         } else {
            /* Otherwise overwrite the whole dword to make sure that
             * unused fields are initialized to zero.
             */
            emit(BRW_OPCODE_SHL, offset(dst, j),
                 offset(csrc, i), k);

            seen[j] = true;
         }
      }
   }

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_unpack_homogeneous(
   backend_reg src,
   unsigned shift_r, unsigned width_r,
   unsigned shift_g, unsigned width_g,
   unsigned shift_b, unsigned width_b,
   unsigned shift_a, unsigned width_a) const
{
   const unsigned shift[] = { shift_r, shift_g, shift_b, shift_a };
   const unsigned width[] = { width_r, width_g, width_b, width_a };
   const unsigned type = type_for_width(width[0]);
   fs_reg tmp = retype(fs_reg(src), type).apply_stride(4 / type_sz(type));
   fs_reg dst = make_grf(src.type, 4);

   for (unsigned i = 0; i < 4; ++i) {
      if (width[i])
         emit(BRW_OPCODE_MOV,
              offset(dst, i),
              offset(byte_offset(tmp, shift[i] % 32 / 8), shift[i] / 32));
   }

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_convert_to_integer(
   backend_reg src,
   unsigned mask0, unsigned width0,
   unsigned mask1, unsigned width1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const unsigned width[] = { width0, width1 };

   for (unsigned i = 0; i < Elements(mask); ++i) {
      for (unsigned j = 0; j < 4; ++j) {
         if (mask[i] & (1 << j)) {
            const int32_t max = (type_is_signed(src.type) ?
                                 (1 << (width[i] - 1)) - 1 :
                                 (1 << width[i]) - 1);

            /* Clamp to the minimum value. */
            if (type_is_signed(src.type))
               emit(BRW_OPCODE_SEL, offset(src, j),
                    offset(src, j), - max - 1)
               .conditional_mod = BRW_CONDITIONAL_G;

            /* Clamp to the maximum value. */
            emit(BRW_OPCODE_SEL, offset(src, j),
                 offset(src, j), max)
               .conditional_mod = BRW_CONDITIONAL_L;
         }
      }
   }

   return src;
}

backend_reg
brw_fs_surface_visitor::emit_convert_from_scaled(
   backend_reg src,
   unsigned mask0, float scale0,
   unsigned mask1, float scale1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const float scale[] = { scale0, scale1 };
   fs_reg dst = retype(src, BRW_REGISTER_TYPE_F);

   for (unsigned i = 0; i < Elements(mask); ++i) {
      for (unsigned j = 0; j < 4; ++j) {
         if (mask[i] & (1 << j)) {
            /* Convert to float and divide by the normalization
             * constant.
             */
            emit(BRW_OPCODE_MOV, offset(dst, j), offset(src, j));
            emit(BRW_OPCODE_MUL, offset(dst, j), offset(dst, j),
                    fs_reg(1.0f / scale[i]));

            /* Clamp to the minimum value. */
            if (type_is_signed(src.type))
               emit(BRW_OPCODE_SEL, offset(dst, j),
                    offset(dst, j), -1.0f)
                  .conditional_mod = BRW_CONDITIONAL_G;
         }
      }
   }

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_convert_to_scaled(
   backend_reg src, unsigned type,
   unsigned mask0, float scale0,
   unsigned mask1, float scale1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const float scale[] = { scale0, scale1 };
   fs_reg dst = retype(src, type);

   for (unsigned i = 0; i < Elements(mask); ++i) {
      for (unsigned j = 0; j < 4; ++j) {
         if (mask[i] & (1 << j)) {
            /* Clamp to the minimum value. */
            if (type_is_signed(type))
               emit(BRW_OPCODE_SEL, offset(src, j),
                    offset(src, j), -1.0f)
                  .conditional_mod = BRW_CONDITIONAL_G;

            /* Clamp to the maximum value. */
            emit(BRW_OPCODE_SEL, offset(src, j),
                 offset(src, j), 1.0f)
               .conditional_mod = BRW_CONDITIONAL_L;

            /* Multiply by the normalization constant and convert to
             * integer.
             */
            emit(BRW_OPCODE_MUL, offset(src, j), offset(src, j),
                    scale[i]);
            emit(BRW_OPCODE_MOV, offset(dst, j), offset(src, j));
         }
      }
   }

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_convert_from_float(
   backend_reg src,
   unsigned mask0, unsigned width0,
   unsigned mask1, unsigned width1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const unsigned width[] = { width0, width1 };
   fs_reg dst = retype(src, BRW_REGISTER_TYPE_F);

   for (unsigned i = 0; i < Elements(mask); ++i) {
      for (unsigned j = 0; j < 4; ++j) {
         if (mask[i] & (1 << j)) {
            /* Extend 10-bit and 11-bit floating point numbers to 15
             * bits.  This works because they have a 5-bit exponent
             * just like the 16-bit floating point format, and they
             * have no sign bit.
             */
            if (width[i] < 16)
               emit(BRW_OPCODE_SHL, offset(src, j),
                       offset(src, j), 15 - width[i]);

            /* Convert to a 32-bit float. */
            emit(BRW_OPCODE_F16TO32, offset(dst, j), offset(src, j));
         }
      }
   }

   return dst;
}

backend_reg
brw_fs_surface_visitor::emit_convert_to_float(
   backend_reg src,
   unsigned mask0, unsigned width0,
   unsigned mask1, unsigned width1) const
{
   const unsigned mask[] = { mask0, mask1 };
   const unsigned width[] = { width0, width1 };
   fs_reg dst = retype(src, BRW_REGISTER_TYPE_UD);

   for (unsigned i = 0; i < Elements(mask); ++i) {
      for (unsigned j = 0; j < 4; ++j) {
         if (mask[i] & (1 << j)) {
            /* Clamp to the minimum value. */
            if (width[i] < 16)
               emit(BRW_OPCODE_SEL, offset(src, j),
                       offset(src, j), 0.0f)
                  .conditional_mod = BRW_CONDITIONAL_G;

            /* Convert to a 16-bit float. */
            emit(BRW_OPCODE_F32TO16, offset(dst, j), offset(src, j));

            /* Discard the least significant bits to get a floating
             * point number of the requested width.  This works
             * because the 10-bit and 11-bit floating point formats
             * have a 5-bit exponent just like the 16-bit format, and
             * they have no sign bit.
             */
            if (width[i] < 16)
               v->emit(BRW_OPCODE_SHR, offset(dst, j),
                       offset(dst, j), 15 - width[i]);
         }
      }
   }

   return dst;
}
