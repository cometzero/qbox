/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>

namespace qbox {
namespace mmu720ae {

static constexpr uint32_t ARM_SMMU_IDR0 = 0x000;
static constexpr uint32_t ARM_SMMU_IDR1 = 0x004;
static constexpr uint32_t ARM_SMMU_IDR3 = 0x00c;
static constexpr uint32_t ARM_SMMU_IDR5 = 0x014;
static constexpr uint32_t ARM_SMMU_IIDR = 0x018;
static constexpr uint32_t ARM_SMMU_AIDR = 0x01c;
static constexpr uint32_t ARM_SMMU_CR0 = 0x020;
static constexpr uint32_t ARM_SMMU_CR0ACK = 0x024;
static constexpr uint32_t ARM_SMMU_CR1 = 0x028;
static constexpr uint32_t ARM_SMMU_CR2 = 0x02c;
static constexpr uint32_t ARM_SMMU_GBPA = 0x044;
static constexpr uint32_t ARM_SMMU_IRQ_CTRL = 0x050;
static constexpr uint32_t ARM_SMMU_IRQ_CTRLACK = 0x054;
static constexpr uint32_t ARM_SMMU_GERROR = 0x060;
static constexpr uint32_t ARM_SMMU_GERRORN = 0x064;
static constexpr uint32_t ARM_SMMU_GERROR_IRQ_CFG0 = 0x068;
static constexpr uint32_t ARM_SMMU_GERROR_IRQ_CFG1 = 0x070;
static constexpr uint32_t ARM_SMMU_GERROR_IRQ_CFG2 = 0x074;
static constexpr uint32_t ARM_SMMU_STRTAB_BASE = 0x080;
static constexpr uint32_t ARM_SMMU_STRTAB_BASE_CFG = 0x088;
static constexpr uint32_t ARM_SMMU_CMDQ_BASE = 0x090;
static constexpr uint32_t ARM_SMMU_CMDQ_PROD = 0x098;
static constexpr uint32_t ARM_SMMU_CMDQ_CONS = 0x09c;
static constexpr uint32_t ARM_SMMU_EVTQ_BASE = 0x0a0;
static constexpr uint32_t ARM_SMMU_EVTQ_PROD = 0x0a8;
static constexpr uint32_t ARM_SMMU_EVTQ_CONS = 0x0ac;
static constexpr uint32_t ARM_SMMU_EVTQ_IRQ_CFG0 = 0x0b0;
static constexpr uint32_t ARM_SMMU_EVTQ_IRQ_CFG1 = 0x0b8;
static constexpr uint32_t ARM_SMMU_EVTQ_IRQ_CFG2 = 0x0bc;
static constexpr uint32_t ARM_SMMU_PRIQ_BASE = 0x0c0;
static constexpr uint32_t ARM_SMMU_PRIQ_PROD = 0x0c8;
static constexpr uint32_t ARM_SMMU_PRIQ_CONS = 0x0cc;
static constexpr uint32_t ARM_SMMU_PRIQ_IRQ_CFG0 = 0x0d0;
static constexpr uint32_t ARM_SMMU_PRIQ_IRQ_CFG1 = 0x0d8;
static constexpr uint32_t ARM_SMMU_PRIQ_IRQ_CFG2 = 0x0dc;

static constexpr uint32_t ARM_SMMU_PAGE1_OFFSET = 0x10000;
static constexpr uint32_t ARM_SMMU_REG_SZ = 0xe00;

static constexpr uint64_t Q_BASE_ADDR_MASK = 0x000fffffffffffffe0ull;
static constexpr uint32_t Q_BASE_LOG2SIZE_MASK = 0x1fu;
static constexpr uint32_t EVTQ_ENT_DWORDS = 4;
static constexpr uint32_t EVTQ_ENT_BYTES = EVTQ_ENT_DWORDS * sizeof(uint64_t);

static constexpr uint32_t CR0_ATSCHK = 1u << 4;
static constexpr uint32_t CR0_CMDQEN = 1u << 3;
static constexpr uint32_t CR0_EVTQEN = 1u << 2;
static constexpr uint32_t CR0_PRIQEN = 1u << 1;
static constexpr uint32_t CR0_SMMUEN = 1u << 0;
static constexpr uint32_t CR0_IMPLEMENTED_MASK =
    CR0_CMDQEN | CR0_EVTQEN | CR0_SMMUEN;

static constexpr uint32_t IRQ_CTRL_EVTQ_IRQEN = 1u << 2;
static constexpr uint32_t IRQ_CTRL_PRIQ_IRQEN = 1u << 1;
static constexpr uint32_t IRQ_CTRL_GERROR_IRQEN = 1u << 0;

static constexpr uint32_t GBPA_UPDATE = 1u << 31;

static constexpr uint32_t GERROR_CMDQ_ERR = 1u << 0;
static constexpr uint32_t GERROR_EVTQ_ABT_ERR = 1u << 2;
static constexpr uint32_t GERROR_PRIQ_ABT_ERR = 1u << 3;
static constexpr uint32_t GERROR_ERR_MASK = 0x1fdu;

static constexpr uint32_t EVT_ID_TRANSLATION_FAULT = 0x10;
static constexpr uint64_t EVTQ_0_ID_MASK = 0xffull;
static constexpr uint32_t EVTQ_0_SID_SHIFT = 32;
static constexpr uint32_t EVTQ_1_RNW_SHIFT = 35;

static constexpr uint32_t IDR0_STALL_MODEL_NONE = 1u << 24;
static constexpr uint32_t IDR0_TTENDIAN_LE = 2u << 21;
static constexpr uint32_t IDR0_PRI = 1u << 16;
static constexpr uint32_t IDR0_MSI = 1u << 13;
static constexpr uint32_t IDR0_ATS = 1u << 10;
static constexpr uint32_t IDR0_COHACC = 1u << 4;
static constexpr uint32_t IDR0_TTF_AARCH64 = 2u << 2;
static constexpr uint32_t IDR0_S1P = 1u << 1;

static constexpr uint32_t IDR1_CMDQS_SHIFT = 21;
static constexpr uint32_t IDR1_EVTQS_SHIFT = 16;
static constexpr uint32_t IDR1_PRIQS_SHIFT = 11;
static constexpr uint32_t IDR1_SSIDSIZE_SHIFT = 6;

static constexpr uint32_t IDR5_GRAN4K = 1u << 4;
static constexpr uint32_t IDR5_OAS_48_BIT = 5u;

} // namespace mmu720ae
} // namespace qbox
