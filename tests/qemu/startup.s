/* mps2-an386 极简启动：无需 newlib 的 crt0（TZ 之外的自包含路径） */
    .syntax unified
    .cpu cortex-m4
    .thumb

    .section .isr_vector,"a",%progbits
    .global _start_vector
    .type _start_vector, %object
_start_vector:
    .long __stack_top              @ 初始 SP
    .long Reset_Handler            @ 复位向量
    .rept 20
    .long 0
    .endr
    .size _start_vector, .-_start_vector

    .section .text
    .thumb_func
    .global Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    @ 拷 .data（flash -> sram）
    ldr r0, =__data_load__
    ldr r1, =__data_start__
    ldr r2, =__data_end__
1:  cmp r1, r2
    bhs 2f
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    b 1b
2:
    @ 清 .bss
    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    movs r2, #0
3:  cmp r0, r1
    bhs 4f
    strb r2, [r0], #1
    b 3b
4:
    bl main
    b .             @ 结束时死循环：QEMU 侧由脚本超时收尾
    .size Reset_Handler, .-Reset_Handler
