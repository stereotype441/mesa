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


#include "marshal_generatable.h"

#include "api_exec.h"
#include "context.h"
#include "dispatch.h"
#include "marshal.h"


#define QUEUE_SIMPLE_COMMAND(var, cmd_name) \
   struct marshal_cmd_##cmd_name *var = \
      _mesa_allocate_command_in_queue(ctx, DISPATCH_CMD_##cmd_name,    \
                                sizeof(struct marshal_cmd_##cmd_name))


static const GLubyte *GLAPIENTRY
marshal_GetString(GLenum name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   return CALL_GetString(ctx->CurrentServerDispatch, (name));
}


struct marshal_cmd_Viewport
{
   struct marshal_cmd_base cmd_base;
   GLint x;
   GLint y;
   GLsizei width;
   GLsizei height;
};


static inline void
unmarshal_Viewport(struct gl_context *ctx, const struct marshal_cmd_Viewport *cmd)
{
   CALL_Viewport(ctx->CurrentServerDispatch, (cmd->x, cmd->y, cmd->width, cmd->height));
}


static void GLAPIENTRY
marshal_Viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Viewport);
   cmd->x = x;
   cmd->y = y;
   cmd->width = width;
   cmd->height = height;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_MatrixMode
{
   struct marshal_cmd_base cmd_base;
   GLenum mode;
};


static inline void
unmarshal_MatrixMode(struct gl_context *ctx, const struct marshal_cmd_MatrixMode *cmd)
{
   CALL_MatrixMode(ctx->CurrentServerDispatch, (cmd->mode));
}


static void GLAPIENTRY
marshal_MatrixMode(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, MatrixMode);
   cmd->mode = mode;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_LoadIdentity
{
   struct marshal_cmd_base cmd_base;
};


static inline void
unmarshal_LoadIdentity(struct gl_context *ctx,
                       const struct marshal_cmd_LoadIdentity *cmd)
{
   CALL_LoadIdentity(ctx->CurrentServerDispatch, ());
}


static void GLAPIENTRY
marshal_LoadIdentity(void)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, LoadIdentity);
   (void) cmd;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_Ortho
{
   struct marshal_cmd_base cmd_base;
   GLdouble left;
   GLdouble right;
   GLdouble bottom;
   GLdouble top;
   GLdouble nearval;
   GLdouble farval;
};


static inline void
unmarshal_Ortho(struct gl_context *ctx, const struct marshal_cmd_Ortho *cmd)
{
   CALL_Ortho(ctx->CurrentServerDispatch, (cmd->left, cmd->right, cmd->bottom, cmd->top, cmd->nearval, cmd->farval));
}


static void GLAPIENTRY
marshal_Ortho(GLdouble left, GLdouble right,
              GLdouble bottom, GLdouble top, GLdouble nearval, GLdouble farval)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Ortho);
   cmd->left = left;
   cmd->right = right;
   cmd->bottom = bottom;
   cmd->top = top;
   cmd->nearval = nearval;
   cmd->farval = farval;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_PolygonMode
{
   struct marshal_cmd_base cmd_base;
   GLenum face;
   GLenum mode;
};


static inline void
unmarshal_PolygonMode(struct gl_context *ctx,
                      const struct marshal_cmd_PolygonMode *cmd)
{
   CALL_PolygonMode(ctx->CurrentServerDispatch, (cmd->face, cmd->mode));
}


static void GLAPIENTRY
marshal_PolygonMode(GLenum face, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, PolygonMode);
   cmd->face = face;
   cmd->mode = mode;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_ClearColor
{
   struct marshal_cmd_base cmd_base;
   GLclampf red;
   GLclampf green;
   GLclampf blue;
   GLclampf alpha;
};


static inline void
unmarshal_ClearColor(struct gl_context *ctx, const struct marshal_cmd_ClearColor *cmd)
{
   CALL_ClearColor(ctx->CurrentServerDispatch, (cmd->red, cmd->green, cmd->blue, cmd->alpha));
}


static void GLAPIENTRY
marshal_ClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, ClearColor);
   cmd->red = red;
   cmd->green = green;
   cmd->blue = blue;
   cmd->alpha = alpha;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_Clear
{
   struct marshal_cmd_base cmd_base;
   GLbitfield mask;
};


static inline void
unmarshal_Clear(struct gl_context *ctx, const struct marshal_cmd_Clear *cmd)
{
   CALL_Clear(ctx->CurrentServerDispatch, (cmd->mask));
}


