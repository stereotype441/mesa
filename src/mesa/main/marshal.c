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

#include <stdbool.h>

#include "api_exec.h"
#include "dispatch.h"
#include "threadpool.h"


#define ALIGN(value, alignment)  (((value) + alignment - 1) & ~(alignment - 1))


static bool execute_immediately = false;
static bool use_actual_threads = true;


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
   DISPATCH_CMD_Flush,
   DISPATCH_CMD_ShaderSourceARB,
   DISPATCH_CMD_CompileShaderARB,
};


static size_t
unmarshal_dispatch_cmd(struct gl_context *ctx, void *cmd);
static void
consume_command_queue(void *ctx);


/**
 * A single batch of commands queued up for later execution by a thread pool
 * task.
 */
struct gl_context_marshal_batch
{
   /**
    * Next batch of commands to execute after this batch, or NULL if this is
    * the last set of commands queued.  Protected by ctx->Marshal.Mutex.
    */
   struct gl_context_marshal_batch *Next;

   /**
    * Points to the first command in the batch.
    */
   uint32_t *Buffer;

   /**
    * Amount of data used by batch commands, in multiples of 32 bits.
    */
   size_t DwordsUsed;
};


struct cmd_base
{
   /**
    * Type of command.  See enum dispatch_cmd_id.
    */
   uint16_t cmd_id;

   /**
    * Size of command, in multiples of 4 bytes, including cmd_base.
    */
   uint16_t cmd_size;
};


#define BUFFER_SIZE_DWORDS 65536
#define MAX_CMD_SIZE 65535


static void
submit_batch(struct gl_context *ctx)
{
   if (ctx->Marshal.BatchPrep == NULL)
      return;

   _glthread_LOCK_MUTEX(ctx->Marshal.Mutex);

   /* TODO: wait if the queue is full. */

   *ctx->Marshal.Shared.BatchQueueTail = ctx->Marshal.BatchPrep;
   ctx->Marshal.Shared.BatchQueueTail = &ctx->Marshal.BatchPrep->Next;
   ctx->Marshal.BatchPrep = NULL;

   if (ctx->Marshal.Task != NULL) {
      if (ctx->Marshal.Shared.TaskComplete) {
         /* The task has already started to exit (or has already exited), so
          * it won't pick up the new batch.  We'll need to start a new task,
          * so tell the thread pool we're done with this one.
          */
         _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
         _mesa_threadpool_wait_for_task(ctx->Shared->MarshalThreadPool,
                                        &ctx->Marshal.Task);
      } else {
         /* The task is still running, so it will pick up the new batch.
          * Nothing more we need to do.
          */
         _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
         return;
      }
   } else {
      _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
   }

   /* Now ctx->Marshal.Task is NULL, so it is safe to set
    * ctx->Marshal.Shared.TaskComplete without re-acquiring the mutex, because
    * there is no background task to contend with.
    */
   ctx->Marshal.Shared.TaskComplete = GL_FALSE;

   if (use_actual_threads) {
      ctx->Marshal.Task =
         _mesa_threadpool_queue_task(ctx->Shared->MarshalThreadPool,
                                     consume_command_queue, ctx);
   } else {
      /* If we aren't using actual threads, execute the commands
       * immediately.
       */
      consume_command_queue(ctx);
   }
}


static void *
allocate_command_in_queue(struct gl_context *ctx, enum dispatch_cmd_id cmd_id,
                          size_t size_bytes)
{
   struct cmd_base *cmd_base;
   size_t size_dwords = ALIGN(size_bytes, 4) / 4;

   assert(size_dwords <= 65535);
   assert(size_dwords <= BUFFER_SIZE_DWORDS);

   if (ctx->Marshal.BatchPrep != NULL &&
       ctx->Marshal.BatchPrep->DwordsUsed + size_dwords > BUFFER_SIZE_DWORDS) {
      submit_batch(ctx);
   }

   if (ctx->Marshal.BatchPrep == NULL) {
      /* TODO: how to handle memory allocation failure? */
      ctx->Marshal.BatchPrep =
         calloc(1, sizeof(struct gl_context_marshal_batch));
      ctx->Marshal.BatchPrep->Buffer = malloc(BUFFER_SIZE_DWORDS * 4);
   }

   cmd_base = (struct cmd_base *)
      &ctx->Marshal.BatchPrep->Buffer[ctx->Marshal.BatchPrep->DwordsUsed];
   ctx->Marshal.BatchPrep->DwordsUsed += size_dwords;
   cmd_base->cmd_id = cmd_id;
   cmd_base->cmd_size = size_dwords;
   return cmd_base;
}


