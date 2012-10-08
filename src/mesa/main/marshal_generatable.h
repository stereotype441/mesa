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

#ifndef MARSHAL_GENERATABLE_H
#define MARSHAL_GENERATABLE_H

enum marshal_dispatch_cmd_id
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
   DISPATCH_CMD_AttachShader,
   DISPATCH_CMD_LinkProgramARB,
   DISPATCH_CMD_DeleteShader,
   DISPATCH_CMD_UseProgramObjectARB,
   DISPATCH_CMD_Uniform1fvARB,
   DISPATCH_CMD_Uniform1iARB,
   DISPATCH_CMD_VertexPointer,
   DISPATCH_CMD_EnableClientState,
   DISPATCH_CMD_DisableClientState,
};


#endif /* MARSHAL_GENERATABLE_H */