static void GLAPIENTRY
marshal_Clear(GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Clear);
   cmd->mask = mask;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_Color4f
{
   struct marshal_cmd_base cmd_base;
   GLfloat x;
   GLfloat y;
   GLfloat z;
   GLfloat w;
};


static inline void
unmarshal_Color4f(struct gl_context *ctx, const struct marshal_cmd_Color4f *cmd)
{
   CALL_Color4f(ctx->CurrentServerDispatch, (cmd->x, cmd->y, cmd->z, cmd->w));
}


static void GLAPIENTRY
marshal_Color4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Color4f);
   cmd->x = x;
   cmd->y = y;
   cmd->z = z;
   cmd->w = w;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_Begin
{
   struct marshal_cmd_base cmd_base;
   GLenum mode;
};


static inline void
unmarshal_Begin(struct gl_context *ctx, const struct marshal_cmd_Begin *cmd)
{
   CALL_Begin(ctx->CurrentServerDispatch, (cmd->mode));
}


static void GLAPIENTRY
marshal_Begin(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Begin);
   cmd->mode = mode;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_EdgeFlag
{
   struct marshal_cmd_base cmd_base;
   GLboolean x;
};


static inline void
unmarshal_EdgeFlag(struct gl_context *ctx, const struct marshal_cmd_EdgeFlag *cmd)
{
   CALL_EdgeFlag(ctx->CurrentServerDispatch, (cmd->x));
}


static void GLAPIENTRY
marshal_EdgeFlag(GLboolean x)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, EdgeFlag);
   cmd->x = x;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_Vertex2f
{
   struct marshal_cmd_base cmd_base;
   GLfloat x;
   GLfloat y;
};


static inline void
unmarshal_Vertex2f(struct gl_context *ctx, const struct marshal_cmd_Vertex2f *cmd)
{
   CALL_Vertex2f(ctx->CurrentServerDispatch, (cmd->x, cmd->y));
}


static void GLAPIENTRY
marshal_Vertex2f(GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Vertex2f);
   cmd->x = x;
   cmd->y = y;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_End
{
   struct marshal_cmd_base cmd_base;
};


static inline void
unmarshal_End(struct gl_context *ctx, const struct marshal_cmd_End *cmd)
{
   CALL_End(ctx->CurrentServerDispatch, ());
}


static void GLAPIENTRY
marshal_End(void)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, End);
   (void) cmd;
   _mesa_post_marshal_hook(ctx);
}


static void GLAPIENTRY
marshal_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   CALL_ReadPixels(ctx->CurrentServerDispatch, (x, y, width, height, format, type, pixels));
}


struct marshal_cmd_Flush
{
   struct marshal_cmd_base cmd_base;
};


static inline void
unmarshal_Flush(struct gl_context *ctx, const struct marshal_cmd_Flush *cmd)
{
   CALL_Flush(ctx->CurrentServerDispatch, ());
}


static void GLAPIENTRY
marshal_Flush(void)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Flush);
   (void) cmd;
   _mesa_post_marshal_hook(ctx);
   _mesa_marshal_synchronize(ctx); /* TODO: HACK to avoid problems with SwapBuffers */
}


static void GLAPIENTRY
marshal_GetIntegerv(GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   CALL_GetIntegerv(ctx->CurrentServerDispatch, (pname, params));
}


static GLuint GLAPIENTRY
marshal_CreateShader(GLenum type)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   return CALL_CreateShader(ctx->CurrentServerDispatch, (type));
}


struct marshal_cmd_CompileShaderARB
{
   struct marshal_cmd_base cmd_base;
   GLhandleARB shaderObj;
};


static inline void
unmarshal_CompileShaderARB(struct gl_context *ctx,
                           const struct marshal_cmd_CompileShaderARB *cmd)
{
   CALL_CompileShaderARB(ctx->CurrentServerDispatch, (cmd->shaderObj));
}


static void GLAPIENTRY
marshal_CompileShaderARB(GLhandleARB shaderObj)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, CompileShaderARB);
   cmd->shaderObj = shaderObj;
   _mesa_post_marshal_hook(ctx);
}


static void GLAPIENTRY
marshal_GetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   CALL_GetShaderiv(ctx->CurrentServerDispatch, (shader, pname, params));
}


static GLuint GLAPIENTRY
marshal_CreateProgram(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   return CALL_CreateProgram(ctx->CurrentServerDispatch, ());
}


struct marshal_cmd_AttachShader
{
   struct marshal_cmd_base cmd_base;
   GLuint program;
   GLuint shader;
};


static inline void
unmarshal_AttachShader(struct gl_context *ctx,
                       const struct marshal_cmd_AttachShader *cmd)
{
   CALL_AttachShader(ctx->CurrentServerDispatch, (cmd->program, cmd->shader));
}


