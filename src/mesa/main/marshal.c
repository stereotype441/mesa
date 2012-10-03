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
};


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
   unmarshal_Viewport(ctx, cmd);
}


static void GLAPIENTRY
marshal_MatrixMode(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_MatrixMode(ctx->Exec, (mode));
}


static void GLAPIENTRY
marshal_LoadIdentity(void)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_LoadIdentity(ctx->Exec, ());
}


static void GLAPIENTRY
marshal_Ortho(GLdouble left, GLdouble right,
              GLdouble bottom, GLdouble top, GLdouble nearval, GLdouble farval)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_Ortho(ctx->Exec, (left, right, bottom, top, nearval, farval));
}


static void GLAPIENTRY
marshal_PolygonMode(GLenum face, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_PolygonMode(ctx->Exec, (face, mode));
}


static void GLAPIENTRY
marshal_ClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_ClearColor(ctx->Exec, (red, green, blue, alpha));
}


static void GLAPIENTRY
marshal_Clear(GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_Clear(ctx->Exec, (mask));
}


static void GLAPIENTRY
marshal_Color4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_Color4f(ctx->Exec, (x, y, z, w));
}


static void GLAPIENTRY
marshal_Begin(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_Begin(ctx->Exec, (mode));
}


static void GLAPIENTRY
marshal_EdgeFlag(GLboolean x)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_EdgeFlag(ctx->Exec, (x));
}


static void GLAPIENTRY
marshal_Vertex2f(GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_Vertex2f(ctx->Exec, (x, y));
}


static void GLAPIENTRY
marshal_End(void)
{
   GET_CURRENT_CONTEXT(ctx);

   CALL_End(ctx->Exec, ());
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
