    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function

_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    la sp, __stack_top
    li ra, 0
    li fp, 0
    li tp, 0

    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b

2:
    call main

    li a0, 0
    .long 0x4200007b

3:
    wfi
    j 3b

    .size _start, . - _start
