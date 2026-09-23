/* mps2-an386 (Cortex-M4): flash 0x0 (512K), sram 0x20000000 (512K) */
ENTRY(Reset_Handler)

MEMORY {
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 512K
  SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

__stack_top = 0x20080000;

SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) *(.rodata.*) *(.glue_7*) *(.glue_7t*) } > FLASH
  .data : ALIGN(4) {
    __data_start__ = .;
    *(.data*) *(.sdata*)
    __data_end__ = .;
  } > SRAM AT > FLASH
  __data_load__ = LOADADDR(.data);
  .bss : ALIGN(4) {
    __bss_start__ = .;
    *(.bss*) *(COMMON)
    __bss_end__ = .;
  } > SRAM
}