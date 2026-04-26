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
    r1 = ##0x00c01000
    memw(r0 + #0) = r1
    r1 = ##0x00a00000
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

halt:
    jump halt
