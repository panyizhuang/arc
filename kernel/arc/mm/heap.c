/*
 * Copyright (c) 2011-2012 Graham Edgecombe <graham@grahamedgecombe.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <arc/mm/heap.h>
#include <arc/lock/spinlock.h>
#include <arc/mm/pmm.h>
#include <arc/mm/vmm.h>
#include <arc/mm/align.h>
#include <arc/pack.h>
#include <arc/panic.h>
#include <stdbool.h>
#include <stdint.h>

/* the states a heap node can be in */
#define HEAP_NODE_FREE      0 /* not allocated */
#define HEAP_NODE_RESERVED  1 /* allocated, physical frames not managed by us */
#define HEAP_NODE_ALLOCATED 2 /* allocated, physical frames managed by us */

typedef PACK(struct heap_node
{
  struct heap_node *next;
  struct heap_node *prev;
  int state;
  uintptr_t start; /* the address of the first page, inclusive */
  uintptr_t end;   /* the address of the last page, exclusive */
}) heap_node_t;

static heap_node_t *heap_root;
static spinlock_t heap_lock;

void heap_init(void)
{
  /* find where the kernel image ends and the heap starts (inclusive) */
  extern int _end;
  uintptr_t heap_start = PAGE_ALIGN_2M((uintptr_t) &_end);

  /* hard coded end of the heap (exclusive) */
  uintptr_t heap_end = VM_STACK_OFFSET;

  /* allocate some space for the root node */
  uintptr_t root_phy = pmm_alloc();
  if (!root_phy)
    boot_panic("couldn't allocate physical frame for heap root node");

  /* sanity check which probably seems completely ridiculous */
  if (heap_start >= heap_end)
    boot_panic("no room for heap");

  /* the root node will take the first virtual address */
  heap_root = (heap_node_t *) heap_start;
  if (!vmm_map(heap_start, root_phy, PG_WRITABLE | PG_NO_EXEC))
    boot_panic("couldn't map heap root node into the virtual memory");

  /* fill out the root node */
  heap_root->next = 0;
  heap_root->prev = 0;
  heap_root->state = HEAP_NODE_FREE;
  heap_root->start = heap_start + FRAME_SIZE;
  heap_root->end = heap_end;
}

static heap_node_t *find_node(size_t size)
{
  /* look for the first node that will fit the requested size */
  for (heap_node_t *node = heap_root; node; node = node->next)
  {
    /* skip free nodes */
    if (node->state != HEAP_NODE_FREE)
      continue;

    /* skip nodes that are too small */
    size_t node_size = node->end - node->start;
    if (node_size < size)
      continue;

    /* check if splitting the node would actually leave some space */
    size_t extra_size = node_size - size;
    if (extra_size >= (FRAME_SIZE * 2))
    {
      /* only split the node if we can allocate a physical page */
      uintptr_t phy = pmm_alloc();
      if (phy)
      {
        /* map the new heap_node_t into virtual memory, only split if it works */
        heap_node_t *next = (heap_node_t *) ((uintptr_t) node + size + FRAME_SIZE);
        if (vmm_map((uintptr_t) next, phy, PG_WRITABLE | PG_NO_EXEC))
        {
          /* fill in the new heap_node_t */
          next->start = (uintptr_t) node + size + FRAME_SIZE * 2;
          next->end = node->end;
          next->state = HEAP_NODE_FREE;
          next->prev = node;
          next->next = node->next;

          /* update the node that was split */
          node->end = (uintptr_t) next;

          /* update the surrounding nodes */
          node->next = next;
          if (next->next)
            next->next->prev = next;
        }
      }
    }

    /* update the state of the allocated node */
    node->state = HEAP_NODE_RESERVED;
    return node;
  }

  return 0;
}

static void _heap_free(void *ptr)
{
  /* find where the node is */
  heap_node_t *node = (heap_node_t *) ((uintptr_t) ptr - FRAME_SIZE);

  /* free the physical frames if heap_alloc allocated them */
  if (node->state == HEAP_NODE_ALLOCATED)
  {
    for (uintptr_t page = node->start; page < node->end; page += FRAME_SIZE)
    {
      uintptr_t phy = vmm_unmap(page);
      if (phy) /* frames that weren't mapped yet are NULL */
        pmm_free(phy);
    }
  }

  /* set the node's state to free */
  node->state = HEAP_NODE_FREE;

  /* try to coalesce with the next node */
  heap_node_t *next = node->next;
  if (next && next->state == HEAP_NODE_FREE)
  {
    /* update the pointers */
    node->next = next->next;
    if (next->next)
      next->next->prev = node;

    /* update the address range */
    node->end = next->end;

    /* unmap and free the physical frame behind the next node */
    pmm_free(vmm_unmap((uintptr_t) next));
  }

  /* try to coalesce with the previous node */
  heap_node_t *prev = node->prev;
  if (prev && prev->state == HEAP_NODE_FREE)
  {
    /* update the pointers */
    prev->next = node->next;
    if (node->next)
      node->next->prev = prev;

    /* update the address range */
    prev->end = node->end;

    /* unmap and free the physical frame behind the next node */
    pmm_free(vmm_unmap((uintptr_t) next));
  }
}

static void *_heap_alloc(size_t size, int flags, bool phy_alloc)
{
  /* round up the size such that it is a multiple of the page size */
  size = PAGE_ALIGN(size);

  /* find a node that can satisfy the size */
  heap_node_t *node = find_node(size);
  if (!node)
    return node;

  if (phy_alloc)
  {
    /* change the state to allocated so heap_free releases the frames */
    node->state = HEAP_NODE_ALLOCATED;

    /* translate HEAP_W and HEAP_X flags to PG_WRITABLE and PG_NO_EXEC flags */
    uint64_t map_flags = 0;

    if (flags & HEAP_W)
      map_flags |= PG_WRITABLE;

    if (!(flags & HEAP_X))
      map_flags |= PG_NO_EXEC;

    /* allocate physical frames and map them into memory */
    uintptr_t end = node->start + size;
    for (uintptr_t page = node->start; page < end; page += FRAME_SIZE)
    {
      /* allocate a physical frame */
      uintptr_t phy = pmm_alloc();

      /* if the physical allocation fails, roll back our changes */
      if (!phy)
      {
        _heap_free(node);
        return 0;
      }

      /*
       * otherwise map the physical frame into the virtual address space, roll
       * back our changes if this fails
       */
      if (!vmm_map(page, phy, map_flags))
      {
        _heap_free(node);
        return 0;
      }
    }
  }

  return (void *) ((uintptr_t) node + FRAME_SIZE);
}

void *heap_reserve(size_t size)
{
  spin_lock(&heap_lock);
  void *ptr = _heap_alloc(size, 0, false);
  spin_unlock(&heap_lock);
  return ptr;
}

void *heap_alloc(size_t size, int flags)
{
  spin_lock(&heap_lock);
  void *ptr = _heap_alloc(size, flags, true);
  spin_unlock(&heap_lock);
  return ptr;
}

void heap_free(void *ptr)
{
  spin_lock(&heap_lock);
  _heap_free(ptr);
  spin_unlock(&heap_lock);
}
