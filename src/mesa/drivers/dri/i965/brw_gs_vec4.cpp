#include "brw_vec4.h"
extern "C" {
#include "brw_state.h"
}

using namespace brw;

namespace brw {

struct gs_vec4_prog_key
{
   GLbitfield64 attrs;
   unsigned num_vertices:3;
   unsigned urb_entry_read_length:6;
   unsigned userclip_active:1;
   unsigned need_gs_prog:1;
};

class gs_vec4_compiler : public vec4_generator
{
public:
   gs_vec4_compiler(brw_compile *p, const gs_vec4_prog_key *key);

   void run();

   const void *get_prog_data() const
   {
      return &this->prog_data;
   }

   unsigned get_prog_data_size() const
   {
      return sizeof(this->prog_data);
   }

   static const unsigned MAX_NUM_VERTICES = 4;

protected:
   virtual bool get_debug_flag() const;
   virtual const char *get_debug_name() const;

private:
   int setup_payload();
   void emit_code();

   const gs_vec4_prog_key * const key;
   unsigned urb_entry_read_length;

   dst_reg r0;
   src_reg vertex_data[MAX_NUM_VERTICES];
   brw_gs_prog_data prog_data;
};

bool
gs_vec4_compiler::get_debug_flag() const
{
   return INTEL_DEBUG & DEBUG_GS;
}

const char *
gs_vec4_compiler::get_debug_name() const
{
   return "geometry shader (for transform feedback)";
}

gs_vec4_compiler::gs_vec4_compiler(brw_compile *p, const gs_vec4_prog_key *key)
   : vec4_generator(p),
     key(key)
{
   assert (1 <= key->num_vertices && key->num_vertices <= MAX_NUM_VERTICES);
   memset(&prog_data, 0, sizeof(prog_data));

   brw_vue_map vue_map;
   brw_compute_vue_map(&vue_map, intel, key->userclip_active, key->attrs);
   urb_entry_read_length = (vue_map.num_slots + 1)/2;
}

int
gs_vec4_compiler::setup_payload()
{
   /* r0.5 7:0: FFTID.  Needs to be incleded in each URB WRITE message.
    * r0.2 7:   Rendering enabled flag.
    * r0.2 4:0: Primitive Topology Type.
    */
   int reg = 0;
   this->r0 = retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UD);

   /* TODO: add streamed vertex buffer index values. */

   for (unsigned i = 0; i < this->key->num_vertices; ++i) {
      this->vertex_data[i] =
         retype(brw_vec8_grf(reg, 0), BRW_REGISTER_TYPE_UD);
      reg += this->urb_entry_read_length;
   }

   return reg;
}

void
gs_vec4_compiler::emit_code()
{
   /* Writeback register used to hold the value returned from FF sync and URB
    * writes
    */
   dst_reg writeback(this, 2, BRW_REGISTER_TYPE_UD);

   /* TODO: don't emit vertices if rendering disabled */

   current_annotation = "FF sync";
   emit(MOV(dst_reg(brw_message_reg(1)), src_reg(this->r0)));
   emit(MOV(dst_reg(brw_message_reg(1)).subreg(1).width(1),
            src_reg((unsigned) 1)));
   vec4_instruction *ff_sync = emit(GS_OPCODE_FF_SYNC, writeback);
   ff_sync->base_mrf = 1;

   for (unsigned vertex = 0; vertex < this->key->num_vertices; ++vertex) {
      /* TODO: need to set more fields in the URB header */
      current_annotation = "URB header";
      emit(MOV(dst_reg(brw_message_reg(1)), src_reg(this->r0)));
      emit(MOV(dst_reg(brw_message_reg(1)).width(1),
               src_reg(writeback).width(1)));

      current_annotation = "Vertex data";
      for (unsigned urb_reg = 0; urb_reg < this->urb_entry_read_length; ++urb_reg) {
         emit(MOV(dst_reg(brw_message_reg(2 + urb_reg)),
                  this->vertex_data[vertex].offset(urb_reg)));
      }

      // TODO: GS_OPCODE_URB_WRITE is like VS_OPCODE_URB_WRITE, except:
      // - it allows a non-null dest register
      // - it doesn't do the implied move of the first source register
      // - it sets allocate based on EOT
      // - it sets rlen appropriately
      // - it sets writes_complete to 1 when EOT
      // - it uses an offset of 0
      // - it uses BRW_URB_SWIZZLE_NONE
      bool eot = (vertex + 1 == this->key->num_vertices);
      vec4_instruction *urb_write =
         emit(GS_OPCODE_URB_WRITE, eot ? writeback : dst_null_d());
      urb_write->base_mrf = 1;
      urb_write->mlen = 1 + this->urb_entry_read_length; // TODO: does this ever get too big?
      urb_write->eot = eot;
   }
}

