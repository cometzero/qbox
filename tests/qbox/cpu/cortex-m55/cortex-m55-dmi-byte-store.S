/*
 * Cortex-M55 shared-memory DMI byte-store smoke firmware.
 *
 * The first DMI memory read asks QEMU to install a DMI mapping. Subsequent
 * byte stores and byte reads verify that the M-profile CPU path preserves
 * byte lanes for the same pattern seen in the RSE BL1_2 decrypt failure.
 */

    .syntax unified
    .thumb

    .section .isr_vector, "a", %progbits
    .word _stack_top
    .word Reset_Handler + 1
    .rept 14
    .word Default_Handler + 1
    .endr

    .section .text.Reset_Handler, "ax", %progbits
    .global Reset_Handler
    .type Reset_Handler, %function
    .thumb_func
Reset_Handler:
    ldr r1, =0x90000000
    ldr r2, =0x80000000

    /* Install DMI before the byte stores. */
    ldrb r0, [r1]

    movs r3, #0x67
    strb r3, [r1, #0]
    movs r3, #0xa4
    strb r3, [r1, #1]
    movs r3, #0x79
    strb r3, [r1, #2]
    movs r3, #0x10
    strb r3, [r1, #3]
    movs r3, #0x4f
    strb r3, [r1, #4]
    movs r3, #0x5d
    strb r3, [r1, #5]
    movs r3, #0xf3
    strb r3, [r1, #6]
    movs r3, #0x0a
    strb r3, [r1, #7]
    movs r3, #0xc8
    strb r3, [r1, #8]
    movs r3, #0x9e
    strb r3, [r1, #9]
    movs r3, #0x37
    strb r3, [r1, #10]
    movs r3, #0x42
    strb r3, [r1, #11]
    movs r3, #0x19
    strb r3, [r1, #12]
    movs r3, #0x80
    strb r3, [r1, #13]
    movs r3, #0xae
    strb r3, [r1, #14]
    movs r3, #0x52
    strb r3, [r1, #15]
    movs r3, #0x01
    strb r3, [r1, #16]
    movs r3, #0x23
    strb r3, [r1, #17]
    movs r3, #0x45
    strb r3, [r1, #18]
    movs r3, #0x89
    strb r3, [r1, #19]
    movs r3, #0xab
    strb r3, [r1, #20]
    movs r3, #0xcd
    strb r3, [r1, #21]
    movs r3, #0xef
    strb r3, [r1, #22]
    movs r3, #0x5a
    strb r3, [r1, #23]

    movs r4, #0

    ldrb r5, [r1, #0]
    cmp r5, #0x67
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #1]
    cmp r5, #0xa4
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #2]
    cmp r5, #0x79
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #3]
    cmp r5, #0x10
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #4]
    cmp r5, #0x4f
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #5]
    cmp r5, #0x5d
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #6]
    cmp r5, #0xf3
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #7]
    cmp r5, #0x0a
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #8]
    cmp r5, #0xc8
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #9]
    cmp r5, #0x9e
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #10]
    cmp r5, #0x37
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #11]
    cmp r5, #0x42
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #12]
    cmp r5, #0x19
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #13]
    cmp r5, #0x80
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #14]
    cmp r5, #0xae
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #15]
    cmp r5, #0x52
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #16]
    cmp r5, #0x01
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #17]
    cmp r5, #0x23
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #18]
    cmp r5, #0x45
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #19]
    cmp r5, #0x89
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #20]
    cmp r5, #0xab
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #21]
    cmp r5, #0xcd
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #22]
    cmp r5, #0xef
    bne fail
    adds r4, r4, #1
    ldrb r5, [r1, #23]
    cmp r5, #0x5a
    bne fail

success:
    movs r0, #0
    str r0, [r2]
success_loop:
    wfi
    b success_loop

fail:
    adds r0, r4, #1
    lsls r0, r0, #8
    orrs r0, r0, r5
    str r0, [r2]
fail_loop:
    b fail_loop

    .section .text.Default_Handler, "ax", %progbits
    .global Default_Handler
    .type Default_Handler, %function
    .thumb_func
Default_Handler:
    ldr r2, =0x80000000
    ldr r0, =0xbad00001
    str r0, [r2]
    b .
