/* SPDX-License-Identifier: GPL-3.0-only
 * armaria-nam memory map, from Felucca's firmware/app.ld (GPL-3.0).
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120); the
 * BlackBox app slot is the stock app.bin size, 0xA0160 bytes (tools/bbpkg.py keeps sizes).
 * 0x01C00000..0x01C06000 holds .ram_text (the flash driver runs there while the flash is busy).
 * RAM / POOL / stacks / NOINIT as Felucca: 0x01C7FD50.. is reserved (boot_info, update record,
 * vector table). */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0xA0160
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 72K
    POOL   (rw)  : ORIGIN = 0x01C1A000, LENGTH = 0x5A000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