static void GLAPIENTRY
marshal_AttachShader(GLuint program, GLuint shader)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, AttachShader);
   cmd->program = program;
   cmd->shader = shader;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_LinkProgramARB
{
   struct marshal_cmd_base cmd_base;
   GLhandleARB programObj;
};


static inline void
unmarshal_LinkProgramARB(struct gl_context *ctx,
                         const struct marshal_cmd_LinkProgramARB *cmd)
{
   CALL_LinkProgramARB(ctx->CurrentServerDispatch, (cmd->programObj));
}


static void GLAPIENTRY
marshal_LinkProgramARB(GLuint programObj)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, LinkProgramARB);
   cmd->programObj = programObj;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_DeleteShader
{
   struct marshal_cmd_base cmd_base;
   GLuint shader;
};


static inline void
unmarshal_DeleteShader(struct gl_context *ctx,
                       const struct marshal_cmd_DeleteShader *cmd)
{
   CALL_DeleteShader(ctx->CurrentServerDispatch, (cmd->shader));
}


static void GLAPIENTRY
marshal_DeleteShader(GLuint shader)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, DeleteShader);
   cmd->shader = shader;
   _mesa_post_marshal_hook(ctx);
}


static void GLAPIENTRY
marshal_GetProgramiv(GLuint program, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   return CALL_GetProgramiv(ctx->CurrentServerDispatch, (program, pname, params));
}


struct marshal_cmd_UseProgramObjectARB
{
   struct marshal_cmd_base cmd_base;
   GLhandleARB program;
};


static inline void
unmarshal_UseProgramObjectARB(struct gl_context *ctx,
                              const struct marshal_cmd_UseProgramObjectARB *cmd)
{
   CALL_UseProgramObjectARB(ctx->CurrentServerDispatch, (cmd->program));
}


static void GLAPIENTRY
marshal_UseProgramObjectARB(GLhandleARB program)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, UseProgramObjectARB);
   cmd->program = program;
   _mesa_post_marshal_hook(ctx);
}


static GLenum GLAPIENTRY
marshal_GetError(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   return CALL_GetError(ctx->CurrentServerDispatch, ());
}


static const GLubyte * GLAPIENTRY
marshal_GetStringi(GLenum name, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   return CALL_GetStringi(ctx->CurrentServerDispatch, (name, index));
}


static GLint GLAPIENTRY
marshal_GetUniformLocationARB(GLhandleARB programObj, const GLcharARB *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   return CALL_GetUniformLocationARB(ctx->CurrentServerDispatch, (programObj, name));
}


struct marshal_cmd_Uniform1fvARB
{
   struct marshal_cmd_base cmd_base;
   GLint location;
   GLsizei count;
   /* Followed by GLfloat value[count] */
};


static inline void
unmarshal_Uniform1fvARB(struct gl_context *ctx,
                        const struct marshal_cmd_Uniform1fvARB *cmd)
{
   const GLfloat *cmd_value = (const GLfloat *) (cmd + 1);
   CALL_Uniform1fvARB(ctx->CurrentServerDispatch, (cmd->location, cmd->count, cmd_value));
}


static void GLAPIENTRY
marshal_Uniform1fvARB(GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   const size_t fixed_cmd_size = sizeof(struct marshal_cmd_Uniform1fvARB);
   STATIC_ASSERT(fixed_cmd_size % sizeof(GLfloat) == 0);
   size_t value_size = count * sizeof(GLfloat);
   size_t total_cmd_size = fixed_cmd_size + value_size;
   if (total_cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      struct marshal_cmd_Uniform1fvARB *cmd =
         _mesa_allocate_command_in_queue(ctx, DISPATCH_CMD_Uniform1fvARB,
                                         total_cmd_size);
      GLfloat *cmd_value = (GLfloat *) (cmd + 1);
      cmd->location = location;
      cmd->count = count;
      memcpy(cmd_value, value, value_size);
      _mesa_post_marshal_hook(ctx);
   } else {
      _mesa_marshal_synchronize(ctx);
      CALL_Uniform1fvARB(ctx->CurrentServerDispatch, (location, count, value));
   }
}


struct marshal_cmd_Uniform1iARB
{
   struct marshal_cmd_base cmd_base;
   GLint location;
   GLint v0;
};


static inline void
unmarshal_Uniform1iARB(struct gl_context *ctx,
                       const struct marshal_cmd_Uniform1iARB *cmd)
{
   CALL_Uniform1iARB(ctx->CurrentServerDispatch, (cmd->location, cmd->v0));
}


