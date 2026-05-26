/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <ports/target-signal-socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <boost/multiprecision/cpp_int.hpp>

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
    static constexpr uint32_t PKA_OPCODE = 0x080;
    static constexpr uint32_t PKA_STATUS = 0x088;
    static constexpr uint32_t PKA_SW_RESET = 0x08c;
    static constexpr uint32_t PKA_L_BASE = 0x090;
    static constexpr uint32_t PKA_PIPE_RDY = 0x0b0;
    static constexpr uint32_t PKA_DONE = 0x0b4;
    static constexpr uint32_t PKA_SRAM_ADDR = 0x0d4;
    static constexpr uint32_t PKA_SRAM_WDATA = 0x0d8;
    static constexpr uint32_t PKA_SRAM_RDATA = 0x0dc;
    static constexpr uint32_t PKA_SRAM_RADDR = 0x0e4;
    static constexpr uint32_t AES_KEY_0 = 0x400;
    static constexpr uint32_t AES_IV_0 = 0x440;
    static constexpr uint32_t AES_CTR_0 = 0x460;
    static constexpr uint32_t AES_BUSY = 0x470;
    static constexpr uint32_t AES_CMAC_INIT = 0x47c;
    static constexpr uint32_t AES_REMAINING_BYTES = 0x4bc;
    static constexpr uint32_t AES_CONTROL = 0x4c0;
    static constexpr uint32_t AES_HW_FLAGS = 0x4c8;
    static constexpr uint32_t AES_RBG_SEEDING_RDY = 0x4fc;
    static constexpr uint32_t HASH_H = 0x640;
    static constexpr uint32_t AUTO_HW_PADDING = 0x684;
    static constexpr uint32_t HASH_CONTROL = 0x7c0;
    static constexpr uint32_t HASH_PAD_CFG = 0x7c8;
    static constexpr uint32_t HASH_CUR_LEN0 = 0x7cc;
    static constexpr uint32_t HASH_CUR_LEN1 = 0x7d0;
    static constexpr uint32_t CLK_STATUS = 0x824;
    static constexpr uint32_t CRYPTO_CTL = 0x900;
    static constexpr uint32_t CRYPTO_BUSY = 0x910;
    static constexpr uint32_t HASH_BUSY = 0x91c;
    static constexpr uint32_t HOST_RGF_IRR = 0xa00;
    static constexpr uint32_t HOST_RGF_IMR = 0xa04;
    static constexpr uint32_t HOST_RGF_ICR = 0xa08;
    static constexpr uint32_t HOST_CRYPTOKEY_SEL = 0xa38;
    static constexpr uint32_t HOST_BOOT = 0xa28;
    static constexpr uint32_t HOST_CC_IS_IDLE = 0xa7c;
    static constexpr uint32_t HOST_SF_READY = 0xa90;
    static constexpr uint32_t DIN_MEM_DMA_BUSY = 0xc20;
    static constexpr uint32_t DIN_SRC_LLI_WORD0 = 0xc28;
    static constexpr uint32_t DIN_SRC_LLI_WORD1 = 0xc2c;
    static constexpr uint32_t DIN_SRAM_SRC_ADDR = 0xc30;
    static constexpr uint32_t DIN_SRAM_BYTES_LEN = 0xc34;
    static constexpr uint32_t DIN_SRAM_DMA_BUSY = 0xc38;
    static constexpr uint32_t FIFO_IN_EMPTY = 0xc50;
    static constexpr uint32_t DOUT_MEM_DMA_BUSY = 0xd20;
    static constexpr uint32_t DOUT_DST_LLI_WORD0 = 0xd28;
    static constexpr uint32_t DOUT_DST_LLI_WORD1 = 0xd2c;
    static constexpr uint32_t DOUT_SRAM_BYTES_LEN = 0xd34;
    static constexpr uint32_t DOUT_SRAM_DMA_BUSY = 0xd38;
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

    static constexpr uint32_t SRAM_TO_DIN_INT = 1u << 4;
    static constexpr uint32_t DOUT_TO_SRAM_INT = 1u << 5;
    static constexpr uint32_t MEM_TO_DIN_INT = 1u << 6;
    static constexpr uint32_t DOUT_TO_MEM_INT = 1u << 7;
    static constexpr uint32_t SYM_DMA_COMPLETED = 1u << 11;

    static constexpr uint32_t CC3XX_ENGINE_NONE = 0x00;
    static constexpr uint32_t CC3XX_ENGINE_AES = 0x01;
    static constexpr uint32_t CC3XX_ENGINE_AES_AND_HASH = 0x03;
    static constexpr uint32_t CC3XX_ENGINE_HASH = 0x07;
    static constexpr uint32_t CC3XX_ENGINE_AES_TO_HASH_AND_DOUT = 0x0a;
    static constexpr uint32_t CC3XX_HASH_ALG_SHA256 = 0x02;
    static constexpr uint32_t CC3XX_AES_MODE_ECB = 0x00;
    static constexpr uint32_t CC3XX_AES_MODE_CTR = 0x02;
    static constexpr uint32_t CC3XX_AES_MODE_CMAC = 0x07;
    static constexpr uint32_t CC3XX_AES_KEYSIZE_128 = 0x00;
    static constexpr uint32_t CC3XX_AES_KEYSIZE_192 = 0x01;
    static constexpr uint32_t CC3XX_AES_KEYSIZE_256 = 0x02;
    static constexpr uint32_t CC3XX_PKA_OPCODE_ADD_INC = 0x04;
    static constexpr uint32_t CC3XX_PKA_OPCODE_SUB_DEC_NEG = 0x05;
    static constexpr uint32_t CC3XX_PKA_OPCODE_MODADD_MODINC = 0x06;
    static constexpr uint32_t CC3XX_PKA_OPCODE_MODSUB_MODDEC_MODNEG = 0x07;
    static constexpr uint32_t CC3XX_PKA_OPCODE_AND_TST0_CLR0 = 0x08;
    static constexpr uint32_t CC3XX_PKA_OPCODE_OR_COPY_SET0 = 0x09;
    static constexpr uint32_t CC3XX_PKA_OPCODE_XOR_FLIP0_INVERT_COMPARE = 0x0a;
    static constexpr uint32_t CC3XX_PKA_OPCODE_SHR0 = 0x0c;
    static constexpr uint32_t CC3XX_PKA_OPCODE_SHR1 = 0x0d;
    static constexpr uint32_t CC3XX_PKA_OPCODE_SHL0 = 0x0e;
    static constexpr uint32_t CC3XX_PKA_OPCODE_SHL1 = 0x0f;
    static constexpr uint32_t CC3XX_PKA_OPCODE_MULLOW = 0x10;
    static constexpr uint32_t CC3XX_PKA_OPCODE_MODMUL = 0x11;
    static constexpr uint32_t CC3XX_PKA_OPCODE_MODEXP = 0x13;
    static constexpr uint32_t CC3XX_PKA_OPCODE_DIV = 0x14;
    static constexpr uint32_t CC3XX_PKA_OPCODE_MODINV = 0x15;
    static constexpr uint32_t CC3XX_PKA_OPCODE_MULHIGH = 0x17;
    static constexpr uint32_t CC3XX_PKA_OPCODE_REDUCTION = 0x1b;
    static constexpr uint32_t PKA_STATUS_ALU_SIGN_OUT = 1u << 8;
    static constexpr uint32_t PKA_STATUS_ALU_CARRY = 1u << 9;
    static constexpr uint32_t PKA_STATUS_ALU_OUT_ZERO = 1u << 12;
    static constexpr uint32_t PKA_STATUS_DIV_BY_ZERO = 1u << 14;
    static constexpr size_t PKA_SRAM_WORDS = 0x1800 / sizeof(uint32_t);

    using pka_int = boost::multiprecision::cpp_int;

    std::array<uint8_t, REG_BYTES> m_regs{};
    std::array<uint32_t, PKA_SRAM_WORDS> m_pka_sram{};
    uint32_t m_pka_write_addr = 0;
    uint32_t m_pka_read_addr = 0;
    mutable bool m_pka_modulus_cache_valid = false;
    mutable size_t m_pka_modulus_cache_words = 0;
    mutable pka_int m_pka_modulus_cache = 0;
    unsigned int m_trace_seen_count = 0;
    unsigned int m_trace_count = 0;

    using initiator_socket_type = tlm_utils::simple_initiator_socket_b<
        cc3xx, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types, sc_core::SC_ZERO_OR_MORE_BOUND>;

    struct sha256_state {
        std::array<uint32_t, 8> h{};
        std::array<uint8_t, 64> block{};
        uint64_t bytes = 0;
        size_t block_len = 0;
        bool active = false;
        bool finalized = false;
    };

    sha256_state m_sha256;
    uint32_t m_engine = CC3XX_ENGINE_NONE;
    std::vector<uint8_t> m_cmac_data;
    bool m_cmac_active = false;

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

    static int32_t sign_extend_5(uint32_t value)
    {
        value &= 0x1fu;
        return (value & 0x10u) != 0 ? static_cast<int32_t>(value | ~0x1fu)
                                    : static_cast<int32_t>(value);
    }

    size_t pka_len_words(uint32_t size_id) const
    {
        uint32_t bits = 0;

        if (size_id < 8) {
            bits = load32(PKA_L_BASE + size_id * sizeof(uint32_t));
        }

        if (bits == 0) {
            return 1;
        }

        return std::min<size_t>((bits + 31u) / 32u, m_pka_sram.size());
    }

    uint32_t pka_sram_read(uint32_t word_addr) const
    {
        if (word_addr >= m_pka_sram.size()) {
            return 0;
        }

        return m_pka_sram[word_addr];
    }

    void pka_invalidate_modulus_cache()
    {
        m_pka_modulus_cache_valid = false;
        m_pka_modulus_cache_words = 0;
        m_pka_modulus_cache = 0;
    }

    void pka_sram_write(uint32_t word_addr, uint32_t value, bool invalidate_modulus = true)
    {
        if (word_addr < m_pka_sram.size()) {
            m_pka_sram[word_addr] = value;
            if (invalidate_modulus) {
                pka_invalidate_modulus_cache();
            }
        }
    }

    uint32_t pka_phys_reg_base(uint32_t phys_reg) const
    {
        if (phys_reg >= 32) {
            return 0;
        }

        return load32(phys_reg * sizeof(uint32_t));
    }

    std::vector<uint32_t> pka_load_operand(bool is_immediate, uint32_t value,
                                           size_t words, bool signed_immediate) const
    {
        std::vector<uint32_t> data(words, 0);

        if (data.empty()) {
            return data;
        }

        if (is_immediate) {
            if (signed_immediate) {
                const auto imm = sign_extend_5(value);
                std::fill(data.begin(), data.end(), imm < 0 ? 0xffffffffu : 0u);
                data[0] = static_cast<uint32_t>(imm);
            } else {
                data[0] = value & 0x1fu;
            }
            return data;
        }

        const uint32_t base = pka_phys_reg_base(value);
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = pka_sram_read(base + static_cast<uint32_t>(i));
        }
        return data;
    }

    void pka_store_operand(uint32_t phys_reg, const std::vector<uint32_t>& data)
    {
        const uint32_t base = pka_phys_reg_base(phys_reg);
        const uint32_t n_base = pka_phys_reg_base(0);
        const bool writes_modulus =
            base < n_base + data.size() &&
            n_base < base + data.size();

        for (size_t i = 0; i < data.size(); ++i) {
            pka_sram_write(base + static_cast<uint32_t>(i), data[i], writes_modulus);
        }
    }

    static bool pka_is_zero(const std::vector<uint32_t>& data)
    {
        return std::all_of(data.begin(), data.end(),
                           [](uint32_t value) { return value == 0; });
    }

    static pka_int pka_word_modulus(size_t words)
    {
        return pka_int(1) << static_cast<unsigned int>(words * 32);
    }

    static pka_int pka_word_mask(size_t words)
    {
        return pka_word_modulus(words) - 1;
    }

    static pka_int pka_words_to_int(const std::vector<uint32_t>& data)
    {
        pka_int value = 0;

        for (auto it = data.rbegin(); it != data.rend(); ++it) {
            value <<= 32;
            value += *it;
        }

        return value;
    }

    static std::vector<uint32_t> pka_int_to_words(pka_int value, size_t words)
    {
        std::vector<uint32_t> data(words, 0);
        if (words == 0) {
            return data;
        }

        const pka_int modulus = pka_word_modulus(words);
        value %= modulus;
        if (value < 0) {
            value += modulus;
        }

        for (size_t i = 0; i < words; ++i) {
            const pka_int word = value & 0xffffffffu;
            data[i] = word.convert_to<uint32_t>();
            value >>= 32;
        }

        return data;
    }

    pka_int pka_modulus_value(size_t words) const
    {
        if (m_pka_modulus_cache_valid && m_pka_modulus_cache_words == words) {
            return m_pka_modulus_cache;
        }

        m_pka_modulus_cache = pka_words_to_int(pka_load_operand(false, 0, words, false));
        m_pka_modulus_cache_words = words;
        m_pka_modulus_cache_valid = true;
        return m_pka_modulus_cache;
    }

    static pka_int pka_positive_mod(pka_int value, const pka_int& modulus)
    {
        if (modulus <= 0) {
            return 0;
        }

        value %= modulus;
        if (value < 0) {
            value += modulus;
        }
        return value;
    }

    static pka_int pka_pow_mod(pka_int base, pka_int exponent, const pka_int& modulus)
    {
        if (modulus <= 0) {
            return 0;
        }

        pka_int result = 1 % modulus;
        base = pka_positive_mod(base, modulus);

        while (exponent > 0) {
            if ((exponent & 1) != 0) {
                result = (result * base) % modulus;
            }
            exponent >>= 1;
            base = (base * base) % modulus;
        }

        return result;
    }

    static pka_int pka_mod_inverse(pka_int value, const pka_int& modulus, bool& ok)
    {
        ok = false;
        if (modulus <= 1) {
            return 0;
        }

        pka_int t = 0;
        pka_int new_t = 1;
        pka_int r = modulus;
        pka_int new_r = pka_positive_mod(value, modulus);

        while (new_r != 0) {
            const pka_int quotient = r / new_r;
            const pka_int next_t = t - quotient * new_t;
            const pka_int next_r = r - quotient * new_r;
            t = new_t;
            new_t = next_t;
            r = new_r;
            new_r = next_r;
        }

        if (r != 1) {
            return 0;
        }
        if (t < 0) {
            t += modulus;
        }

        ok = true;
        return t;
    }

    static std::vector<uint32_t> pka_shift_words(const std::vector<uint32_t>& value,
                                                 uint32_t amount, bool left,
                                                 bool fill_one)
    {
        const size_t words = value.size();
        if (words == 0 || amount == 0) {
            return value;
        }

        const auto width = static_cast<unsigned int>(words * 32);
        if (amount >= width) {
            return fill_one ? pka_int_to_words(pka_word_mask(words), words)
                            : std::vector<uint32_t>(words, 0);
        }

        pka_int result = pka_words_to_int(value);
        if (left) {
            result <<= amount;
            if (fill_one) {
                result |= (pka_int(1) << amount) - 1;
            }
        } else {
            result >>= amount;
            if (fill_one) {
                result |= ((pka_int(1) << amount) - 1) << (width - amount);
            }
        }

        return pka_int_to_words(result, words);
    }

    static std::vector<uint32_t> pka_add_words(const std::vector<uint32_t>& lhs,
                                               const std::vector<uint32_t>& rhs,
                                               bool& carry)
    {
        std::vector<uint32_t> result(lhs.size(), 0);
        uint64_t c = 0;

        for (size_t i = 0; i < result.size(); ++i) {
            const uint64_t sum = static_cast<uint64_t>(lhs[i]) + rhs[i] + c;
            result[i] = static_cast<uint32_t>(sum);
            c = sum >> 32;
        }

        carry = c != 0;
        return result;
    }

    static std::vector<uint32_t> pka_sub_words(const std::vector<uint32_t>& lhs,
                                               const std::vector<uint32_t>& rhs,
                                               bool& borrow)
    {
        std::vector<uint32_t> result(lhs.size(), 0);
        uint64_t b = 0;

        for (size_t i = 0; i < result.size(); ++i) {
            const uint64_t subtrahend = static_cast<uint64_t>(rhs[i]) + b;
            borrow = static_cast<uint64_t>(lhs[i]) < subtrahend;
            result[i] = static_cast<uint32_t>(static_cast<uint64_t>(lhs[i]) - subtrahend);
            b = borrow ? 1 : 0;
        }

        borrow = b != 0;
        return result;
    }

    void pka_update_status(const std::vector<uint32_t>& result, bool carry,
                           bool sign = false, bool div_by_zero = false)
    {
        uint32_t status = 0x1u;

        if (sign) {
            status |= PKA_STATUS_ALU_SIGN_OUT;
        }
        if (carry) {
            status |= PKA_STATUS_ALU_CARRY;
        }
        if (pka_is_zero(result)) {
            status |= PKA_STATUS_ALU_OUT_ZERO;
        }
        if (div_by_zero) {
            status |= PKA_STATUS_DIV_BY_ZERO;
        }

        store32(PKA_STATUS, status);
        store32(PKA_PIPE_RDY, 0x1u);
        store32(PKA_DONE, 0x1u);
    }

    void execute_pka_opcode(uint32_t opcode)
    {
        const uint32_t op = (opcode >> 27) & 0x1fu;
        if (op == 0) {
            return;
        }

        const uint32_t size_id = (opcode >> 24) & 0x7u;
        const bool lhs_immediate = ((opcode >> 23) & 0x1u) != 0;
        const uint32_t lhs_id = (opcode >> 18) & 0x1fu;
        const bool rhs_immediate = ((opcode >> 17) & 0x1u) != 0;
        const uint32_t rhs_id = (opcode >> 12) & 0x1fu;
        const bool discard_result = ((opcode >> 11) & 0x1u) != 0;
        const uint32_t result_id = (opcode >> 6) & 0x1fu;
        const bool shift_op =
            op >= CC3XX_PKA_OPCODE_SHR0 &&
            op <= CC3XX_PKA_OPCODE_SHL1;
        const bool signed_immediate =
            op == CC3XX_PKA_OPCODE_ADD_INC ||
            op == CC3XX_PKA_OPCODE_SUB_DEC_NEG ||
            op == CC3XX_PKA_OPCODE_MODADD_MODINC ||
            op == CC3XX_PKA_OPCODE_MODSUB_MODDEC_MODNEG;
        const size_t words = pka_len_words(size_id);
        const auto lhs = pka_load_operand(lhs_immediate, lhs_id, words, signed_immediate);
        const auto rhs = shift_op ?
            std::vector<uint32_t>(words, 0) :
            pka_load_operand(rhs_immediate, rhs_id, words, signed_immediate);
        std::vector<uint32_t> result(words, 0);
        bool carry = false;
        bool sign = false;
        bool div_by_zero = false;

        switch (op) {
        case CC3XX_PKA_OPCODE_ADD_INC:
            result = pka_add_words(lhs, rhs, carry);
            break;
        case CC3XX_PKA_OPCODE_MODADD_MODINC: {
            const pka_int modulus = pka_modulus_value(words);
            if (modulus > 0) {
                const pka_int sum = pka_words_to_int(lhs) + pka_words_to_int(rhs);
                carry = sum >= modulus;
                result = pka_int_to_words(pka_positive_mod(sum, modulus), words);
            } else {
                result = pka_add_words(lhs, rhs, carry);
            }
            break;
        }
        case CC3XX_PKA_OPCODE_SUB_DEC_NEG:
            result = pka_sub_words(lhs, rhs, carry);
            sign = carry;
            break;
        case CC3XX_PKA_OPCODE_MODSUB_MODDEC_MODNEG: {
            const pka_int lhs_value = pka_words_to_int(lhs);
            const pka_int rhs_value = pka_words_to_int(rhs);
            const pka_int modulus = pka_modulus_value(words);
            sign = lhs_value < rhs_value;
            carry = sign;
            if (modulus > 0) {
                result = pka_int_to_words(pka_positive_mod(lhs_value - rhs_value, modulus), words);
            } else {
                result = pka_sub_words(lhs, rhs, carry);
                sign = carry;
            }
            break;
        }
        case CC3XX_PKA_OPCODE_AND_TST0_CLR0:
            for (size_t i = 0; i < words; ++i) {
                result[i] = lhs[i] & rhs[i];
            }
            break;
        case CC3XX_PKA_OPCODE_OR_COPY_SET0:
            for (size_t i = 0; i < words; ++i) {
                result[i] = lhs[i] | rhs[i];
            }
            break;
        case CC3XX_PKA_OPCODE_XOR_FLIP0_INVERT_COMPARE:
            for (size_t i = 0; i < words; ++i) {
                result[i] = lhs[i] ^ rhs[i];
            }
            break;
        case CC3XX_PKA_OPCODE_SHR0:
        case CC3XX_PKA_OPCODE_SHR1:
        case CC3XX_PKA_OPCODE_SHL0:
        case CC3XX_PKA_OPCODE_SHL1:
            result = pka_shift_words(lhs, rhs_id + 1u,
                                     op == CC3XX_PKA_OPCODE_SHL0 ||
                                         op == CC3XX_PKA_OPCODE_SHL1,
                                     op == CC3XX_PKA_OPCODE_SHR1 ||
                                         op == CC3XX_PKA_OPCODE_SHL1);
            break;
        case CC3XX_PKA_OPCODE_MULLOW: {
            const pka_int product = pka_words_to_int(lhs) * pka_words_to_int(rhs);
            carry = (product >> static_cast<unsigned int>(words * 32)) != 0;
            result = pka_int_to_words(product, words);
            break;
        }
        case CC3XX_PKA_OPCODE_MULHIGH: {
            const pka_int product = pka_words_to_int(lhs) * pka_words_to_int(rhs);
            result = pka_int_to_words(product >> static_cast<unsigned int>(words * 32), words);
            break;
        }
        case CC3XX_PKA_OPCODE_DIV: {
            const pka_int divisor = pka_words_to_int(rhs);
            if (divisor == 0) {
                div_by_zero = true;
                if (!lhs_immediate) {
                    pka_store_operand(lhs_id, lhs);
                }
                break;
            }

            const pka_int dividend = pka_words_to_int(lhs);
            const pka_int quotient = dividend / divisor;
            const pka_int remainder = dividend % divisor;
            result = pka_int_to_words(quotient, words);
            if (!lhs_immediate) {
                pka_store_operand(lhs_id, pka_int_to_words(remainder, words));
            }
            break;
        }
        case CC3XX_PKA_OPCODE_MODMUL: {
            const pka_int modulus = pka_modulus_value(words);
            result = pka_int_to_words(pka_positive_mod(pka_words_to_int(lhs) *
                                                       pka_words_to_int(rhs),
                                                       modulus),
                                      words);
            div_by_zero = modulus <= 0;
            break;
        }
        case CC3XX_PKA_OPCODE_MODEXP: {
            const pka_int modulus = pka_modulus_value(words);
            result = pka_int_to_words(pka_pow_mod(pka_words_to_int(lhs),
                                                  pka_words_to_int(rhs),
                                                  modulus),
                                      words);
            div_by_zero = modulus <= 0;
            break;
        }
        case CC3XX_PKA_OPCODE_MODINV: {
            const pka_int modulus = pka_modulus_value(words);
            bool invertible = false;
            result = pka_int_to_words(pka_mod_inverse(pka_words_to_int(rhs),
                                                      modulus, invertible),
                                      words);
            div_by_zero = !invertible;
            break;
        }
        case CC3XX_PKA_OPCODE_REDUCTION: {
            const pka_int modulus = pka_modulus_value(words);
            result = pka_int_to_words(pka_positive_mod(pka_words_to_int(lhs),
                                                       modulus),
                                      words);
            div_by_zero = modulus <= 0;
            break;
        }
        default:
            pka_update_status(result, false);
            return;
        }

        if (!discard_result) {
            pka_store_operand(result_id, result);
        }
        pka_update_status(result, carry, sign, div_by_zero);
    }

    uint64_t hash_current_len() const
    {
        return load32(HASH_CUR_LEN0) |
               (static_cast<uint64_t>(load32(HASH_CUR_LEN1)) << 32);
    }

    void store_hash_current_len(uint64_t value)
    {
        store32(HASH_CUR_LEN0, static_cast<uint32_t>(value));
        store32(HASH_CUR_LEN1, static_cast<uint32_t>(value >> 32));
    }

    void store_sha256_h()
    {
        for (unsigned int i = 0; i < m_sha256.h.size(); ++i) {
            store32(HASH_H + i * sizeof(uint32_t), m_sha256.h[i]);
        }
    }

    static uint32_t rotr32(uint32_t value, unsigned int bits)
    {
        return (value >> bits) | (value << (32u - bits));
    }

    static uint32_t load_be32(const uint8_t* data)
    {
        return (static_cast<uint32_t>(data[0]) << 24) |
               (static_cast<uint32_t>(data[1]) << 16) |
               (static_cast<uint32_t>(data[2]) << 8) |
               static_cast<uint32_t>(data[3]);
    }

    static void store_be64(uint8_t* data, uint64_t value)
    {
        for (int i = 7; i >= 0; --i) {
            data[7 - i] = static_cast<uint8_t>(value >> (i * 8));
        }
    }

    static uint8_t aes_xtime(uint8_t value)
    {
        return static_cast<uint8_t>((value << 1) ^ ((value & 0x80u) != 0 ? 0x1bu : 0x00u));
    }

    static uint8_t aes_sbox(uint8_t value)
    {
        static const uint8_t sbox[256] = {
            0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
            0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
            0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
            0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
            0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
            0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
            0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
            0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
            0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
            0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
            0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
            0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
            0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
            0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
            0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
            0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
            0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
            0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
            0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
            0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
            0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
            0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
            0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
            0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
            0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
            0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
            0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
            0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
            0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
            0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
            0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
            0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
        };

        return sbox[value];
    }

    static uint8_t aes_inv_sbox(uint8_t value)
    {
        static const uint8_t inv_sbox[256] = {
            0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
            0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
            0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
            0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
            0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
            0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
            0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
            0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
            0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
            0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
            0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
            0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
            0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
            0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
            0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
            0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
            0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
            0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
            0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
            0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
            0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
            0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
            0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
            0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
            0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
            0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
            0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
            0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
            0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
            0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
            0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
            0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
        };

        return inv_sbox[value];
    }

    static uint8_t aes_mul(uint8_t value, uint8_t factor)
    {
        uint8_t result = 0;

        while (factor != 0) {
            if ((factor & 0x1u) != 0) {
                result ^= value;
            }
            value = aes_xtime(value);
            factor >>= 1;
        }

        return result;
    }

    static void aes_add_round_key(uint8_t* state, const uint8_t* round_key)
    {
        for (unsigned int i = 0; i < 16; ++i) {
            state[i] ^= round_key[i];
        }
    }

    static void aes_sub_bytes(uint8_t* state)
    {
        for (unsigned int i = 0; i < 16; ++i) {
            state[i] = aes_sbox(state[i]);
        }
    }

    static void aes_shift_rows(uint8_t* state)
    {
        uint8_t value = state[1];
        state[1] = state[5];
        state[5] = state[9];
        state[9] = state[13];
        state[13] = value;

        value = state[2];
        state[2] = state[10];
        state[10] = value;
        value = state[6];
        state[6] = state[14];
        state[14] = value;

        value = state[15];
        state[15] = state[11];
        state[11] = state[7];
        state[7] = state[3];
        state[3] = value;
    }

    static void aes_inv_sub_bytes(uint8_t* state)
    {
        for (unsigned int i = 0; i < 16; ++i) {
            state[i] = aes_inv_sbox(state[i]);
        }
    }

    static void aes_inv_shift_rows(uint8_t* state)
    {
        uint8_t value = state[13];
        state[13] = state[9];
        state[9] = state[5];
        state[5] = state[1];
        state[1] = value;

        value = state[2];
        state[2] = state[10];
        state[10] = value;
        value = state[6];
        state[6] = state[14];
        state[14] = value;

        value = state[3];
        state[3] = state[7];
        state[7] = state[11];
        state[11] = state[15];
        state[15] = value;
    }

    static void aes_mix_columns(uint8_t* state)
    {
        for (unsigned int i = 0; i < 4; ++i) {
            uint8_t* column = state + i * 4;
            const uint8_t t = column[0] ^ column[1] ^ column[2] ^ column[3];
            const uint8_t u = column[0];

            column[0] ^= t ^ aes_xtime(column[0] ^ column[1]);
            column[1] ^= t ^ aes_xtime(column[1] ^ column[2]);
            column[2] ^= t ^ aes_xtime(column[2] ^ column[3]);
            column[3] ^= t ^ aes_xtime(column[3] ^ u);
        }
    }

    static void aes_inv_mix_columns(uint8_t* state)
    {
        for (unsigned int i = 0; i < 4; ++i) {
            uint8_t* column = state + i * 4;
            const uint8_t a0 = column[0];
            const uint8_t a1 = column[1];
            const uint8_t a2 = column[2];
            const uint8_t a3 = column[3];

            column[0] = aes_mul(a0, 0x0e) ^ aes_mul(a1, 0x0b) ^
                        aes_mul(a2, 0x0d) ^ aes_mul(a3, 0x09);
            column[1] = aes_mul(a0, 0x09) ^ aes_mul(a1, 0x0e) ^
                        aes_mul(a2, 0x0b) ^ aes_mul(a3, 0x0d);
            column[2] = aes_mul(a0, 0x0d) ^ aes_mul(a1, 0x09) ^
                        aes_mul(a2, 0x0e) ^ aes_mul(a3, 0x0b);
            column[3] = aes_mul(a0, 0x0b) ^ aes_mul(a1, 0x0d) ^
                        aes_mul(a2, 0x09) ^ aes_mul(a3, 0x0e);
        }
    }

    static bool aes_expand_key(const uint8_t* key, size_t key_len,
                               std::array<uint8_t, 240>& round_keys,
                               unsigned int& rounds)
    {
        static const uint8_t rcon[11] = {
            0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
            0x20, 0x40, 0x80, 0x1b, 0x36,
        };

        if (key_len != 16 && key_len != 24 && key_len != 32) {
            return false;
        }

        const unsigned int nk = static_cast<unsigned int>(key_len / 4);
        rounds = nk + 6;
        const unsigned int words = 4 * (rounds + 1);

        std::copy(key, key + key_len, round_keys.begin());

        for (unsigned int i = nk; i < words; ++i) {
            uint8_t temp[4] = {
                round_keys[(i - 1) * 4 + 0],
                round_keys[(i - 1) * 4 + 1],
                round_keys[(i - 1) * 4 + 2],
                round_keys[(i - 1) * 4 + 3],
            };

            if ((i % nk) == 0) {
                const uint8_t rotate = temp[0];
                temp[0] = aes_sbox(temp[1]) ^ rcon[i / nk];
                temp[1] = aes_sbox(temp[2]);
                temp[2] = aes_sbox(temp[3]);
                temp[3] = aes_sbox(rotate);
            } else if (nk > 6 && (i % nk) == 4) {
                for (auto& byte : temp) {
                    byte = aes_sbox(byte);
                }
            }

            for (unsigned int j = 0; j < 4; ++j) {
                round_keys[i * 4 + j] = round_keys[(i - nk) * 4 + j] ^ temp[j];
            }
        }

        return true;
    }

    static bool aes_encrypt_block(const uint8_t* key, size_t key_len,
                                  const uint8_t* input, uint8_t* output)
    {
        std::array<uint8_t, 240> round_keys{};
        std::array<uint8_t, 16> state{};
        unsigned int rounds = 0;

        if (!aes_expand_key(key, key_len, round_keys, rounds)) {
            return false;
        }

        std::copy(input, input + state.size(), state.begin());
        aes_add_round_key(state.data(), round_keys.data());

        for (unsigned int round = 1; round < rounds; ++round) {
            aes_sub_bytes(state.data());
            aes_shift_rows(state.data());
            aes_mix_columns(state.data());
            aes_add_round_key(state.data(), round_keys.data() + round * 16);
        }

        aes_sub_bytes(state.data());
        aes_shift_rows(state.data());
        aes_add_round_key(state.data(), round_keys.data() + rounds * 16);
        std::copy(state.begin(), state.end(), output);
        return true;
    }

    static bool aes_decrypt_block(const uint8_t* key, size_t key_len,
                                  const uint8_t* input, uint8_t* output)
    {
        std::array<uint8_t, 240> round_keys{};
        std::array<uint8_t, 16> state{};
        unsigned int rounds = 0;

        if (!aes_expand_key(key, key_len, round_keys, rounds)) {
            return false;
        }

        std::copy(input, input + state.size(), state.begin());
        aes_add_round_key(state.data(), round_keys.data() + rounds * 16);

        for (unsigned int round = rounds - 1; round > 0; --round) {
            aes_inv_shift_rows(state.data());
            aes_inv_sub_bytes(state.data());
            aes_add_round_key(state.data(), round_keys.data() + round * 16);
            aes_inv_mix_columns(state.data());
        }

        aes_inv_shift_rows(state.data());
        aes_inv_sub_bytes(state.data());
        aes_add_round_key(state.data(), round_keys.data());
        std::copy(state.begin(), state.end(), output);
        return true;
    }

    static void aes_increment_counter(std::array<uint8_t, 16>& counter)
    {
        for (auto it = counter.rbegin(); it != counter.rend(); ++it) {
            if (++(*it) != 0) {
                break;
            }
        }
    }

    static void aes_left_shift_block(const uint8_t* input, uint8_t* output)
    {
        uint8_t carry = 0;

        for (int i = 15; i >= 0; --i) {
            const uint8_t next_carry = (input[i] & 0x80u) != 0 ? 1u : 0u;
            output[i] = static_cast<uint8_t>((input[i] << 1) | carry);
            carry = next_carry;
        }
    }

    static void aes_xor_block(uint8_t* lhs, const uint8_t* rhs)
    {
        for (unsigned int i = 0; i < 16; ++i) {
            lhs[i] ^= rhs[i];
        }
    }

    static bool aes_cmac(const uint8_t* key, size_t key_len,
                         const std::vector<uint8_t>& data, uint8_t* tag)
    {
        std::array<uint8_t, 16> zero{};
        std::array<uint8_t, 16> l{};
        std::array<uint8_t, 16> k1{};
        std::array<uint8_t, 16> k2{};
        std::array<uint8_t, 16> block{};
        std::array<uint8_t, 16> state{};

        if (!aes_encrypt_block(key, key_len, zero.data(), l.data())) {
            return false;
        }

        aes_left_shift_block(l.data(), k1.data());
        if ((l[0] & 0x80u) != 0) {
            k1[15] ^= 0x87u;
        }

        aes_left_shift_block(k1.data(), k2.data());
        if ((k1[0] & 0x80u) != 0) {
            k2[15] ^= 0x87u;
        }

        const size_t block_count = data.empty() ? 1 : (data.size() + 15) / 16;
        const bool complete_last = !data.empty() && (data.size() % 16) == 0;

        for (size_t block_index = 0; block_index < block_count; ++block_index) {
            block.fill(0);
            const size_t data_offset = block_index * block.size();
            const size_t remaining = data.size() > data_offset ? data.size() - data_offset : 0;
            const size_t copy_len = std::min(block.size(), remaining);

            if (copy_len != 0) {
                std::copy(data.begin() + data_offset,
                          data.begin() + data_offset + copy_len,
                          block.begin());
            }

            if (block_index == block_count - 1) {
                if (complete_last) {
                    aes_xor_block(block.data(), k1.data());
                } else {
                    block[copy_len] = 0x80u;
                    aes_xor_block(block.data(), k2.data());
                }
            }

            aes_xor_block(block.data(), state.data());
            if (!aes_encrypt_block(key, key_len, block.data(), state.data())) {
                return false;
            }
        }

        std::copy(state.begin(), state.end(), tag);
        return true;
    }

    void sha256_reset()
    {
        m_sha256.h = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        };
        m_sha256.block.fill(0);
        m_sha256.bytes = 0;
        m_sha256.block_len = 0;
        m_sha256.active = true;
        m_sha256.finalized = false;
        store_hash_current_len(0);
        store_sha256_h();
    }

    void sha256_transform(const uint8_t* block)
    {
        static const uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };
        uint32_t w[64];

        for (unsigned int i = 0; i < 16; ++i) {
            w[i] = load_be32(block + i * sizeof(uint32_t));
        }

        for (unsigned int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = m_sha256.h[0];
        uint32_t b = m_sha256.h[1];
        uint32_t c = m_sha256.h[2];
        uint32_t d = m_sha256.h[3];
        uint32_t e = m_sha256.h[4];
        uint32_t f = m_sha256.h[5];
        uint32_t g = m_sha256.h[6];
        uint32_t h = m_sha256.h[7];

        for (unsigned int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        m_sha256.h[0] += a;
        m_sha256.h[1] += b;
        m_sha256.h[2] += c;
        m_sha256.h[3] += d;
        m_sha256.h[4] += e;
        m_sha256.h[5] += f;
        m_sha256.h[6] += g;
        m_sha256.h[7] += h;
        store_sha256_h();
    }

    void sha256_update(const uint8_t* data, size_t len)
    {
        if (!m_sha256.active || m_sha256.finalized) {
            return;
        }

        m_sha256.bytes += len;
        store_hash_current_len(m_sha256.bytes);
        while (len != 0) {
            const size_t copy_len = std::min<size_t>(len, m_sha256.block.size() - m_sha256.block_len);
            std::memcpy(m_sha256.block.data() + m_sha256.block_len, data, copy_len);
            m_sha256.block_len += copy_len;
            data += copy_len;
            len -= copy_len;

            if (m_sha256.block_len == m_sha256.block.size()) {
                sha256_transform(m_sha256.block.data());
                m_sha256.block_len = 0;
            }
        }
    }

    void sha256_finish()
    {
        if (!m_sha256.active || m_sha256.finalized) {
            return;
        }

        const uint64_t message_bytes = m_sha256.bytes;
        const uint64_t bit_len = message_bytes * 8u;
        uint8_t one = 0x80;
        uint8_t zero = 0x00;
        uint8_t len_bytes[8];

        sha256_update(&one, 1);
        while (m_sha256.block_len != 56) {
            sha256_update(&zero, 1);
        }

        store_be64(len_bytes, bit_len);
        sha256_update(len_bytes, sizeof(len_bytes));

        store_sha256_h();
        store_hash_current_len(message_bytes);
        m_sha256.finalized = true;
    }

    bool mem_read(uint64_t address, uint8_t* data, unsigned int len, sc_core::sc_time& delay)
    {
        if (initiator_socket.size() == 0) {
            return false;
        }

        tlm::tlm_generic_payload trans;

        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(address);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);

        initiator_socket->b_transport(trans, delay);
        return trans.is_response_ok();
    }

    bool mem_write(uint64_t address, const uint8_t* data, unsigned int len, sc_core::sc_time& delay)
    {
        if (initiator_socket.size() == 0) {
            return false;
        }

        tlm::tlm_generic_payload trans;

        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(address);
        trans.set_data_ptr(const_cast<uint8_t*>(data));
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);

        initiator_socket->b_transport(trans, delay);
        return trans.is_response_ok();
    }

    size_t aes_key_size_bytes() const
    {
        switch ((load32(AES_CONTROL) >> 12) & 0x3u) {
        case CC3XX_AES_KEYSIZE_128:
            return 16;
        case CC3XX_AES_KEYSIZE_192:
            return 24;
        case CC3XX_AES_KEYSIZE_256:
            return 32;
        default:
            return 16;
        }
    }

    uint32_t aes_mode() const
    {
        return (load32(AES_CONTROL) >> 2) & 0xfu;
    }

    bool aes_decrypt() const
    {
        return (load32(AES_CONTROL) & 0x1u) != 0;
    }

    bool aes_ctr_xcrypt(uint64_t source, uint64_t dest, uint64_t len, sc_core::sc_time& delay)
    {
        const size_t key_len = aes_key_size_bytes();
        std::array<uint8_t, 32> key{};
        std::array<uint8_t, 16> counter{};
        std::array<uint8_t, 16> stream{};
        std::array<uint8_t, 256> input{};
        std::array<uint8_t, 256> output{};

        std::copy(m_regs.begin() + AES_KEY_0, m_regs.begin() + AES_KEY_0 + key_len, key.begin());
        std::copy(m_regs.begin() + AES_CTR_0, m_regs.begin() + AES_CTR_0 + counter.size(), counter.begin());

        while (len != 0) {
            const auto chunk_len = static_cast<unsigned int>(std::min<uint64_t>(len, input.size()));

            if (!mem_read(source, input.data(), chunk_len, delay)) {
                return false;
            }

            for (unsigned int offset = 0; offset < chunk_len; offset += stream.size()) {
                if (!aes_encrypt_block(key.data(), key_len, counter.data(), stream.data())) {
                    return false;
                }

                const auto block_len =
                    static_cast<unsigned int>(std::min<size_t>(stream.size(), chunk_len - offset));
                for (unsigned int i = 0; i < block_len; ++i) {
                    output[offset + i] = input[offset + i] ^ stream[i];
                }
                aes_increment_counter(counter);
            }

            if (!mem_write(dest, output.data(), chunk_len, delay)) {
                return false;
            }

            source += chunk_len;
            dest += chunk_len;
            len -= chunk_len;
        }

        std::copy(counter.begin(), counter.end(), m_regs.begin() + AES_CTR_0);
        store32(AES_REMAINING_BYTES, 0);
        store32(AES_BUSY, 0);
        return true;
    }

    bool aes_ecb_xcrypt(uint64_t source, uint64_t dest, uint64_t len, sc_core::sc_time& delay)
    {
        if ((len % 16) != 0) {
            return false;
        }

        const size_t key_len = aes_key_size_bytes();
        const bool decrypt = aes_decrypt();
        std::array<uint8_t, 32> key{};
        std::array<uint8_t, 256> input{};
        std::array<uint8_t, 256> output{};

        std::copy(m_regs.begin() + AES_KEY_0, m_regs.begin() + AES_KEY_0 + key_len, key.begin());

        while (len != 0) {
            const auto chunk_len = static_cast<unsigned int>(std::min<uint64_t>(len, input.size()));

            if (!mem_read(source, input.data(), chunk_len, delay)) {
                return false;
            }

            for (unsigned int offset = 0; offset < chunk_len; offset += 16) {
                const bool ok = decrypt ?
                    aes_decrypt_block(key.data(), key_len, input.data() + offset, output.data() + offset) :
                    aes_encrypt_block(key.data(), key_len, input.data() + offset, output.data() + offset);
                if (!ok) {
                    return false;
                }
            }

            if (!mem_write(dest, output.data(), chunk_len, delay)) {
                return false;
            }

            source += chunk_len;
            dest += chunk_len;
            len -= chunk_len;
        }

        store32(AES_REMAINING_BYTES, 0);
        store32(AES_BUSY, 0);
        return true;
    }

    void cmac_reset()
    {
        m_cmac_data.clear();
        m_cmac_active = true;
    }

    void cmac_dma_input(uint32_t trigger_offset, sc_core::sc_time& delay)
    {
        if (!m_cmac_active || m_engine != CC3XX_ENGINE_AES ||
            aes_mode() != CC3XX_AES_MODE_CMAC || trigger_offset != DIN_SRC_LLI_WORD1) {
            return;
        }

        uint64_t source = load32(DIN_SRC_LLI_WORD0);
        uint64_t remaining = load32(DIN_SRC_LLI_WORD1);
        std::array<uint8_t, 256> chunk{};

        while (remaining != 0) {
            const auto len = static_cast<unsigned int>(std::min<uint64_t>(remaining, chunk.size()));
            if (!mem_read(source, chunk.data(), len, delay)) {
                return;
            }

            m_cmac_data.insert(m_cmac_data.end(), chunk.begin(), chunk.begin() + len);
            source += len;
            remaining -= len;
        }
    }

    void cmac_finish()
    {
        if (!m_cmac_active || aes_mode() != CC3XX_AES_MODE_CMAC) {
            return;
        }

        const size_t key_len = aes_key_size_bytes();
        std::array<uint8_t, 32> key{};
        std::array<uint8_t, 16> tag{};

        std::copy(m_regs.begin() + AES_KEY_0, m_regs.begin() + AES_KEY_0 + key_len, key.begin());
        if (aes_cmac(key.data(), key_len, m_cmac_data, tag.data())) {
            std::copy(tag.begin(), tag.end(), m_regs.begin() + AES_IV_0);
        }

        store32(AES_BUSY, 0);
        m_cmac_active = false;
    }

    void reset_registers()
    {
        m_regs.fill(0);
        m_pka_sram.fill(0);
        m_pka_write_addr = 0;
        m_pka_read_addr = 0;
        pka_invalidate_modulus_cache();
        m_sha256 = sha256_state{};
        m_engine = CC3XX_ENGINE_NONE;
        m_cmac_data.clear();
        m_cmac_active = false;

        store32(PKA_STATUS, 0x00000001);
        store32(PKA_PIPE_RDY, 0x00000001);
        store32(PKA_DONE, 0x00000001);

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
        store32(AES_BUSY, 0x00000000);
        store32(AES_HW_FLAGS,
                (1u << 0) |  /* SUPPORT_256_192_KEY_LOCAL */
                    (1u << 3) |  /* CTR_EXISTS_LOCAL */
                    (1u << 12)); /* AES_DFA_IS_SUPPORTED */
        store32(AES_RBG_SEEDING_RDY, 0x00000001);
        store32(CLK_STATUS, 0xffffffff);
        store32(CRYPTO_BUSY, 0x00000000);
        store32(HASH_BUSY, 0x00000000);
        store32(HOST_BOOT,
                (1u << 30) | /* AES_EXISTS_LOCAL */
                    (1u << 28) | /* SUPPORT_256_192_KEY_LOCAL */
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
        store32(HOST_RGF_IRR, 0x00000000);
        store32(HOST_RGF_IMR, 0x00000000);
        store32(DIN_MEM_DMA_BUSY, 0x00000000);
        store32(DIN_SRAM_DMA_BUSY, 0x00000000);
        store32(FIFO_IN_EMPTY, 0x00000001);
        store32(DOUT_MEM_DMA_BUSY, 0x00000000);
        store32(DOUT_SRAM_DMA_BUSY, 0x00000000);
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

    void update_hash_after_write(uint32_t offset, uint32_t value)
    {
        if (offset >= HASH_H && offset < HASH_H + m_sha256.h.size() * sizeof(uint32_t)) {
            store32(offset, value);
            if (m_sha256.active && !m_sha256.finalized) {
                m_sha256.h[(offset - HASH_H) / sizeof(uint32_t)] = value;
            }
            return;
        }

        if (offset == HASH_CUR_LEN0 || offset == HASH_CUR_LEN1) {
            store32(offset, value);
            if (m_sha256.active && !m_sha256.finalized) {
                m_sha256.bytes = hash_current_len();
                m_sha256.block_len = 0;
            }
            return;
        }

        if (offset == HASH_CONTROL) {
            store32(HASH_CONTROL, value);
            if ((value & 0xfu) == CC3XX_HASH_ALG_SHA256) {
                sha256_reset();
            } else {
                m_sha256.active = false;
                m_sha256.finalized = false;
            }
            return;
        }

        if (offset == HASH_PAD_CFG) {
            store32(HASH_PAD_CFG, value);
            if ((value & 0x4u) != 0 && m_sha256.active) {
                sha256_finish();
            }
            return;
        }

        if (offset == AUTO_HW_PADDING) {
            store32(AUTO_HW_PADDING, value);
            return;
        }
    }

    void hash_dma_input(uint32_t trigger_offset, sc_core::sc_time& delay)
    {
        if (m_engine != CC3XX_ENGINE_HASH || !m_sha256.active || m_sha256.finalized) {
            return;
        }

        if (trigger_offset != DIN_SRC_LLI_WORD1) {
            return;
        }

        uint64_t source = load32(DIN_SRC_LLI_WORD0);
        uint64_t remaining = load32(DIN_SRC_LLI_WORD1);
        std::array<uint8_t, 256> chunk{};

        while (remaining != 0) {
            const auto len = static_cast<unsigned int>(std::min<uint64_t>(remaining, chunk.size()));
            if (!mem_read(source, chunk.data(), len, delay)) {
                return;
            }

            sha256_update(chunk.data(), len);
            source += len;
            remaining -= len;
        }

        if ((load32(AUTO_HW_PADDING) & 0x1u) != 0) {
            sha256_finish();
        }
    }

    void aes_dma_output(uint32_t trigger_offset, sc_core::sc_time& delay)
    {
        if (trigger_offset != DIN_SRC_LLI_WORD1) {
            return;
        }

        const bool aes_engine =
            m_engine == CC3XX_ENGINE_AES ||
            m_engine == CC3XX_ENGINE_AES_AND_HASH ||
            m_engine == CC3XX_ENGINE_AES_TO_HASH_AND_DOUT;
        if (!aes_engine) {
            return;
        }

        const uint64_t source = load32(DIN_SRC_LLI_WORD0);
        const uint64_t dest = load32(DOUT_DST_LLI_WORD0);
        const uint64_t len = std::min<uint64_t>(load32(DIN_SRC_LLI_WORD1), load32(DOUT_DST_LLI_WORD1));

        if (len == 0) {
            return;
        }

        switch (aes_mode()) {
        case CC3XX_AES_MODE_ECB:
            (void)aes_ecb_xcrypt(source, dest, len, delay);
            return;
        case CC3XX_AES_MODE_CTR:
            (void)aes_ctr_xcrypt(source, dest, len, delay);
            return;
        default:
            break;
        }
    }

    void complete_dma_transfer(uint32_t trigger_offset, sc_core::sc_time& delay)
    {
        uint32_t interrupts = SYM_DMA_COMPLETED;

        switch (trigger_offset) {
        case DIN_SRC_LLI_WORD1:
            interrupts |= MEM_TO_DIN_INT;
            break;
        case DIN_SRAM_BYTES_LEN:
            interrupts |= SRAM_TO_DIN_INT;
            break;
        case DOUT_DST_LLI_WORD1:
            interrupts |= DOUT_TO_MEM_INT;
            break;
        case DOUT_SRAM_BYTES_LEN:
            interrupts |= DOUT_TO_SRAM_INT;
            break;
        default:
            break;
        }

        hash_dma_input(trigger_offset, delay);
        aes_dma_output(trigger_offset, delay);
        cmac_dma_input(trigger_offset, delay);

        store32(HOST_RGF_IRR, load32(HOST_RGF_IRR) | interrupts);
        store32(DIN_MEM_DMA_BUSY, 0x00000000);
        store32(DIN_SRAM_DMA_BUSY, 0x00000000);
        store32(DOUT_MEM_DMA_BUSY, 0x00000000);
        store32(DOUT_SRAM_DMA_BUSY, 0x00000000);
        store32(FIFO_IN_EMPTY, 0x00000001);
        store32(DOUT_FIFO_EMPTY, 0x00000001);
        store32(HOST_CC_IS_IDLE, 0x00000001);
    }

    void write32(uint32_t offset, uint32_t value, sc_core::sc_time& delay)
    {
        switch (offset) {
        case PKA_SW_RESET:
            store32(PKA_SW_RESET, value);
            m_pka_sram.fill(0);
            m_pka_write_addr = 0;
            m_pka_read_addr = 0;
            pka_invalidate_modulus_cache();
            store32(PKA_STATUS, 0x00000001);
            store32(PKA_PIPE_RDY, 0x00000001);
            store32(PKA_DONE, 0x00000001);
            break;
        case PKA_SRAM_ADDR:
            store32(PKA_SRAM_ADDR, value);
            m_pka_write_addr = value;
            break;
        case PKA_SRAM_WDATA:
            store32(PKA_SRAM_WDATA, value);
            pka_sram_write(m_pka_write_addr++, value);
            break;
        case PKA_SRAM_RADDR:
            store32(PKA_SRAM_RADDR, value);
            m_pka_read_addr = value;
            break;
        case PKA_OPCODE:
            store32(PKA_OPCODE, value);
            execute_pka_opcode(value);
            break;
        case RNG_ICR:
        case RNG_SW_RESET:
        case RND_SOURCE_ENABLE:
        case RST_BITS_COUNTER:
        case RNG_CLK_ENABLE:
            update_rng_after_write(offset, value);
            break;
        case HOST_RGF_ICR:
            store32(HOST_RGF_IRR, load32(HOST_RGF_IRR) & ~value);
            break;
        case HASH_CONTROL:
        case HASH_PAD_CFG:
        case AUTO_HW_PADDING:
        case HASH_CUR_LEN0:
        case HASH_CUR_LEN1:
            update_hash_after_write(offset, value);
            break;
        case AES_CMAC_INIT:
            store32(AES_CMAC_INIT, value);
            if (value & 0x1u) {
                cmac_reset();
            }
            break;
        case AES_REMAINING_BYTES:
            store32(AES_REMAINING_BYTES, value);
            if (value == 0) {
                cmac_finish();
            }
            break;
        case CRYPTO_CTL:
            store32(CRYPTO_CTL, value);
            m_engine = value & 0x1fu;
            store32(CRYPTO_BUSY, 0x00000000);
            store32(HASH_BUSY, 0x00000000);
            store32(HOST_CC_IS_IDLE, 0x00000001);
            break;
        case DIN_SRC_LLI_WORD1:
        case DIN_SRAM_BYTES_LEN:
        case DOUT_DST_LLI_WORD1:
        case DOUT_SRAM_BYTES_LEN:
            store32(offset, value);
            complete_dma_transfer(offset, delay);
            break;
        default:
            if (offset >= HASH_H && offset < HASH_H + m_sha256.h.size() * sizeof(uint32_t)) {
                update_hash_after_write(offset, value);
            } else {
                if (offset == 0) {
                    pka_invalidate_modulus_cache();
                }
                store32(offset, value);
            }
            break;
        }
    }

    bool access(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) || offset + len > m_regs.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            if (!debug && len == sizeof(uint32_t) && offset == PKA_SRAM_RDATA) {
                uint32_t value = pka_sram_read(m_pka_read_addr++);
                store32(PKA_SRAM_RDATA, value);
                std::memcpy(data, &value, sizeof(value));
            } else {
                std::memcpy(data, &m_regs[offset], len);
            }
            if (!debug && offset >= EHR_DATA && offset < EHR_DATA + 24) {
                store32(RNG_ISR, load32(RNG_ISR) | 0x1u);
            }
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            if (len == sizeof(uint32_t) && (offset % sizeof(uint32_t)) == 0) {
                uint32_t value = 0;
                std::memcpy(&value, data, sizeof(value));
                write32(static_cast<uint32_t>(offset), value, delay);
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

    bool trace_filter_matches(uint64_t offset) const
    {
        const auto filter = p_trace_filter.get_value();

        if (filter.empty() || filter == "all") {
            return true;
        }

        if (filter == "pka") {
            return offset < 0x100;
        }

        if (filter == "pka-opcode") {
            return offset == PKA_OPCODE ||
                   offset == PKA_STATUS ||
                   offset == PKA_PIPE_RDY ||
                   offset == PKA_DONE;
        }

        if (filter == "dma") {
            return is_dma_trace_offset(offset);
        }

        if (filter == "crypto") {
            return offset < 0x100 ||
                   (offset >= AES_KEY_0 && offset <= AES_HW_FLAGS) ||
                   (offset >= HASH_H && offset <= HASH_PAD_CFG) ||
                   offset == CRYPTO_CTL ||
                   offset == CRYPTO_BUSY ||
                   offset == HASH_BUSY ||
                   is_dma_trace_offset(offset);
        }

        return false;
    }

    bool is_dma_trace_offset(uint64_t offset) const
    {
        const bool aes_engine =
            m_engine == CC3XX_ENGINE_AES ||
            m_engine == CC3XX_ENGINE_AES_AND_HASH ||
            m_engine == CC3XX_ENGINE_AES_TO_HASH_AND_DOUT;
        const bool input_dma_engine = aes_engine || m_engine == CC3XX_ENGINE_HASH;
        const bool address_match = dma_trace_address_matches();

        switch (offset) {
        case AES_CONTROL:
        case AES_REMAINING_BYTES:
            return address_match;
        case CRYPTO_CTL:
            return input_dma_engine && address_match;
        case HOST_RGF_IRR:
        case HOST_RGF_IMR:
        case HOST_RGF_ICR:
            return input_dma_engine && address_match;
        case DIN_MEM_DMA_BUSY:
        case DIN_SRC_LLI_WORD0:
        case DIN_SRC_LLI_WORD1:
        case DIN_SRAM_SRC_ADDR:
        case DIN_SRAM_BYTES_LEN:
        case DIN_SRAM_DMA_BUSY:
        case FIFO_IN_EMPTY:
            return input_dma_engine && address_match;
        case DOUT_MEM_DMA_BUSY:
        case DOUT_DST_LLI_WORD0:
        case DOUT_DST_LLI_WORD1:
        case DOUT_SRAM_BYTES_LEN:
        case DOUT_SRAM_DMA_BUSY:
        case DOUT_FIFO_EMPTY:
            return address_match;
        default:
            break;
        }

        return address_match &&
               ((offset >= AES_KEY_0 && offset < AES_KEY_0 + 32) ||
                (offset >= AES_CTR_0 && offset < AES_CTR_0 + 16));
    }

    bool dma_trace_address_matches() const
    {
        const auto min_address = p_trace_address_min.get_value();

        if (min_address == 0) {
            return true;
        }

        return load32(DIN_SRC_LLI_WORD0) >= min_address ||
               load32(DOUT_DST_LLI_WORD0) >= min_address;
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset, unsigned int len, bool debug)
    {
        if (!p_trace.get_value() ||
            !trace_filter_matches(offset)) {
            return;
        }

        ++m_trace_seen_count;
        if (m_trace_seen_count <= p_trace_skip.get_value() ||
            m_trace_count >= p_trace_limit.get_value()) {
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
    cci::cci_param<unsigned int> p_trace_skip;
    cci::cci_param<std::string> p_trace_filter;
    cci::cci_param<uint64_t> p_trace_address_min;
    initiator_socket_type initiator_socket;
    tlm_utils::simple_target_socket<cc3xx, DEFAULT_TLM_BUSWIDTH> target_socket;
    TargetSignalSocket<bool> reset;

    explicit cc3xx(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_trace_skip("trace_skip", 0)
        , p_trace_filter("trace_filter", "all")
        , p_trace_address_min("trace_address_min", 0)
        , initiator_socket("initiator_socket")
        , target_socket("target_socket")
        , reset("reset")
    {
        reset_registers();
        target_socket.register_b_transport(this, &cc3xx::b_transport);
        target_socket.register_transport_dbg(this, &cc3xx::transport_dbg);
        reset.register_value_changed_cb([this](bool value) { doreset(value); });
    }

    void doreset(bool value)
    {
        if (value) {
            reset_registers();
        }
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        trans.set_dmi_allowed(false);
        access(trans, delay, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        return access(trans, delay, true) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
