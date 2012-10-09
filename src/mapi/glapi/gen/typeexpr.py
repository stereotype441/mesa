#!/usr/bin/env python

# (C) Copyright IBM Corporation 2005
# All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# on the rights to use, copy, modify, merge, publish, distribute, sub
# license, and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
# IBM AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Authors:
#    Ian Romanick <idr@us.ibm.com>

import string, copy

class type_node(object):
        """A portion of a C type: either a base type (e.g. "unsigned
        int"), or a layer of indirection (a pointer).
        """
	def __init__(self):
		self.pointer = 0  # bool
		self.const = 0    # bool
		self.signed = 1   # bool (only meaningful for integers)
		self.integer = 1  # bool (ignored for pointers)

		# Number of array elements if this field is an
		# array; otherwise 0.
		self.elements = 0

                # base name (e.g. "int" for "unsigned int").  None for
                # pointers.
		self.name = None

		self.size = 0     # type's size in bytes.  0 for pointers.
		return


	def string(self):
		"""Return the C representation of this type_node.

                Array size is ignored.
                """
		s = ""
		
		if self.pointer:
			s = "* "

		if self.const:
			s += "const "

		if not self.pointer:
			if self.integer:
				if self.signed:
					s += "signed "
				else:
					s += "unsigned "

			if self.name:
				s += "%s " % (self.name)

		return s


class type_table(object):
        """Table of types that is searchable by base name."""
	def __init__(self):
		self.types_by_name = {}
		return


	def add_type(self, type_expr):
		self.types_by_name[ type_expr.get_base_name() ] = type_expr
		return


	def find_type(self, name):
		if name in self.types_by_name:
			return self.types_by_name[ name ]
		else:
			return None


def create_initial_types():
        """Populate type_expression.built_in_types with the standard
        set of GL base types.
        """
	tt = type_table()

	basic_types = [
		("char",   1, 1),
		("short",  2, 1),
		("int",    4, 1),
		("long",   4, 1),
		("float",  4, 0),
		("double", 8, 0),
		("enum",   4, 1)
	]

	for (type_name, type_size, integer) in basic_types:
		te = type_expression(None)
		tn = type_node()
		tn.name = type_name
		tn.size = type_size
		tn.integer = integer
		te.expr.append(tn)
		tt.add_type( te )

	type_expression.built_in_types = tt
	return


class type_expression(object):
        """Represents a complete C type."""
	built_in_types = None

	def __init__(self, type_string, extra_types = None):
                """Construct based on a C representation of the type.

                extra_types, if present, is a type_table containing
                base types that should be recognized while parsing.

                Pass an empty type_string to construct an empty
                type_expression.
                """
                # List of type_node objects; the first (always
                # present) is the base type, the remaining objects (if
                # present) represent levels of pointer indirection.
		self.expr = []

		if not type_string:
			return

		self.original_string = type_string

		if not type_expression.built_in_types:
			raise RuntimeError("create_initial_types must be called before creating type_expression objects.")

		# Replace '*' with ' * ' in type_string.  Then, split the string
		# into tokens, separated by spaces.
		tokens = string.split( string.replace( type_string, "*", " * " ) )

		const = 0
		t = None
		signed = 0
		unsigned = 0

		for i in tokens:
			if i == "const":
				if t and t.pointer:
					t.const = 1
				else:
					const = 1
			elif i == "signed":
				signed = 1
			elif i == "unsigned":
				unsigned = 1
			elif i == "*":
				# This is a quirky special-case because of the
				# way the C works for types.  If 'unsigned' is
				# specified all by itself, it is treated the
				# same as "unsigned int".

				if unsigned:
					self.set_base_type( "int", signed, unsigned, const, extra_types )
					const = 0
					signed = 0
					unsigned = 0

				if not self.expr:
					raise RuntimeError("Invalid type expression (dangling pointer)")

				if signed:
					raise RuntimeError("Invalid type expression (signed / unsigned applied to pointer)")

				t = type_node()
				t.pointer = 1
				self.expr.append( t )
			else:
				if self.expr:
					raise RuntimeError('Invalid type expression (garbage after pointer qualifier -> "%s")' % (self.original_string))

				self.set_base_type( i, signed, unsigned, const, extra_types )
				const = 0
				signed = 0
				unsigned = 0

			if signed and unsigned:
				raise RuntimeError("Invalid type expression (both signed and unsigned specified)")
				

		if const:
			raise RuntimeError("Invalid type expression (dangling const)")

		if unsigned:
			raise RuntimeError("Invalid type expression (dangling signed)")

		if signed:
			raise RuntimeError("Invalid type expression (dangling unsigned)")

		return


	def set_base_type(self, type_name, signed, unsigned, const, extra_types):
                """Replace the contents of this type by looking up the
                type having name type_name, and apply signed,
                unsigned, and const modifiers if present.
                """
		te = type_expression.built_in_types.find_type( type_name )
		if not te:
			te = extra_types.find_type( type_name )

		if not te:
			raise RuntimeError('Unknown base type "%s".' % (type_name))

		self.expr = copy.deepcopy(te.expr)

		t = self.expr[ len(self.expr) - 1 ]
		t.const = const
		if signed:
			t.signed = 1
		elif unsigned:
			t.signed = 0


	def set_base_type_node(self, tn):
                """Replace the contents of this type with the given
                type_node.
                """
		self.expr = [tn]
		return


	def set_elements(self, count):
                """Set the number of array elements in this type; this
                is tracked in the first type_node object.
                """
		tn = self.expr[0]

		tn.elements = count
		return


	def string(self):
                """Return the C representation of this type.

                Array size is ignored.
                """
		s = ""
		for t in self.expr:
			s += t.string()

		return s


	def get_base_type_node(self):
		return self.expr[0]


	def get_base_name(self):
		if len(self.expr):
			return self.expr[0].name
		else:
			return None


	def get_element_size(self):
                """Get the size of the base type, accounting for array
                size if present.
                """
		tn = self.expr[0]

		if tn.elements:
			return tn.elements * tn.size
		else:
			return tn.size


	def get_element_count(self):
		tn = self.expr[0]
		return tn.elements


	def get_stack_size(self):
                """Find out how much space this type takes up on the
                stack, assuming a 32-bit ABI, and accounting for the
                padding necessary to align types to 32-bit boundaries.
                """
		tn = self.expr[ -1 ]

		if tn.elements or tn.pointer:
			return 4
		elif not tn.integer:
			return tn.size
		else:
			return 4


	def is_pointer(self):
		tn = self.expr[ -1 ]
		return tn.pointer


	def format_string(self):
                """Return a printf format string suitable for printing
                values of this type.

                Note: since GLenum is synonymous with int, this
                returns "%d" for GLenum.
                """
		tn = self.expr[ -1 ]
		if tn.pointer:
			return "%p"
		elif not tn.integer:
			return "%f"
		else:
			return "%d"



if __name__ == '__main__':
	
	types_to_try = [ "int", "int *", "const int *", "int * const", "const int * const", \
	                 "unsigned * const *", \
			 "float", "const double", "double * const"]

	create_initial_types()

	for t in types_to_try:
		print 'Trying "%s"...' % (t)
		te = type_expression( t )
		print 'Got "%s" (%u, %u).' % (te.string(), te.get_stack_size(), te.get_element_size())
