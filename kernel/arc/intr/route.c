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

#include <arc/intr/route.h>
#include <arc/intr/ic.h>
#include <arc/intr/ioapic.h>
#include <arc/lock/rwlock.h>
#include <arc/lock/intr.h>
#include <arc/tty.h>
#include <arc/panic.h>
#include <stdlib.h>

typedef struct intr_handler_node
{
  intr_handler_t handler;
  struct intr_handler_node *next;
} intr_handler_node_t;

static rwlock_t intr_route_lock = RWLOCK_UNLOCKED;
static intr_handler_node_t *intr_handlers[INTERRUPTS];

void intr_dispatch(intr_state_t *state)
{
  /* acknowledge we received this interrupt if it isn't a fault and spurious */
  intr_t intr = state->id;
  if (intr > FAULT31 && intr != SPURIOUS)
    ic_ack(intr);

  /* if there is no handler panic (this is for debugging purposes) */
  rw_rlock(&intr_route_lock);
  intr_handler_node_t *head = intr_handlers[state->id];
  if (!head)
    panic("unhandled interrupt %d\n", state->id);

  /* call all the handlers */
  for (intr_handler_node_t *node = head; node; node = node->next)
  {
    intr_handler_t handler = node->handler;
    (*handler)(state);
  }
  rw_runlock(&intr_route_lock);
}

void intr_route_init(void)
{
  ic_print_info();

  for (ioapic_t *apic = ioapic_iter(); apic; apic = apic->next)
  {
    uint64_t addr = apic->_phy_addr;
    ioapic_id_t id = apic->id;
    irq_t irq_first = apic->irq_base;
    irq_t irq_last = apic->irq_base + apic->irqs - 1;
    tty_printf(" => Using I/O APIC (at %0#18x, id %0#4x, irqs %d-%d)\n", addr, id, irq_first, irq_last);
  }
}

static bool _intr_route_intr(intr_t intr, intr_handler_t handler)
{
  /* allocate the handler node */
  intr_handler_node_t *tail_node = malloc(sizeof(*tail_node));
  if (!tail_node)
    return false;

  /* fill it out */
  tail_node->handler = handler;

  /* cons it onto the handler list */
  tail_node->next = intr_handlers[intr];
  intr_handlers[intr] = tail_node;

  /* all done! */
  return true;
}

static void _intr_unroute_intr(intr_t intr, intr_handler_t handler)
{
  /* find the handler node corresponding to the given handler */
  for (intr_handler_node_t *prev = 0, *node = intr_handlers[intr]; node; prev = node, node = node->next)
  {
    if (node->handler == handler)
    {
      /* unlink it from the list */
      if (prev == 0)
        intr_handlers[intr] = node->next;
      else
        prev->next = node->next;

      /* free the memory used by the list node */
      free(node);

      /* all done! */
      return;
    }
  }
}

bool intr_route_intr(intr_t intr, intr_handler_t handler)
{
  intr_lock();
  rw_wlock(&intr_route_lock);
  bool ok = _intr_route_intr(intr, handler);
  rw_wunlock(&intr_route_lock);
  intr_unlock();
  return ok;
}

void intr_unroute_intr(intr_t intr, intr_handler_t handler)
{
  intr_lock();
  rw_wlock(&intr_route_lock);
  _intr_unroute_intr(intr, handler);
  rw_wunlock(&intr_route_lock);
  intr_unlock();
}

bool intr_route_irq(irq_tuple_t *tuple, intr_handler_t handler)
{
  /* calculate the interrupt number */
  irq_t irq = tuple->irq;
  intr_t intr = (irq % IRQS) + IRQ0;

  /* iterate through the I/O APICs */
  intr_lock();
  rw_wlock(&intr_route_lock);
  for (ioapic_t *apic = ioapic_iter(); apic; apic = apic->next)
  {
    /* check if the IRQ belongs to this I/O APIC */
    irq_t irq_first = apic->irq_base;
    irq_t irq_last = apic->irq_base + apic->irqs - 1;
    if (irq >= irq_first && irq < irq_last)
    {
      /* route the interrupt */
      bool ok = _intr_route_intr(intr, handler);

      /* program the I/O APIC */
      ioapic_route(apic, tuple, intr);

      /* all done! */
      rw_wunlock(&intr_route_lock);
      intr_unlock();
      return ok;
    }
  }

  rw_wunlock(&intr_route_lock);
  intr_unlock();
  return false;
}

void intr_unroute_irq(irq_tuple_t *tuple, intr_handler_t handler)
{
  /* calculate the interrupt number */
  irq_t irq = tuple->irq;
  intr_t intr = (irq % IRQS) + IRQ0;

  /* iterate through the I/O APICs */
  intr_lock();
  rw_wlock(&intr_route_lock);
  for (ioapic_t *apic = ioapic_iter(); apic; apic = apic->next)
  {
    /* check if the IRQ belongs to this I/O APIC */
    irq_t irq_first = apic->irq_base;
    irq_t irq_last = apic->irq_base + apic->irqs - 1;
    if (irq >= irq_first && irq < irq_last)
    {
      /* program the I/O APIC */
      ioapic_mask(apic, tuple);
    }
  }

  /* unroute this interrupt */
  _intr_unroute_intr(intr, handler);
  rw_wunlock(&intr_route_lock);
  intr_unlock();
}

