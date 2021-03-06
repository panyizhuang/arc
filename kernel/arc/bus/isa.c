/*
 * Copyright (c) 2011-2014 Graham Edgecombe <graham@grahamedgecombe.com>
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

#include <arc/bus/isa.h>
#include <arc/cpu/port.h>
#include <arc/trace.h>
#include <arc/panic.h>
#include <string.h>

static irq_tuple_t isa_irqs[ISA_INTR_LINES];

void isa_init(void)
{
  /*
   * The default mapping of ISA interrupt lines to GSI numbers is 1:1 but they
   * can also be overridden by MADT entries.
   *
   * ISA IRQs are edge-triggered and active high by default.
   */
  for (int line = 0; line < ISA_INTR_LINES; line++)
  {
    irq_tuple_t *tuple = &isa_irqs[line];
    tuple->irq = line;
    tuple->active_polarity = POLARITY_HIGH;
    tuple->trigger = TRIGGER_EDGE;
  }
}

irq_tuple_t *isa_irq(isa_line_t line)
{
  if (line >= ISA_INTR_LINES)
    panic("invalid ISA interrupt line %d", line);

  return &isa_irqs[line];
}
