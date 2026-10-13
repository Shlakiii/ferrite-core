ENTRY(_start)

SECTIONS
{
    . = 0x00100000;

    .text : {
        KEEP(*(.multiboot))
        *(.text)
        . = ALIGN(4096);
    }

    .rodata ALIGN(4096) : {
        *(.rodata)
    }

    .data ALIGN(4096) : {
        *(.data)
    }

    .bss ALIGN(4096) : {
        *(.bss)
        *(COMMON)
    }

    .modules ALIGN(4096) : {
        __modules_start = .;
        *(.module)
        . = ALIGN(4096);
        __modules_end = .;
    }

    .binary_stage1 : {
        _binary_stage1_bin_start = .;
        *(.stage1)
        _binary_stage1_bin_end = .;
    }

    .binary_stage2 : {
        _binary_stage2_bin_start = .;
        *(.stage2)
        _binary_stage2_bin_end = .;
    }

    .binary_kernel : {
        _binary_kernel_bin_start = .;
        KEEP(*(.binary_kernel))
        _binary_kernel_bin_end = .;
    }

    _end = .;
    end = .;
}