#define QUEUE_SIMPLE_COMMAND(var, cmd_name) \
   struct cmd_##cmd_name *var = \
      allocate_command_in_queue(ctx, DISPATCH_CMD_##cmd_name,    \
                                sizeof(struct cmd_##cmd_name))


static void
consume_command_queue(void *data)
{
   struct gl_context *ctx = (struct gl_context *) data;
   size_t pos;

   _glthread_LOCK_MUTEX(ctx->Marshal.Mutex);

   while (ctx->Marshal.Shared.BatchQueue != NULL) {
      /* Remove the first batch from the queue. */
      struct gl_context_marshal_batch *batch = ctx->Marshal.Shared.BatchQueue;
      ctx->Marshal.Shared.BatchQueue = batch->Next;
      if (ctx->Marshal.Shared.BatchQueue == NULL)
         ctx->Marshal.Shared.BatchQueueTail = &ctx->Marshal.Shared.BatchQueue;

      /* Drop the mutex, execute it, and free it. */
      _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
      for (pos = 0; pos < batch->DwordsUsed; )
         pos += unmarshal_dispatch_cmd(ctx, &batch->Buffer[pos]);
      assert(pos == batch->DwordsUsed);
      free(batch->Buffer);
      free(batch);

      _glthread_LOCK_MUTEX(ctx->Marshal.Mutex);
   }

   ctx->Marshal.Shared.TaskComplete = GL_TRUE;

   _glthread_UNLOCK_MUTEX(ctx->Marshal.Mutex);
}


static inline void
post_marshal_hook(struct gl_context *ctx)
{
   if (execute_immediately)
      submit_batch(ctx);
}


static inline void
synchronize(struct gl_context *ctx)
{
   submit_batch(ctx);

   if (ctx->Marshal.Task != NULL) {
      _mesa_threadpool_wait_for_task(ctx->Shared->MarshalThreadPool,
                                     &ctx->Marshal.Task);
   }
}


static const GLubyte *GLAPIENTRY
marshal_GetString(GLenum name)
{
   GET_CURRENT_CONTEXT(ctx);
   synchronize(ctx);
   return CALL_GetString(ctx->Exec, (name));
}


struct cmd_Viewport
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_MatrixMode
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_LoadIdentity
{
   struct cmd_base cmd_base;
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
   (void) cmd;
   post_marshal_hook(ctx);
}


struct cmd_Ortho
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_PolygonMode
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_ClearColor
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_Clear
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_Color4f
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_Begin
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_EdgeFlag
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_Vertex2f
{
   struct cmd_base cmd_base;
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
   post_marshal_hook(ctx);
}


struct cmd_End
{
   struct cmd_base cmd_base;
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
   (void) cmd;
   post_marshal_hook(ctx);
}


static void GLAPIENTRY
marshal_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   synchronize(ctx);
   CALL_ReadPixels(ctx->Exec, (x, y, width, height, format, type, pixels));
}


struct cmd_Flush
{
   struct cmd_base cmd_base;
};


static inline void
unmarshal_Flush(struct gl_context *ctx, struct cmd_Flush *cmd)
{
   CALL_Flush(ctx->Exec, ());
}


static void GLAPIENTRY
marshal_Flush(void)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, Flush);
   (void) cmd;
   post_marshal_hook(ctx);
   synchronize(ctx); /* TODO: HACK to avoid problems with SwapBuffers */
}


static void GLAPIENTRY
marshal_GetIntegerv(GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   synchronize(ctx);
   CALL_GetIntegerv(ctx->Exec, (pname, params));
}


static GLuint GLAPIENTRY
marshal_CreateShader(GLenum type)
{
   GET_CURRENT_CONTEXT(ctx);
   synchronize(ctx);
   return CALL_CreateShader(ctx->Exec, (type));
}


struct cmd_ShaderSourceARB
{
   struct cmd_base cmd_base;
   GLhandleARB shaderObj;
   GLsizei count;
   /* Followed by GLint length[count], then the contents of all strings,
    * concatenated.
    */
};


static inline void
unmarshal_ShaderSourceARB(struct gl_context *ctx,
                          struct cmd_ShaderSourceARB *cmd)
{
   const GLint *cmd_length = (const GLint *) (cmd + 1);
   const GLcharARB *cmd_strings = (const GLchar *) (cmd_length + cmd->count);
   /* TODO: how to deal with malloc failure? */
   const GLcharARB * *string = malloc(cmd->count * sizeof(const GLcharARB *));
   int i;

   for (i = 0; i < cmd->count; ++i) {
      string[i] = cmd_strings;
      cmd_strings += cmd_length[i];
   }
   CALL_ShaderSourceARB(ctx->Exec,
                        (cmd->shaderObj, cmd->count, string, cmd_length));
   free(string);
}


static size_t
measure_ShaderSource_strings(GLsizei count, const GLcharARB **string,
                             const GLint *length_in, GLint *length_out)
{
   int i;
   size_t total_string_length = 0;

   for (i = 0; i < count; ++i) {
      if (length_in == NULL || length_in[i] < 0)
         length_out[i] = strlen(string[i]);
      else
         length_out[i] = length_in[i];
      total_string_length += length_out[i];
   }
   return total_string_length;
}


