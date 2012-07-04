/*
 * Copyright © 2008, 2010 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file list.h
 * \brief Doubly-linked list abstract container type.
 *
 * Each doubly-linked list has a sentinel head and tail node.  These nodes
 * contain no data.  The head sentinel can be identified by its \c prev
 * pointer being \c NULL.  The tail sentinel can be identified by its
 * \c next pointer being \c NULL.
 *
 * A list is empty if either the head sentinel's \c next pointer points to the
 * tail sentinel or the tail sentinel's \c prev poiner points to the head
 * sentinel.
 *
 * Instead of tracking two separate \c node structures and a \c list structure
 * that points to them, the sentinel nodes are in a single structure.  Noting
 * that each sentinel node always has one \c NULL pointer, the \c NULL
 * pointers occupy the same memory location.  In the \c list structure
 * contains a the following:
 *
 *   - A \c head pointer that represents the \c next pointer of the
 *     head sentinel node.
 *   - A \c tail pointer that represents the \c prev pointer of the head
 *     sentinel node and the \c next pointer of the tail sentinel node.  This
 *     pointer is \b always \c NULL.
 *   - A \c tail_prev pointer that represents the \c prev pointer of the
 *     tail sentinel node.
 *
 * Therefore, if \c head->next is \c NULL or \c tail_prev->prev is \c NULL,
 * the list is empty.
 *
 * To anyone familiar with "exec lists" on the Amiga, this structure should
 * be immediately recognizable.  See the following link for the original Amiga
 * operating system documentation on the subject.
 *
 * http://www.natami.net/dev/Libraries_Manual_guide/node02D7.html
 *
 * \author Ian Romanick <ian.d.romanick@intel.com>
 */

#pragma once
#ifndef LIST_CONTAINER_H
#define LIST_CONTAINER_H

#ifndef __cplusplus
#include <stddef.h>
#endif
#include <assert.h>

#include "ralloc.h"

#ifdef __cplusplus

template<class node_type>
class typed_exec_list;

template<class node_type>
class typed_exec_node
{
public:
   node_type *next;
   node_type *prev;

#ifdef __cplusplus
   /* Callers of this ralloc-based new need not call delete. It's
    * easier to just ralloc_free 'ctx' (or any of its ancestors). */
   static void* operator new(size_t size, void *ctx)
   {
      void *node;

      node = ralloc_size(ctx, size);
      assert(node != NULL);

      return node;
   }

   /* If the user *does* call delete, that's OK, we will just
    * ralloc_free in that case. */
   static void operator delete(void *node)
   {
      ralloc_free(node);
   }

   typed_exec_node() : next(NULL), prev(NULL)
   {
      /* empty */
   }

   const node_type *get_next() const
   {
      return next;
   }

   node_type *get_next()
   {
      return next;
   }

   const node_type *get_prev() const
   {
      return prev;
   }

   node_type *get_prev()
   {
      return prev;
   }

   void remove()
   {
      next->prev = prev;
      prev->next = next;
      next = NULL;
      prev = NULL;
   }

   /**
    * Link a node with itself
    *
    * This creates a sort of degenerate list that is occasionally useful.
    */
   void self_link()
   {
      next = (node_type *) this;
      prev = (node_type *) this;
   }

   /**
    * Insert a node in the list after the current node
    */
   void insert_after(node_type *after)
   {
      after->next = this->next;
      after->prev = (node_type *) this;

      this->next->prev = after;
      this->next = after;
   }
   /**
    * Insert a node in the list before the current node
    */
   void insert_before(node_type *before)
   {
      before->next = (node_type *) this;
      before->prev = this->prev;

      this->prev->next = before;
      this->prev = before;
   }

   /**
    * Insert another list in the list before the current node
    */
   void insert_before(typed_exec_list<node_type> *before);

   /**
    * Replace the current node with the given node.
    */
   void replace_with(node_type *replacement)
   {
      replacement->prev = this->prev;
      replacement->next = this->next;

      this->prev->next = replacement;
      this->next->prev = replacement;
   }

   /**
    * Is this the sentinel at the tail of the list?
    */
   bool is_tail_sentinel() const
   {
      return this->next == NULL;
   }

   /**
    * Is this the sentinel at the head of the list?
    */
   bool is_head_sentinel() const
   {
      return this->prev == NULL;
   }
#endif
};


#ifdef __cplusplus
/* This macro will not work correctly if `t' uses virtual inheritance.  If you
 * are using virtual inheritance, you deserve a slow and painful death.  Enjoy!
 */
#define exec_list_offsetof(t, f, p) \
   (((char *) &((t *) p)->f) - ((char *) p))
#else
#define exec_list_offsetof(t, f, p) offsetof(t, f)
#endif

//struct exec_node;


