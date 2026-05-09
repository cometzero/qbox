/*
 * Apollo Hexagon firmware-triggered DMA smoke test.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

.text
.align 4
.globl _start
_start:
    r0 = ##0x00c01000
    r1 = ##0x48455831
    memw(r0 + #0) = r1
    r1 = ##0x444d4131
    memw(r0 + #4) = r1
    r1 = ##0x11223344
    memw(r0 + #8) = r1
    r1 = ##0x55667788
    memw(r0 + #12) = r1
    r1 = ##0xa5a55a5a
    memw(r0 + #16) = r1
    r1 = ##0x5a5aa5a5
    memw(r0 + #20) = r1
    r1 = ##0xfeedc0de
    memw(r0 + #24) = r1
    r1 = ##0x600dbeef
    memw(r0 + #28) = r1

    r0 = ##0x1c220000
    r1 = ##0x10201000
    memw(r0 + #0) = r1
    r1 = ##0x10000000
    memw(r0 + #4) = r1
    r1 = #32
    memw(r0 + #8) = r1
    r1 = #1
    memw(r0 + #12) = r1

poll_dma_done:
    r2 = memw(r0 + #16)
    p0 = cmp.eq(r2, #1)
    if (!p0) jump poll_dma_done

    r1 = ##0x48455844
    memw(r0 + #24) = r1

job_loop:
    r0 = ##0x1c220000
    r1 = memw(r0 + #80)
    p0 = cmp.eq(r1, #1)
    if (!p0) jump job_loop

    r1 = #0
    memw(r0 + #84) = r1
    memw(r0 + #88) = r1

    r4 = memw(r0 + #72)
    p0 = cmp.eq(r4, #64)
    if (!p0) jump job_error
    r4 = memw(r0 + #76)
    p0 = cmp.eq(r4, #16)
    if (!p0) jump job_error

    r10 = memw(r0 + #64)
    r11 = memw(r0 + #68)

    r1 = r10
    memw(r0 + #0) = r1
    r1 = ##0x10202000
    memw(r0 + #4) = r1
    r1 = #64
    memw(r0 + #8) = r1
    r1 = #1
    memw(r0 + #12) = r1

poll_input_dma_done:
    r2 = memw(r0 + #16)
    p0 = cmp.eq(r2, #1)
    if (!p0) jump poll_input_dma_done

    r10 = ##0x00c02000
    r2 = memw(r10 + #0)
    r3 = memw(r10 + #4)
    r2 = add(r2, r3)
    r3 = memw(r10 + #8)
    r2 = add(r2, r3)
    r3 = memw(r10 + #16)
    r2 = add(r2, r3)
    r3 = memw(r10 + #20)
    r2 = add(r2, r3)
    r3 = memw(r10 + #24)
    r2 = add(r2, r3)
    r3 = memw(r10 + #32)
    r2 = add(r2, r3)
    r3 = memw(r10 + #36)
    r2 = add(r2, r3)
    r3 = memw(r10 + #40)
    r2 = add(r2, r3)
    p0 = cmp.eq(r2, #54)
    if (!p0) jump job_error

    r2 = memw(r10 + #4)
    r3 = memw(r10 + #8)
    r2 = add(r2, r3)
    r3 = memw(r10 + #12)
    r2 = add(r2, r3)
    r3 = memw(r10 + #20)
    r2 = add(r2, r3)
    r3 = memw(r10 + #24)
    r2 = add(r2, r3)
    r3 = memw(r10 + #28)
    r2 = add(r2, r3)
    r3 = memw(r10 + #36)
    r2 = add(r2, r3)
    r3 = memw(r10 + #40)
    r2 = add(r2, r3)
    r3 = memw(r10 + #44)
    r2 = add(r2, r3)
    p0 = cmp.eq(r2, #63)
    if (!p0) jump job_error

    r2 = memw(r10 + #16)
    r3 = memw(r10 + #20)
    r2 = add(r2, r3)
    r3 = memw(r10 + #24)
    r2 = add(r2, r3)
    r3 = memw(r10 + #32)
    r2 = add(r2, r3)
    r3 = memw(r10 + #36)
    r2 = add(r2, r3)
    r3 = memw(r10 + #40)
    r2 = add(r2, r3)
    r3 = memw(r10 + #48)
    r2 = add(r2, r3)
    r3 = memw(r10 + #52)
    r2 = add(r2, r3)
    r3 = memw(r10 + #56)
    r2 = add(r2, r3)
    p0 = cmp.eq(r2, #90)
    if (!p0) jump job_error

    r2 = memw(r10 + #20)
    r3 = memw(r10 + #24)
    r2 = add(r2, r3)
    r3 = memw(r10 + #28)
    r2 = add(r2, r3)
    r3 = memw(r10 + #36)
    r2 = add(r2, r3)
    r3 = memw(r10 + #40)
    r2 = add(r2, r3)
    r3 = memw(r10 + #44)
    r2 = add(r2, r3)
    r3 = memw(r10 + #52)
    r2 = add(r2, r3)
    r3 = memw(r10 + #56)
    r2 = add(r2, r3)
    r3 = memw(r10 + #60)
    r2 = add(r2, r3)
    p0 = cmp.eq(r2, #99)
    if (!p0) jump job_error

    r12 = ##0x00c03000
    r1 = ##0x42580000
    memw(r12 + #0) = r1
    r1 = ##0x427c0000
    memw(r12 + #4) = r1
    r1 = ##0x42b40000
    memw(r12 + #8) = r1
    r1 = ##0x42c60000
    memw(r12 + #12) = r1

    r0 = ##0x1c220000
    r1 = ##0x10203000
    memw(r0 + #0) = r1
    r1 = r11
    memw(r0 + #4) = r1
    r1 = #16
    memw(r0 + #8) = r1
    r1 = #1
    memw(r0 + #12) = r1

poll_output_dma_done:
    r2 = memw(r0 + #16)
    p0 = cmp.eq(r2, #1)
    if (!p0) jump poll_output_dma_done

    r1 = ##0x434e4e4f
    memw(r0 + #88) = r1
    r1 = #1
    memw(r0 + #84) = r1
    r1 = #0
    memw(r0 + #80) = r1
    jump job_loop

job_error:
    r0 = ##0x1c220000
    r1 = ##0xbad10001
    memw(r0 + #88) = r1
    r1 = #2
    memw(r0 + #84) = r1
    r1 = #0
    memw(r0 + #80) = r1
    jump job_loop