static void GLAPIENTRY
marshal_Uniform1iARB(GLint location, GLint v0)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Uniform1iARB);
   cmd->location = location;
   cmd->v0 = v0;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_VertexPointer
{
   struct marshal_cmd_base cmd_base;
   GLint size;
   GLenum type;
   GLsizei stride;
   const GLvoid *pointer;
};


static inline void
unmarshal_VertexPointer(struct gl_context *ctx,
                        const struct marshal_cmd_VertexPointer *cmd)
{
   CALL_VertexPointer(ctx->CurrentServerDispatch, (cmd->size, cmd->type, cmd->stride, cmd->pointer));
}


static void GLAPIENTRY
marshal_VertexPointer(GLint size, GLenum type, GLsizei stride,
                      const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, VertexPointer);
   cmd->size = size;
   cmd->type = type;
   cmd->stride = stride;
   cmd->pointer = pointer;
   _mesa_post_marshal_hook(ctx);
}


struct marshal_cmd_EnableClientState
{
   struct marshal_cmd_base cmd_base;
   GLenum array;
};


static inline void
unmarshal_EnableClientState(struct gl_context *ctx,
                            const struct marshal_cmd_EnableClientState *cmd)
{
   CALL_EnableClientState(ctx->CurrentServerDispatch, (cmd->array));
}


static void GLAPIENTRY
marshal_EnableClientState(GLenum array)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, EnableClientState);
   cmd->array = array;
   _mesa_post_marshal_hook(ctx);
}


static void GLAPIENTRY
marshal_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_marshal_synchronize(ctx);
   CALL_DrawArrays(ctx->CurrentServerDispatch, (mode, first, count));
}


struct marshal_cmd_DisableClientState
{
   struct marshal_cmd_base cmd_base;
   GLenum array;
};


static inline void
unmarshal_DisableClientState(struct gl_context *ctx,
                             const struct marshal_cmd_DisableClientState *cmd)
{
   CALL_DisableClientState(ctx->CurrentServerDispatch, (cmd->array));
}


static void GLAPIENTRY
marshal_DisableClientState(GLenum array)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, DisableClientState);
   cmd->array = array;
   _mesa_post_marshal_hook(ctx);
}


size_t
_mesa_unmarshal_dispatch_cmd(struct gl_context *ctx, const void *cmd)
{
   const struct marshal_cmd_base *cmd_base = (const struct marshal_cmd_base *) cmd;
   switch (cmd_base->cmd_id) {
   case DISPATCH_CMD_Viewport:
      unmarshal_Viewport(ctx, (const struct marshal_cmd_Viewport *) cmd);
      break;
   case DISPATCH_CMD_MatrixMode:
      unmarshal_MatrixMode(ctx, (const struct marshal_cmd_MatrixMode *) cmd);
      break;
   case DISPATCH_CMD_LoadIdentity:
      unmarshal_LoadIdentity(ctx, (const struct marshal_cmd_LoadIdentity *) cmd);
      break;
   case DISPATCH_CMD_Ortho:
      unmarshal_Ortho(ctx, (const struct marshal_cmd_Ortho *) cmd);
      break;
   case DISPATCH_CMD_PolygonMode:
      unmarshal_PolygonMode(ctx, (const struct marshal_cmd_PolygonMode *) cmd);
      break;
   case DISPATCH_CMD_ClearColor:
      unmarshal_ClearColor(ctx, (const struct marshal_cmd_ClearColor *) cmd);
      break;
   case DISPATCH_CMD_Clear:
      unmarshal_Clear(ctx, (const struct marshal_cmd_Clear *) cmd);
      break;
   case DISPATCH_CMD_Color4f:
      unmarshal_Color4f(ctx, (const struct marshal_cmd_Color4f *) cmd);
      break;
   case DISPATCH_CMD_Begin:
      unmarshal_Begin(ctx, (const struct marshal_cmd_Begin *) cmd);
      break;
   case DISPATCH_CMD_EdgeFlag:
      unmarshal_EdgeFlag(ctx, (const struct marshal_cmd_EdgeFlag *) cmd);
      break;
   case DISPATCH_CMD_Vertex2f:
      unmarshal_Vertex2f(ctx, (const struct marshal_cmd_Vertex2f *) cmd);
      break;
   case DISPATCH_CMD_End:
      unmarshal_End(ctx, (const struct marshal_cmd_End *) cmd);
      break;
   case DISPATCH_CMD_Flush:
      unmarshal_Flush(ctx, (const struct marshal_cmd_Flush *) cmd);
      break;
   case DISPATCH_CMD_ShaderSourceARB:
      _mesa_unmarshal_ShaderSourceARB(ctx, (const struct marshal_cmd_ShaderSourceARB *) cmd);
      break;
   case DISPATCH_CMD_CompileShaderARB:
      unmarshal_CompileShaderARB(ctx, (const struct marshal_cmd_CompileShaderARB *) cmd);
      break;
   case DISPATCH_CMD_AttachShader:
      unmarshal_AttachShader(ctx, (const struct marshal_cmd_AttachShader *) cmd);
      break;
   case DISPATCH_CMD_LinkProgramARB:
      unmarshal_LinkProgramARB(ctx, (const struct marshal_cmd_LinkProgramARB *) cmd);
      break;
   case DISPATCH_CMD_DeleteShader:
      unmarshal_DeleteShader(ctx, (const struct marshal_cmd_DeleteShader *) cmd);
      break;
   case DISPATCH_CMD_UseProgramObjectARB:
      unmarshal_UseProgramObjectARB(ctx, (const struct marshal_cmd_UseProgramObjectARB *) cmd);
      break;
   case DISPATCH_CMD_Uniform1fvARB:
      unmarshal_Uniform1fvARB(ctx, (const struct marshal_cmd_Uniform1fvARB *) cmd);
      break;
   case DISPATCH_CMD_Uniform1iARB:
      unmarshal_Uniform1iARB(ctx, (const struct marshal_cmd_Uniform1iARB *) cmd);
      break;
   case DISPATCH_CMD_VertexPointer:
      unmarshal_VertexPointer(ctx, (const struct marshal_cmd_VertexPointer *) cmd);
      break;
   case DISPATCH_CMD_EnableClientState:
      unmarshal_EnableClientState(ctx, (const struct marshal_cmd_EnableClientState *) cmd);
      break;
   case DISPATCH_CMD_DisableClientState:
      unmarshal_DisableClientState(ctx, (const struct marshal_cmd_DisableClientState *) cmd);
      break;
   default:
      assert(!"Unrecognized command ID");
      break;
   }

   return cmd_base->cmd_size;
}


