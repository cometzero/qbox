/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class cc3xx : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x10000;

    static constexpr uint32_t RNG_ISR = 0x104;
    static constexpr uint32_t RNG_ICR = 0x108;
    static constexpr uint32_t EHR_DATA = 0x114;
    static constexpr uint32_t RND_SOURCE_ENABLE = 0x12c;
    static constexpr uint32_t SAMPLE_CNT1 = 0x130;
    static constexpr uint32_t RNG_SW_RESET = 0x140;
    static constexpr uint32_t RST_BITS_COUNTER = 0x1bc;
    static constexpr uint32_t RNG_CLK_ENABLE = 0x1c4;
    static constexpr uint32_t AES_HW_FLAGS = 0x4c8;
    static constexpr uint32_t AES_RBG_SEEDING_RDY = 0x4fc;
    static constexpr uint32_t CLK_STATUS = 0x824;
    static constexpr uint32_t CRYPTO_CTL = 0x900;
    static constexpr uint32_t CRYPTO_BUSY = 0x910;
    static constexpr uint32_t HASH_BUSY = 0x91c;
    static constexpr uint32_t HOST_RGF_ICR = 0xa08;
    static constexpr uint32_t HOST_BOOT = 0xa28;
    static constexpr uint32_t HOST_CC_IS_IDLE = 0xa7c;
    static constexpr uint32_t HOST_SF_READY = 0xa90;
    static constexpr uint32_t FIFO_IN_EMPTY = 0xc50;
    static constexpr uint32_t DOUT_FIFO_EMPTY = 0xd50;
    static constexpr uint32_t PIDR0 = 0xfe0;
    static constexpr uint32_t PIDR1 = 0xfe4;
    static constexpr uint32_t PIDR2 = 0xfe8;
    static constexpr uint32_t PIDR3 = 0xfec;
    static constexpr uint32_t CIDR0 = 0xff0;
    static constexpr uint32_t CIDR1 = 0xff4;
    static constexpr uint32_t AIB_FUSE_PROG_COMPLETED = 0x1f04;
    static constexpr uint32_t LCS_IS_VALID = 0x1f0c;
    static constexpr uint32_t NVM_IS_IDLE = 0x1f10;
    static constexpr uint32_t LCS_REG = 0x1f14;

    std::array<uint8_t, REG_BYTES> m_regs{};
    unsigned int m_trace_count = 0;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    uint32_t load32(uint32_t offset) const
    {
        uint32_t value = 0;
        std::memcpy(&value, &m_regs[offset], sizeof(value));
        return value;
    }

    void store32(uint32_t offset, uint32_t value)
    {
        std::memcpy(&m_regs[offset], &value, sizeof(value));
    }

    void reset_registers()
    {
        m_regs.fill(0);

        store32(0x088, 0x00000001); /* pka_status */
        store32(0x0b0, 0x00000001); /* pka_pipe_rdy */
        store32(0x0b4, 0x00000001); /* pka_done */

        store32(RNG_ISR, 0x00000001);
        store32(0x110, 0x00000001); /* trng_valid */
        store32(SAMPLE_CNT1, 0x0000ffff);
        store32(0x1b8, 0x00000000); /* rng_busy */
        store32(0x1c0, 0x03120000); /* rng_version */

        const uint32_t entropy[6] = {
            0x243f6a88, 0x85a308d3, 0x13198a2e,
            0x03707344, 0xa4093822, 0x299f31d0,
        };
        for (unsigned int i = 0; i < 6; ++i) {
            store32(EHR_DATA + i * sizeof(uint32_t), entropy[i]);
        }

        store32(0x3b0, 0x00000000); /* chacha_busy */
        store32(0x3b4, 0x00000001); /* chacha_hw_flags */
        store32(0x470, 0x00000000); /* aes_busy */
        store32(AES_HW_FLAGS, (1u << 3) | (1u << 12));
        store32(AES_RBG_SEEDING_RDY, 0x00000001);
        store32(CLK_STATUS, 0xffffffff);
        store32(CRYPTO_BUSY, 0x00000000);
        store32(HASH_BUSY, 0x00000000);
        store32(HOST_BOOT,
                (1u << 30) | /* AES_EXISTS_LOCAL */
                    (1u << 25) | /* CTR_EXISTS_LOCAL */
                    (1u << 22) | /* AES_CCM_EXISTS_LOCAL */
                    (1u << 21) | /* AES_CMAC_EXISTS_LOCAL */
                    (1u << 17) | /* HASH_EXISTS_LOCAL */
                    (1u << 15) | /* SHA_256_PRSNT_LOCAL */
                    (1u << 11)); /* RNG_EXISTS_LOCAL */
        store32(HOST_CC_IS_IDLE, 0x00000001);
        store32(0xa84, 0x00000000); /* host_remove_ghash_engine */
        store32(0xa88, 0x00000000); /* host_remove_chacha_engine */
        store32(HOST_SF_READY, 0x00000001);
        store32(0xc20, 0x00000000); /* din_mem_dma_busy */
        store32(0xc38, 0x00000000); /* din_sram_dma_busy */
        store32(FIFO_IN_EMPTY, 0x00000001);
        store32(0xd20, 0x00000000); /* dout_mem_dma_busy */
        store32(0xd38, 0x00000000); /* dout_sram_dma_busy */
        store32(DOUT_FIFO_EMPTY, 0x00000001);

        store32(PIDR0, 0x000000c1);
        store32(PIDR1, 0x000000b0);
        store32(PIDR2, 0x0000000b);
        store32(PIDR3, 0x00000000);
        store32(CIDR0, 0x0000000d);
        store32(CIDR1, 0x000000f0);

        store32(0x1e30, 0x00000000); /* ao_cc_sec_debug_reset */
        store32(0x1e3c, 0x00000000); /* ao_cc_gppc */
        store32(AIB_FUSE_PROG_COMPLETED, 0x00000001);
        store32(LCS_IS_VALID, 0x00000001);
        store32(NVM_IS_IDLE, 0x00000001);
        store32(LCS_REG, 0x00000005); /* CC3XX_LCS_SE_CODE */
    }

    void update_rng_after_write(uint32_t offset, uint32_t value)
    {
        if (offset == RNG_SW_RESET) {
            store32(RNG_SW_RESET, value);
            store32(RNG_ISR, 0x00000000);
            return;
        }

        if (offset == RNG_ICR) {
            store32(RNG_ISR, load32(RNG_ISR) & ~value);
            return;
        }

        if (offset == RND_SOURCE_ENABLE) {
            store32(RND_SOURCE_ENABLE, value);
            if (value & 0x1u) {
                store32(RNG_ISR, load32(RNG_ISR) | 0x1u);
            }
            return;
        }

        if (offset == RST_BITS_COUNTER) {
            store32(RST_BITS_COUNTER, value);
            return;
        }

        if (offset == RNG_CLK_ENABLE) {
            store32(RNG_CLK_ENABLE, value);
            return;
        }
    }

    void write32(uint32_t offset, uint32_t value)
    {
        switch (offset) {
        case RNG_ICR:
        case RNG_SW_RESET:
        case RND_SOURCE_ENABLE:
        case RST_BITS_COUNTER:
        case RNG_CLK_ENABLE:
            update_rng_after_write(offset, value);
            break;
        case HOST_RGF_ICR:
            store32(0xa00, load32(0xa00) & ~value);
            break;
        case CRYPTO_CTL:
            store32(CRYPTO_CTL, value);
            store32(CRYPTO_BUSY, 0x00000000);
            store32(HASH_BUSY, 0x00000000);
            store32(HOST_CC_IS_IDLE, 0x00000001);
            break;
        default:
            store32(offset, value);
            break;
        }
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) || offset + len > m_regs.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, &m_regs[offset], len);
            if (!debug && offset >= EHR_DATA && offset < EHR_DATA + 24) {
                store32(RNG_ISR, load32(RNG_ISR) | 0x1u);
            }
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            if (len == sizeof(uint32_t) && (offset % sizeof(uint32_t)) == 0) {
                uint32_t value = 0;
                std::memcpy(&value, data, sizeof(value));
                write32(static_cast<uint32_t>(offset), value);
            } else {
                std::memcpy(&m_regs[offset], data, len);
            }
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trace_access(trans, offset, len, debug);

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset, unsigned int len, bool debug)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value()) {
            return;
        }

        ++m_trace_count;
        uint32_t value = 0;
        if (len <= sizeof(value)) {
            std::memcpy(&value, trans.get_data_ptr(), len);
        }

        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    tlm_utils::simple_target_socket<cc3xx, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit cc3xx(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &cc3xx::b_transport);
        target_socket.register_transport_dbg(this, &cc3xx::transport_dbg);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access(trans, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans, true) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
