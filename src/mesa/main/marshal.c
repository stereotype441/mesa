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

/** \file marshal.h
 *
 * Functions related to marshalling GL calls from a client thread to a server
 * thread.
 */

#include "marshal.h"

#include "api_exec.h"
#include "dispatch.h"


enum dispatch_cmd_id
{
   DISPATCH_CMD_Viewport,
   DISPATCH_CMD_MatrixMode,
   DISPATCH_CMD_LoadIdentity,
   DISPATCH_CMD_Ortho,
   DISPATCH_CMD_PolygonMode,
   DISPATCH_CMD_ClearColor,
   DISPATCH_CMD_Clear,
   DISPATCH_CMD_Color4f,
   DISPATCH_CMD_Begin,
   DISPATCH_CMD_EdgeFlag,
   DISPATCH_CMD_Vertex2f,
   DISPATCH_CMD_End,
};


static size_t
unmarshal_dispatch_cmd(struct gl_context *ctx, void *cmd);


static const GLubyte *GLAPIENTRY
marshal_GetString(GLenum name)
{
   GET_CURRENT_CONTEXT(ctx);

   return CALL_GetString(ctx->Exec, (name));
}


#define QUEUE_SIMPLE_COMMAND(var, cmd_name) \
   struct cmd_##cmd_name _local_cmd; \
   struct cmd_##cmd_name *var = &_local_cmd; \
   var->cmd_id = DISPATCH_CMD_##cmd_name


struct cmd_Viewport
{
   enum dispatch_cmd_id cmd_id;
   GLint x;
   GLint y;
   GLsizei width;
   GLsizei height;
};


static inline void
unmarshal_Viewport(struct gl_context *ctx, struct cmd_Viewport *cmd)
{
   CALL_Viewport(ctx->Exec, (cmd->x, cmd->y, cmd->width, cmd->height));
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
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_MatrixMode
{
   enum dispatch_cmd_id cmd_id;
   GLenum mode;
};


static inline void
unmarshal_MatrixMode(struct gl_context *ctx, struct cmd_MatrixMode *cmd)
{
   CALL_MatrixMode(ctx->Exec, (cmd->mode));
}


static void GLAPIENTRY
marshal_MatrixMode(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, MatrixMode);
   cmd->mode = mode;
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_LoadIdentity
{
   enum dispatch_cmd_id cmd_id;
};


static inline void
unmarshal_LoadIdentity(struct gl_context *ctx, struct cmd_LoadIdentity *cmd)
{
   CALL_LoadIdentity(ctx->Exec, ());
}


static void GLAPIENTRY
marshal_LoadIdentity(void)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, LoadIdentity);
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_Ortho
{
   enum dispatch_cmd_id cmd_id;
   GLdouble left;
   GLdouble right;
   GLdouble bottom;
   GLdouble top;
   GLdouble nearval;
   GLdouble farval;
};


