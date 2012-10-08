#!/usr/bin/env python

# Copyright (C) 2012 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

import license
import gl_XML
import sys, getopt

header = """
#include "marshal_generatable.h"

#include "api_exec.h"
#include "context.h"
#include "dispatch.h"
#include "marshal.h"
"""

footer = """/* FOOTER */
"""


class PrintCode(gl_XML.gl_print_base):

	def __init__(self):
		gl_XML.gl_print_base.__init__(self)

		self.name = "gl_marshal.py (from Mesa)"
		self.license = license.bsd_license_template % (
                        "Copyright (C) 2012 Intel Corporation",
                        "INTEL CORPORATION")

		return


	def get_stack_size(self, f):
                raise Exception("Unneeded?")
		size = 0
		for p in f.parameterIterator():
			if p.is_padding:
				continue

			size += p.get_stack_size()

		return size


	def printRealHeader(self):
		print header
		return


	def printRealFooter(self):
		print footer
		return


        def classify_function(self, f):
                # TODO: should go into function class
                if f.name == 'Flush':
                        # TODO: since we don't have any hook into
                        # SwapBuffers, we have to do a synchronous
                        # flush.
                        return 'sync'
                if f.name == 'Finish':
                        return 'sync'
                if f.return_type != 'void':
                        return 'sync'
                for p in f.parameters:
                        if p.is_output:
                                return 'sync'
                        if p.is_pointer() and not (p.count or p.counter):
                                return 'sync'
                        if p.count_parameter_list:
                                # Parameter size is determined by
                                # enums; don't know how to do this
                                # yet.
                                return 'sync'
                return 'async'


        def print_sync_call(self, func, indent):
                call = 'CALL_{0}(ctx->CurrentServerDispatch, ({1}))'.format(
                        func.name, func.get_called_parameter_string())
                if func.return_type == 'void':
                        print indent + '{0};'.format(call)
                else:
                        print indent + 'return {0};'


        def print_sync_dispatch(self, func, indent):
                print indent + '_mesa_marshal_synchronize(ctx);'
                self.print_sync_call(func, indent)


        def print_sync_body(self, func):
                print '/* {0}: marshalled synchronously */'.format(func.name)
                print 'static {0} GLAPIENTRY'.format(func.return_type)
                print 'marshal_{0}({1})'.format(
                        func.name, func.get_parameter_string())
                print '{'
                print '   GET_CURRENT_CONTEXT(ctx);'
                self.print_sync_call(func, '   ')
                print '}'
                print ''
                print ''


        def print_async_dispatch(self, func, struct, fixed_params,
                                 variable_params, indent):
                def pr(s):
                        print indent + s
                pr('{0} *cmd ='.format(struct))
                pr(('   _mesa_allocate_command_in_queue(ctx, '
                    'DISPATCH_CMD_{0}, cmd_size);').format(func.name))
                for p in fixed_params:
                        pr('cmd->{0} = {0};'.format(p.name))
                if variable_params:
                        pr('char *variable_data = (char *) (cmd + 1);')
                        for p in variable_params:
                                pr(('memcpy(variable_data, '
                                    '{0}, {1});').format(
                                                p.name, p.size_string(False)))
                                pr('variable_data += {0};'.format(
                                                p.size_string(False)))
                if not fixed_params and not variable_params:
                        pr('(void) cmd;\n')
                pr('_mesa_post_marshal_hook(ctx);')


        def print_async_body(self, func):
                fixed_params = []
                variable_params = []
                for p in func.parameters:
                        if p.is_padding:
                                continue
                        if p.is_variable_length():
                                variable_params.append(p)
                        else:
                                fixed_params.append(p)
                print '/* {0}: marshalled asynchronously */'.format(func.name)
                print 'struct marshal_cmd_{0}'.format(func.name)
                print '{'
                print '   struct marshal_cmd_base cmd_base;'
                for p in fixed_params:
                        if p.count:
                                print '   {0} {1}[{2}];'.format(
                                        p.get_base_type_string(),
                                        p.name, p.count)
                        else:
                                print '   {0} {1};'.format(
                                        p.type_string(), p.name)
                # TODO: when there are variable parameters, insert
                # padding if necessary for alignment.
                for p in variable_params:
                        if p.count_scale != 1:
                                print ('   /* Next {0} bytes are '
                                       '{1} {2}[{3}][{4}] */').format(
                                        p.size_string(),
                                        p.get_base_type_string(),
                                        p.name, p.counter, p.count_scale)
                        else:
                                print ('   /* Next {0} bytes are '
                                       '{1} {2}[{3}] */').format(
                                        p.size_string(),
                                        p.get_base_type_string(),
                                        p.name, p.counter)
                print '};'
                print 'static inline void'
                print ('unmarshal_{0}(struct gl_context *ctx, '
                       'const struct marshal_cmd_{0} *cmd)').format(func.name)
                print '{'
                for p in fixed_params:
                        if p.count:
                                print '   const {0} * {1} = cmd->{1};'.format(
                                        p.get_base_type_string(),
                                        p.name)
                        else:
                                print '   const {0} {1} = cmd->{1};'.format(
                                        p.type_string(), p.name)
                if variable_params:
                        for p in variable_params:
                                print '   const {0} * {1};'.format(
                                        p.get_base_type_string(),
                                        p.name)
                        print ('   const char *variable_data = '
                               '(const char *) (cmd + 1);')
                        for p in variable_params:
                                print ('   {0} = (const {1} *) '
                                       'variable_data;').format(
                                        p.name, p.get_base_type_string())
                                print '   variable_data += {0};'.format(
                                        p.size_string(False))
                self.print_sync_call(func, '   ')
                print '}'
                print 'static void GLAPIENTRY'
                print 'marshal_{0}({1})'.format(
                        func.name, func.get_parameter_string())
                print '{'
                print '   GET_CURRENT_CONTEXT(ctx);'
                struct = 'struct marshal_cmd_{0}'.format(func.name)
                size_terms = ['sizeof({0})'.format(struct)]
                for p in variable_params:
                        size_terms.append(p.size_string())
                print '   size_t cmd_size = {0};'.format(
                        ' + '.join(size_terms))
                if variable_params:
                        print '   if (cmd_size <= MARSHAL_MAX_CMD_SIZE) {'
                        self.print_async_dispatch(func, struct, fixed_params,
                                                  variable_params, '      ')
                        print '   } else {'
                        self.print_sync_dispatch(func, '      ')
                        print '   }'
                else:
                        self.print_async_dispatch(func, struct, fixed_params,
                                                  variable_params, '   ')
                print '}'
                print ''
                print ''


        def print_unmarshal_dispatch_cmd(self, funcs):
                print 'size_t'
                print ('_mesa_unmarshal_dispatch_cmd(struct gl_context *ctx, '
                       'const void *cmd)')
                print '{'
                print '   const struct marshal_cmd_base *cmd_base = cmd;'
                print '   switch (cmd_base->cmd_id) {'
                for func in funcs:
                        print '   case DISPATCH_CMD_{0}:'.format(func.name)
                        print ('      unmarshal_{0}(ctx, '
                               '(const struct marshal_cmd_{0} *)'
                               ' cmd);').format(func.name)
                        print '      break;'
                print '   default:'
                print '      assert(!"Unrecognized command ID");'
                print '      break;'
                print '   }'
                print ''
                print '   return cmd_base->cmd_size;'
                print '}'
                print ''
                print ''


	def printBody(self, api):
                async_funcs = []
                for func in api.functionIterateAll():
                        classification = self.classify_function(func)
                        if classification == 'sync':
                                self.print_sync_body(func)
                        elif classification == 'async':
                                self.print_async_body(func)
                                async_funcs.append(func)
                        else:
                                raise Exception(
                                        'Unexpected classification {0}'.format(
                                                classification))
                self.print_unmarshal_dispatch_cmd(async_funcs)


def show_usage():
	print "Usage: %s [-f input_file_name]" % sys.argv[0]
	sys.exit(1)

if __name__ == '__main__':
	file_name = "gl_API.xml"

	try:
		(args, trail) = getopt.getopt(sys.argv[1:], "m:f:")
	except Exception,e:
		show_usage()

	for (arg,val) in args:
		if arg == "-f":
			file_name = val

	printer = PrintCode()

	api = gl_XML.parse_GL_API(file_name)
	printer.Print(api)