template<class node_type>
class typed_exec_list
{
public:
   node_type *head;
   node_type *tail;
   node_type *tail_pred;

#ifdef __cplusplus
   /* Callers of this ralloc-based new need not call delete. It's
    * easier to just ralloc_free 'ctx' (or any of its ancestors). */
   static void* operator new(size_t size, void *ctx)
   {
      void *node;

      node = ralloc_size(ctx, size);
      assert(node != NULL);

      return node;
   }

   /* If the user *does* call delete, that's OK, we will just
    * ralloc_free in that case. */
   static void operator delete(void *node)
   {
      ralloc_free(node);
   }

   typed_exec_list()
   {
      make_empty();
   }

   void make_empty()
   {
      head = (node_type *) & tail;
      tail = NULL;
      tail_pred = (node_type *) & head;
   }

   bool is_empty() const
   {
      /* There are three ways to test whether a list is empty or not.
       *
       * - Check to see if the \c head points to the \c tail.
       * - Check to see if the \c tail_pred points to the \c head.
       * - Check to see if the \c head is the sentinel node by test whether its
       *   \c next pointer is \c NULL.
       *
       * The first two methods tend to generate better code on modern systems
       * because they save a pointer dereference.
       */
      return head == (node_type *) &tail;
   }

   const node_type *get_head() const
   {
      return !is_empty() ? head : NULL;
   }

   node_type *get_head()
   {
      return !is_empty() ? head : NULL;
   }

   const node_type *get_tail() const
   {
      return !is_empty() ? tail_pred : NULL;
   }

   node_type *get_tail()
   {
      return !is_empty() ? tail_pred : NULL;
   }

   void push_head(node_type *n)
   {
      n->next = head;
      n->prev = (node_type *) &head;

      n->next->prev = n;
      head = n;
   }

   void push_tail(node_type *n)
   {
      n->next = (node_type *) &tail;
      n->prev = tail_pred;

      n->prev->next = n;
      tail_pred = n;
   }

   void push_degenerate_list_at_head(node_type *n)
   {
      assert(n->prev->next == n);

      n->prev->next = head;
      head->prev = n->prev;
      n->prev = (node_type *) &head;
      head = n;
   }

   /**
    * Remove the first node from a list and return it
    *
    * \return
    * The first node in the list or \c NULL if the list is empty.
    *
    * \sa exec_list::get_head
    */
   node_type *pop_head()
   {
      node_type *const n = this->get_head();
      if (n != NULL)
	 n->remove();

      return n;
   }

   /**
    * Move all of the nodes from this list to the target list
    */
   void move_nodes_to(typed_exec_list<node_type> *target)
   {
      if (is_empty()) {
	 target->make_empty();
      } else {
	 target->head = head;
	 target->tail = NULL;
	 target->tail_pred = tail_pred;

	 target->head->prev = (node_type *) &target->head;
	 target->tail_pred->next = (node_type *) &target->tail;

	 make_empty();
      }
   }

   /**
    * Append all nodes from the source list to the target list
    */
   void
   append_list(typed_exec_list<node_type> *source)
   {
      if (source->is_empty())
	 return;

      /* Link the first node of the source with the last node of the target list.
       */
      this->tail_pred->next = source->head;
      source->head->prev = this->tail_pred;

      /* Make the tail of the source list be the tail of the target list.
       */
      this->tail_pred = source->tail_pred;
      this->tail_pred->next = (node_type *) &this->tail;

      /* Make the source list empty for good measure.
       */
      source->make_empty();
   }
#endif
};


/* Normally this would be a typedef but since we're mixing C and C++ we have
 * to make it a struct.
 */
struct exec_node : public typed_exec_node<exec_node>
{
};


/* Normally this would be a typedef but since we're mixing C and C++ we have
 * to make it a struct.
 */
struct exec_list : public typed_exec_list<exec_node>
{
};


#ifdef __cplusplus
template<class node_type>
inline void
typed_exec_node<node_type>::insert_before(typed_exec_list<node_type> *before)
{
   if (before->is_empty())
      return;

   before->tail_pred->next = (node_type *) this;
   before->head->prev = this->prev;

   this->prev->next = before->head;
   this->prev = before->tail_pred;

   before->make_empty();
}
#endif

/**
 * This version is safe even if the current node is removed.
 */ 
#define foreach_list_safe(__node, __list)			     \
   for (exec_node * __node = (__list)->head, * __next = __node->next \
	; __next != NULL					     \
	; __node = __next, __next = __next->next)

#define foreach_list(__node, __list)			\
   for (exec_node * __node = (__list)->head		\
	; (__node)->next != NULL 			\
	; (__node) = (__node)->next)

#define foreach_list_const(__node, __list)		\
   for (const exec_node * __node = (__list)->head	\
	; (__node)->next != NULL 			\
	; (__node) = (__node)->next)

#endif /* LIST_CONTAINER_H */

#endif
