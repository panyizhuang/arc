#
#  Copyright (c) 2011-2012 Graham Edgecombe <graham@grahamedgecombe.com>
#
#  Permission to use, copy, modify, and/or distribute this software for any
#  purpose with or without fee is hereby granted, provided that the above
#  copyright notice and this permission notice appear in all copies.
#
#  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
#  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
#  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

ARCH=x86_64-pc-elf

CC=$(ARCH)-gcc
CFLAGS=-std=c1x -O3 -Wall -pedantic -ffreestanding -mno-red-zone \
       -mcmodel=large -Iinclude -mno-sse -mno-sse2 -mno-sse3 -mno-3dnow \
       -mno-mmx

AS=nasm
ASFLAGS=-f elf64 -Ox

LD=$(ARCH)-ld
LDFLAGS=-Tarc.lds -z max-page-size=0x1000

AR=$(ARCH)-ar

TARGET=arc
SOURCES=$(shell find src -name "*.c" -or -name "*.s" -type f)
OBJECTS=$(addsuffix .o, $(basename $(SOURCES)))
DEPENDENCIES=$(shell find src -name "*.d")

.PHONY: all clean

all: $(TARGET)

clean:
	@ rm -f $(TARGET) $(OBJECTS) $(DEPENDENCIES)

$(TARGET): $(OBJECTS)
	@ echo " LD     $@"
	@ $(LD) $(LDFLAGS) -o $@ $^

include $(DEPENDENCIES)

.c.o:
	@ echo " CC     $<"
	@ $(CC) $(CFLAGS) -c -o $@ $<
	@ $(CC) $(CFLAGS) -M -MT $@ -o $(addsuffix .d,$(basename $@)) $<

.s.o:
	@ echo " AS     $<"
	@ $(AS) $(ASFLAGS) -o $@ $<