struct _glapi_table *
_mesa_create_marshal_table(const struct gl_context *ctx)
{
   struct _glapi_table *table;

   table = _mesa_alloc_dispatch_table(_gloffset_COUNT);
   if (table == NULL)
      return NULL;

   /* TODO: implement more! */
   SET_GetString(table, marshal_GetString);
   SET_Viewport(table, marshal_Viewport);
   SET_MatrixMode(table, marshal_MatrixMode);
   SET_LoadIdentity(table, marshal_LoadIdentity);
   SET_Ortho(table, marshal_Ortho);
   SET_PolygonMode(table, marshal_PolygonMode);
   SET_ClearColor(table, marshal_ClearColor);
   SET_Clear(table, marshal_Clear);
   SET_Color4f(table, marshal_Color4f);
   SET_Begin(table, marshal_Begin);
   SET_EdgeFlag(table, marshal_EdgeFlag);
   SET_Vertex2f(table, marshal_Vertex2f);
   SET_End(table, marshal_End);
   SET_ReadPixels(table, marshal_ReadPixels);
   SET_Flush(table, marshal_Flush);
   SET_GetIntegerv(table, marshal_GetIntegerv);
   SET_CreateShader(table, marshal_CreateShader);
   SET_ShaderSourceARB(table, _mesa_marshal_ShaderSourceARB);
   SET_CompileShaderARB(table, marshal_CompileShaderARB);
   SET_GetShaderiv(table, marshal_GetShaderiv);
   SET_CreateProgram(table, marshal_CreateProgram);
   SET_AttachShader(table, marshal_AttachShader);
   SET_LinkProgramARB(table, marshal_LinkProgramARB);
   SET_DeleteShader(table, marshal_DeleteShader);
   SET_GetProgramiv(table, marshal_GetProgramiv);
   SET_UseProgramObjectARB(table, marshal_UseProgramObjectARB);
   SET_GetError(table, marshal_GetError);
   SET_GetStringi(table, marshal_GetStringi);
   SET_GetUniformLocationARB(table, marshal_GetUniformLocationARB);
   SET_Uniform1fvARB(table, marshal_Uniform1fvARB);
   SET_Uniform1iARB(table, marshal_Uniform1iARB);
   SET_VertexPointer(table, marshal_VertexPointer);
   SET_EnableClientState(table, marshal_EnableClientState);
   SET_DrawArrays(table, marshal_DrawArrays);
   SET_DisableClientState(table, marshal_DisableClientState);

   return table;
}
