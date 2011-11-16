/*
 * Copyright © 2011 Intel Corporation
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

extern "C" {
#include "main/macros.h"
#include "program/register_allocate.h"
} /* extern "C" */

#include "brw_vec4.h"
#include "glsl/ir_print_visitor.h"

using namespace brw;

namespace brw {

reg_allocator::reg_allocator(int first_non_payload_grf, int virtual_grf_count,
                             const int *virtual_grf_sizes,
                             const exec_list &instructions,
                             fail_tracker *fail_notify)
   : mem_ctx(ralloc_context(NULL)),
     first_non_payload_grf(first_non_payload_grf),
     virtual_grf_count(virtual_grf_count),
     virtual_grf_sizes(virtual_grf_sizes),
     instructions(instructions),
     fail_notify(fail_notify)
{
}

reg_allocator::~reg_allocator()
{
   ralloc_free(this->mem_ctx);
}

void
reg_allocator::assign(int *reg_hw_locations, reg *reg) const
{
   if (reg->file == GRF) {
      reg->reg = reg_hw_locations[reg->reg];
   }
}

int
reg_allocator::allocate_trivial(reg_allocator *allocator)
{
   int hw_reg_mapping[allocator->virtual_grf_count];
   bool virtual_grf_used[allocator->virtual_grf_count];
   int i;
   int next;

   /* Calculate which virtual GRFs are actually in use after whatever
    * optimization passes have occurred.
    */
   for (int i = 0; i < allocator->virtual_grf_count; i++) {
      virtual_grf_used[i] = false;
   }

   foreach_iter(exec_list_iterator, iter, allocator->instructions) {
      vec4_instruction *inst = (vec4_instruction *)iter.get();

      if (inst->dst.file == GRF)
	 virtual_grf_used[inst->dst.reg] = true;

      for (int i = 0; i < 3; i++) {
	 if (inst->src[i].file == GRF)
	    virtual_grf_used[inst->src[i].reg] = true;
      }
   }

   hw_reg_mapping[0] = allocator->first_non_payload_grf;
   next = hw_reg_mapping[0] + allocator->virtual_grf_sizes[0];
   for (i = 1; i < allocator->virtual_grf_count; i++) {
      if (virtual_grf_used[i]) {
	 hw_reg_mapping[i] = next;
	 next += allocator->virtual_grf_sizes[i];
      }
   }
   int total_grf = next;

   foreach_iter(exec_list_iterator, iter, allocator->instructions) {
      vec4_instruction *inst = (vec4_instruction *)iter.get();

      allocator->assign(hw_reg_mapping, &inst->dst);
      allocator->assign(hw_reg_mapping, &inst->src[0]);
      allocator->assign(hw_reg_mapping, &inst->src[1]);
      allocator->assign(hw_reg_mapping, &inst->src[2]);
   }

   if (total_grf > BRW_MAX_GRF) {
      allocator->fail_notify->fail(
           "Ran out of regs on trivial allocator (%d/%d)\n",
	   total_grf, BRW_MAX_GRF);
   }

   return total_grf;
}

void
reg_allocator::alloc_reg_set_for_classes(reg_allocator *allocator,
                                         int *class_sizes,
                                         int class_count,
                                         int base_reg_count)
{
   /* Compute the total number of registers across all classes. */
   int ra_reg_count = 0;
   for (int i = 0; i < class_count; i++) {
      ra_reg_count += base_reg_count - (class_sizes[i] - 1);
   }

   ralloc_free(allocator->ra_reg_to_grf);
   allocator->ra_reg_to_grf = ralloc_array(allocator->mem_ctx, uint8_t,
                                           ra_reg_count);
   ralloc_free(allocator->regs);
   allocator->regs = ra_alloc_reg_set(ra_reg_count);
   ralloc_free(allocator->classes);
   allocator->classes = ralloc_array(allocator->mem_ctx, int, class_count + 1);

   /* Now, add the registers to their classes, and add the conflicts
    * between them and the base GRF registers (and also each other).
    */
   int reg = 0;
   for (int i = 0; i < class_count; i++) {
      int class_reg_count = base_reg_count - (class_sizes[i] - 1);
      allocator->classes[i] = ra_alloc_reg_class(allocator->regs);

      for (int j = 0; j < class_reg_count; j++) {
	 ra_class_add_reg(allocator->regs, allocator->classes[i], reg);

	 allocator->ra_reg_to_grf[reg] = j;

	 for (int base_reg = j;
	      base_reg < j + class_sizes[i];
	      base_reg++) {
	    ra_add_transitive_reg_conflict(allocator->regs, base_reg, reg);
	 }

	 reg++;
      }
   }
   assert(reg == ra_reg_count);

   ra_set_finalize(allocator->regs);
}

int
reg_allocator::allocate(reg_allocator *allocator,
                        const live_interval_data *live_intervals)
{
   int hw_reg_mapping[allocator->virtual_grf_count];
   int first_assigned_grf = allocator->first_non_payload_grf;
   int base_reg_count = BRW_MAX_GRF - first_assigned_grf;
   int class_sizes[base_reg_count];
   int class_count = 0;

   /* Using the trivial allocator can be useful in debugging undefined
    * register access as a result of broken optimization passes.
    */
   if (0) {
      return allocate_trivial(allocator);
   }

   /* Set up the register classes.
    *
    * The base registers store a vec4.  However, we'll need larger
    * storage for arrays, structures, and matrices, which will be sets
    * of contiguous registers.
    */
   class_sizes[class_count++] = 1;

   for (int r = 0; r < allocator->virtual_grf_count; r++) {
      int i;

      for (i = 0; i < class_count; i++) {
	 if (class_sizes[i] == allocator->virtual_grf_sizes[r])
	    break;
      }
      if (i == class_count) {
	 if (allocator->virtual_grf_sizes[r] >= base_reg_count) {
	    allocator->fail_notify->fail(
               "Object too large to register allocate.\n");
	 }

	 class_sizes[class_count++] = allocator->virtual_grf_sizes[r];
      }
   }

   allocator->alloc_reg_set_for_classes(allocator, class_sizes, class_count,
                                        base_reg_count);

   struct ra_graph *g =
      ra_alloc_interference_graph(allocator->regs,
                                  allocator->virtual_grf_count);

   for (int i = 0; i < allocator->virtual_grf_count; i++) {
      for (int c = 0; c < class_count; c++) {
	 if (class_sizes[c] == allocator->virtual_grf_sizes[i]) {
	    ra_set_node_class(g, i, allocator->classes[c]);
	    break;
	 }
      }

      for (int j = 0; j < i; j++) {
	 if (live_intervals->virtual_grf_interferes(i, j)) {
	    ra_add_node_interference(g, i, j);
	 }
      }
   }

   if (!ra_allocate_no_spills(g)) {
      ralloc_free(g);
      allocator->fail_notify->fail("No register spilling support yet\n");
      return 0;
   }

   /* Get the chosen virtual registers for each node, and map virtual
    * regs in the register classes back down to real hardware reg
    * numbers.
    */
   int total_grf = first_assigned_grf;
   for (int i = 0; i < allocator->virtual_grf_count; i++) {
      int reg = ra_get_node_reg(g, i);

      hw_reg_mapping[i] = first_assigned_grf + allocator->ra_reg_to_grf[reg];
      total_grf = MAX2(total_grf,
                       hw_reg_mapping[i] + allocator->virtual_grf_sizes[i]);
   }

   foreach_list(node, &allocator->instructions) {
      vec4_instruction *inst = (vec4_instruction *)node;

      allocator->assign(hw_reg_mapping, &inst->dst);
      allocator->assign(hw_reg_mapping, &inst->src[0]);
      allocator->assign(hw_reg_mapping, &inst->src[1]);
      allocator->assign(hw_reg_mapping, &inst->src[2]);
   }

   ralloc_free(g);

   return total_grf;
}

} /* namespace brw */