static inline void
unmarshal_Ortho(struct gl_context *ctx, struct cmd_Ortho *cmd)
{
   CALL_Ortho(ctx->Exec, (cmd->left, cmd->right, cmd->bottom, cmd->top, cmd->nearval, cmd->farval));
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
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_PolygonMode
{
   enum dispatch_cmd_id cmd_id;
   GLenum face;
   GLenum mode;
};


static inline void
unmarshal_PolygonMode(struct gl_context *ctx, struct cmd_PolygonMode *cmd)
{
   CALL_PolygonMode(ctx->Exec, (cmd->face, cmd->mode));
}


static void GLAPIENTRY
marshal_PolygonMode(GLenum face, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, PolygonMode);
   cmd->face = face;
   cmd->mode = mode;
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_ClearColor
{
   enum dispatch_cmd_id cmd_id;
   GLclampf red;
   GLclampf green;
   GLclampf blue;
   GLclampf alpha;
};


static inline void
unmarshal_ClearColor(struct gl_context *ctx, struct cmd_ClearColor *cmd)
{
   CALL_ClearColor(ctx->Exec, (cmd->red, cmd->green, cmd->blue, cmd->alpha));
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
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_Clear
{
   enum dispatch_cmd_id cmd_id;
   GLbitfield mask;
};


static inline void
unmarshal_Clear(struct gl_context *ctx, struct cmd_Clear *cmd)
{
   CALL_Clear(ctx->Exec, (cmd->mask));
}


static void GLAPIENTRY
marshal_Clear(GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Clear);
   cmd->mask = mask;
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_Color4f
{
   enum dispatch_cmd_id cmd_id;
   GLfloat x;
   GLfloat y;
   GLfloat z;
   GLfloat w;
};


static inline void
unmarshal_Color4f(struct gl_context *ctx, struct cmd_Color4f *cmd)
{
   CALL_Color4f(ctx->Exec, (cmd->x, cmd->y, cmd->z, cmd->w));
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
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_Begin
{
   enum dispatch_cmd_id cmd_id;
   GLenum mode;
};


static inline void
unmarshal_Begin(struct gl_context *ctx, struct cmd_Begin *cmd)
{
   CALL_Begin(ctx->Exec, (cmd->mode));
}


static void GLAPIENTRY
marshal_Begin(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Begin);
   cmd->mode = mode;
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_EdgeFlag
{
   enum dispatch_cmd_id cmd_id;
   GLboolean x;
};


static inline void
unmarshal_EdgeFlag(struct gl_context *ctx, struct cmd_EdgeFlag *cmd)
{
   CALL_EdgeFlag(ctx->Exec, (cmd->x));
}


static void GLAPIENTRY
marshal_EdgeFlag(GLboolean x)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, EdgeFlag);
   cmd->x = x;
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_Vertex2f
{
   enum dispatch_cmd_id cmd_id;
   GLfloat x;
   GLfloat y;
};


static inline void
unmarshal_Vertex2f(struct gl_context *ctx, struct cmd_Vertex2f *cmd)
{
   CALL_Vertex2f(ctx->Exec, (cmd->x, cmd->y));
}


static void GLAPIENTRY
marshal_Vertex2f(GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Vertex2f);
   cmd->x = x;
   cmd->y = y;
   unmarshal_dispatch_cmd(ctx, cmd);
}


struct cmd_End
{
   enum dispatch_cmd_id cmd_id;
};


static inline void
unmarshal_End(struct gl_context *ctx, struct cmd_End *cmd)
{
   CALL_End(ctx->Exec, ());
}


static void GLAPIENTRY
marshal_End(void)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, End);
   unmarshal_dispatch_cmd(ctx, cmd);
}


static void GLAPIENTRY
marshal_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_ReadPixels(ctx->Exec, (x, y, width, height, format, type, pixels));
}


static void GLAPIENTRY
marshal_Flush(void)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_Flush(ctx->Exec, ());
}


static size_t
unmarshal_dispatch_cmd(struct gl_context *ctx, void *cmd)
{
   switch (*(enum dispatch_cmd_id *) cmd) {
   case DISPATCH_CMD_Viewport:
      unmarshal_Viewport(ctx, (struct cmd_Viewport *) cmd);
      return sizeof(struct cmd_Viewport);
   case DISPATCH_CMD_MatrixMode:
      unmarshal_MatrixMode(ctx, (struct cmd_MatrixMode *) cmd);
      return sizeof(struct cmd_MatrixMode);
   case DISPATCH_CMD_LoadIdentity:
      unmarshal_LoadIdentity(ctx, (struct cmd_LoadIdentity *) cmd);
      return sizeof(struct cmd_LoadIdentity);
   case DISPATCH_CMD_Ortho:
      unmarshal_Ortho(ctx, (struct cmd_Ortho *) cmd);
      return sizeof(struct cmd_Ortho);
   case DISPATCH_CMD_PolygonMode:
      unmarshal_PolygonMode(ctx, (struct cmd_PolygonMode *) cmd);
      return sizeof(struct cmd_PolygonMode);
   case DISPATCH_CMD_ClearColor:
      unmarshal_ClearColor(ctx, (struct cmd_ClearColor *) cmd);
      return sizeof(struct cmd_ClearColor);
   case DISPATCH_CMD_Clear:
      unmarshal_Clear(ctx, (struct cmd_Clear *) cmd);
      return sizeof(struct cmd_Clear);
   case DISPATCH_CMD_Color4f:
      unmarshal_Color4f(ctx, (struct cmd_Color4f *) cmd);
      return sizeof(struct cmd_Color4f);
   case DISPATCH_CMD_Begin:
      unmarshal_Begin(ctx, (struct cmd_Begin *) cmd);
      return sizeof(struct cmd_Begin);
   case DISPATCH_CMD_EdgeFlag:
      unmarshal_EdgeFlag(ctx, (struct cmd_EdgeFlag *) cmd);
      return sizeof(struct cmd_EdgeFlag);
   case DISPATCH_CMD_Vertex2f:
      unmarshal_Vertex2f(ctx, (struct cmd_Vertex2f *) cmd);
      return sizeof(struct cmd_Vertex2f);
   case DISPATCH_CMD_End:
      unmarshal_End(ctx, (struct cmd_End *) cmd);
      return sizeof(struct cmd_End);
   default:
      assert(!"Unrecognized command ID");
      return 0;
   }
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

   return table;
}
