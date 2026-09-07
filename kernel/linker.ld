ENTRY(_start)

SECTIONS {
    . = 1M;
    .multiboot : { *(.multiboot) }
    .text : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data : { *(.data) *(.data.*) }
    .bss : {
        bss_start = .;
        *(COMMON) *(.bss) *(.bss.*)
        . = ALIGN(4);
        bss_end = .;
    }
    . = ALIGN(4096);
    end = .;
}