static void GLAPIENTRY
marshal_ShaderSourceARB(GLhandleARB shaderObj, GLsizei count,
                        const GLcharARB **string, const GLint *length)
{
   /* TODO: how to report an error if count < 0? */

   GET_CURRENT_CONTEXT(ctx);
   /* TODO: how to deal with malloc failure? */
   const size_t fixed_cmd_size = sizeof(struct cmd_ShaderSourceARB);
   STATIC_ASSERT(fixed_cmd_size % sizeof(GLint) == 0);
   size_t length_size = count * sizeof(GLint);
   GLint *length_tmp = malloc(length_size);
   size_t total_string_length =
      measure_ShaderSource_strings(count, string, length, length_tmp);
   size_t total_cmd_size = fixed_cmd_size + length_size + total_string_length;

   if (total_cmd_size <= MAX_CMD_SIZE) {
      struct cmd_ShaderSourceARB *cmd =
         allocate_command_in_queue(ctx, DISPATCH_CMD_ShaderSourceARB,
                                   total_cmd_size);
      GLint *cmd_length = (GLint *) (cmd + 1);
      GLcharARB *cmd_strings = (GLcharARB *) (cmd_length + count);
      int i;

      cmd->shaderObj = shaderObj;
      cmd->count = count;
      memcpy(cmd_length, length_tmp, length_size);
      for (i = 0; i < count; ++i) {
         memcpy(cmd_strings, string[i], cmd_length[i]);
         cmd_strings += cmd_length[i];
      }
      post_marshal_hook(ctx);
   } else {
      synchronize(ctx);
      CALL_ShaderSourceARB(ctx->Exec, (shaderObj, count, string, length_tmp));
   }
   free(length_tmp);
}


struct cmd_CompileShaderARB
{
   struct cmd_base cmd_base;
   GLhandleARB shaderObj;
};


static inline void
unmarshal_CompileShaderARB(struct gl_context *ctx,
                           struct cmd_CompileShaderARB *cmd)
{
   CALL_CompileShaderARB(ctx->Exec, (cmd->shaderObj));
}


static void GLAPIENTRY
marshal_CompileShaderARB(GLhandleARB shaderObj)
{
   GET_CURRENT_CONTEXT(ctx);
   QUEUE_SIMPLE_COMMAND(cmd, CompileShaderARB);
   cmd->shaderObj = shaderObj;
   post_marshal_hook(ctx);
}


static size_t
unmarshal_dispatch_cmd(struct gl_context *ctx, void *cmd)
{
   struct cmd_base *cmd_base = (struct cmd_base *) cmd;
   switch (cmd_base->cmd_id) {
   case DISPATCH_CMD_Viewport:
      unmarshal_Viewport(ctx, (struct cmd_Viewport *) cmd);
      break;
   case DISPATCH_CMD_MatrixMode:
      unmarshal_MatrixMode(ctx, (struct cmd_MatrixMode *) cmd);
      break;
   case DISPATCH_CMD_LoadIdentity:
      unmarshal_LoadIdentity(ctx, (struct cmd_LoadIdentity *) cmd);
      break;
   case DISPATCH_CMD_Ortho:
      unmarshal_Ortho(ctx, (struct cmd_Ortho *) cmd);
      break;
   case DISPATCH_CMD_PolygonMode:
      unmarshal_PolygonMode(ctx, (struct cmd_PolygonMode *) cmd);
      break;
   case DISPATCH_CMD_ClearColor:
      unmarshal_ClearColor(ctx, (struct cmd_ClearColor *) cmd);
      break;
   case DISPATCH_CMD_Clear:
      unmarshal_Clear(ctx, (struct cmd_Clear *) cmd);
      break;
   case DISPATCH_CMD_Color4f:
      unmarshal_Color4f(ctx, (struct cmd_Color4f *) cmd);
      break;
   case DISPATCH_CMD_Begin:
      unmarshal_Begin(ctx, (struct cmd_Begin *) cmd);
      break;
   case DISPATCH_CMD_EdgeFlag:
      unmarshal_EdgeFlag(ctx, (struct cmd_EdgeFlag *) cmd);
      break;
   case DISPATCH_CMD_Vertex2f:
      unmarshal_Vertex2f(ctx, (struct cmd_Vertex2f *) cmd);
      break;
   case DISPATCH_CMD_End:
      unmarshal_End(ctx, (struct cmd_End *) cmd);
      break;
   case DISPATCH_CMD_Flush:
      unmarshal_Flush(ctx, (struct cmd_Flush *) cmd);
      break;
   case DISPATCH_CMD_ShaderSourceARB:
      unmarshal_ShaderSourceARB(ctx, (struct cmd_ShaderSourceARB *) cmd);
      break;
   case DISPATCH_CMD_CompileShaderARB:
      unmarshal_CompileShaderARB(ctx, (struct cmd_CompileShaderARB *) cmd);
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
   SET_ShaderSourceARB(table, marshal_ShaderSourceARB);
   SET_CompileShaderARB(table, marshal_CompileShaderARB);

   return table;
}