void
gs_vec4_compiler::run()
{
   int first_non_payload_grf = setup_payload();
   emit_code();
   optimize();
   if (failed())
      return;
   prog_data.total_grf = generate_code(first_non_payload_grf);
}

static void
compile_gs_vec4_prog(struct brw_context *brw, gs_vec4_prog_key *key)
{
   void *mem_ctx = ralloc_context(NULL);

   brw_compile p;
   memset(&p, 0, sizeof(p));
   brw_init_compile(brw, &p, mem_ctx); // TODO: should go inside gs_vec4_compiler.

   gs_vec4_compiler compiler(&p, key);
   compiler.run();
   assert (!compiler.failed());

   GLuint program_size;
   const GLuint *program = brw_get_program(&p, &program_size);

   brw_upload_cache(&brw->cache, BRW_GS_PROG, key, sizeof(*key), program,
                    program_size, compiler.get_prog_data(),
                    compiler.get_prog_data_size(), &brw->gs.prog_offset,
                    &brw->gs.prog_data);
   ralloc_free(mem_ctx);
}

static unsigned
num_vertices_for_primitive(GLenum primitive)
{
   switch (primitive) {
   case _3DPRIM_POINTLIST:
      return 1;
   case _3DPRIM_LINELIST:
   case _3DPRIM_LINESTRIP:
   case _3DPRIM_LINESTRIP_CONT:
   case _3DPRIM_LINESTRIP_CONT_BF:
      return 2;
   case _3DPRIM_TRILIST:
   case _3DPRIM_TRISTRIP:
   case _3DPRIM_TRIFAN:
   case _3DPRIM_TRISTRIP_REVERSE:
   case _3DPRIM_POLYGON:
   case _3DPRIM_RECTLIST:
   case _3DPRIM_TRIFAN_NOSTIPPLE:
      return 3;
   default:
      assert (!"Unexpected primitive for GS");
      return 1;
   }
}

static void
populate_key(struct brw_context *brw, gs_vec4_prog_key *key)
{
   struct gl_context *ctx = &brw->intel.ctx;

   /* BRW_NEW_PRIMITIVE */
   key->num_vertices = num_vertices_for_primitive(brw->primitive);

   /* _NEW_TRANSFORM */
   key->userclip_active = (ctx->Transform.ClipPlanesEnabled != 0);

   /* CACHE_NEW_VS_PROG */
   key->attrs = brw->vs.prog_data->outputs_written;

   key->need_gs_prog = 1; // TODO
}

static void
upload_gs_vec4_prog(struct brw_context *brw)
{
   gs_vec4_prog_key key;
   populate_key(brw, &key);

   // TODO: gen7 should have no gs.  Figure out the right way to accomplish this.

   if (brw->gs.prog_active != key.need_gs_prog) {
      brw->state.dirty.cache |= CACHE_NEW_GS_PROG;
      brw->gs.prog_active = key.need_gs_prog;
   }

   if (brw->gs.prog_active) {
      if (!brw_search_cache(&brw->cache, BRW_GS_PROG, &key, sizeof(key),
                            &brw->gs.prog_offset, &brw->gs.prog_data)) {
         compile_gs_vec4_prog(brw, &key);
      }
   }
}

} /* namespace brw */

// TODO: want to do this using extended initializer lists
const brw_tracked_state brw_gs_vec4_prog = {
   /* .dirty = */ {
      /* .mesa = */ _NEW_TRANSFORM,
      /* .brw = */ BRW_NEW_PRIMITIVE,
      /* .cache = */ CACHE_NEW_VS_PROG
   },
   /* .emit = */ upload_gs_vec4_prog
};
