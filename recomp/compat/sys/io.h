#pragma once
/* Windows/MinGW shim for libx86emu mem.c (host I/O is handled via x86emu_set_memio_handler) */
#define inb(addr) 0xff
#define inw(addr) 0xffff
#define inl(addr) 0xffffffff
#define outb(val, addr) ((void)0)
#define outw(val, addr) ((void)0)
#define outl(val, addr) ((void)0)
