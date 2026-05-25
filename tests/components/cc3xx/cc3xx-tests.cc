/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>
#include <iostream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cci/utils/broker.h>

#include <cc3xx.h>

namespace {

constexpr uint64_t RNG_ISR = 0x104;
constexpr uint64_t RNG_ICR = 0x108;
constexpr uint64_t EHR_DATA = 0x114;
constexpr uint64_t RND_SOURCE_ENABLE = 0x12c;
constexpr uint64_t SAMPLE_CNT1 = 0x130;
constexpr uint64_t RNG_SW_RESET = 0x140;
constexpr uint64_t RNG_CLK_ENABLE = 0x1c4;
constexpr uint64_t PKA_OPCODE = 0x080;
constexpr uint64_t PKA_STATUS = 0x088;
constexpr uint64_t PKA_L_BASE = 0x090;
constexpr uint64_t PKA_SRAM_ADDR = 0x0d4;
constexpr uint64_t PKA_SRAM_WDATA = 0x0d8;
constexpr uint64_t PKA_SRAM_RDATA = 0x0dc;
constexpr uint64_t PKA_SRAM_RADDR = 0x0e4;
constexpr uint64_t AES_KEY_0 = 0x400;
constexpr uint64_t AES_IV_0 = 0x440;
constexpr uint64_t AES_CTR_0 = 0x460;
constexpr uint64_t AES_CMAC_INIT = 0x47c;
constexpr uint64_t AES_REMAINING_BYTES = 0x4bc;
constexpr uint64_t AES_HW_FLAGS = 0x4c8;
constexpr uint64_t AES_RBG_SEEDING_RDY = 0x4fc;
constexpr uint64_t AES_CONTROL = 0x4c0;
constexpr uint64_t HASH_H = 0x640;
constexpr uint64_t AUTO_HW_PADDING = 0x684;
constexpr uint64_t HASH_CONTROL = 0x7c0;
constexpr uint64_t HASH_PAD_CFG = 0x7c8;
constexpr uint64_t HASH_CUR_LEN0 = 0x7cc;
constexpr uint64_t HASH_CUR_LEN1 = 0x7d0;
constexpr uint64_t CRYPTO_CTL = 0x900;
constexpr uint64_t HOST_RGF_IRR = 0xa00;
constexpr uint64_t HOST_RGF_ICR = 0xa08;
constexpr uint64_t HOST_BOOT = 0xa28;
constexpr uint64_t HOST_CC_IS_IDLE = 0xa7c;
constexpr uint64_t HOST_SF_READY = 0xa90;
constexpr uint64_t DIN_SRC_LLI_WORD0 = 0xc28;
constexpr uint64_t DIN_SRC_LLI_WORD1 = 0xc2c;
constexpr uint64_t DIN_SRAM_BYTES_LEN = 0xc34;
constexpr uint64_t FIFO_IN_EMPTY = 0xc50;
constexpr uint64_t DOUT_DST_LLI_WORD0 = 0xd28;
constexpr uint64_t DOUT_DST_LLI_WORD1 = 0xd2c;
constexpr uint64_t DOUT_SRAM_BYTES_LEN = 0xd34;
constexpr uint64_t DOUT_FIFO_EMPTY = 0xd50;
constexpr uint64_t LCS_REG = 0x1f14;

constexpr uint32_t SRAM_TO_DIN_INT = 1u << 4;
constexpr uint32_t DOUT_TO_SRAM_INT = 1u << 5;
constexpr uint32_t MEM_TO_DIN_INT = 1u << 6;
constexpr uint32_t DOUT_TO_MEM_INT = 1u << 7;
constexpr uint32_t SYM_DMA_COMPLETED = 1u << 11;
constexpr uint32_t CC3XX_HASH_ALG_SHA256 = 0x02;
constexpr uint32_t CC3XX_ENGINE_AES = 0x01;
constexpr uint32_t CC3XX_ENGINE_HASH = 0x07;
constexpr uint32_t CC3XX_AES_MODE_ECB = 0x00;
constexpr uint32_t CC3XX_AES_MODE_CTR = 0x02;
constexpr uint32_t CC3XX_AES_MODE_CMAC = 0x07;
constexpr uint32_t CC3XX_AES_KEYSIZE_128 = 0x00;
constexpr uint32_t CC3XX_AES_KEYSIZE_256 = 0x02;
constexpr uint32_t PKA_OP_SIZE_N = 0x0;
constexpr uint32_t PKA_OP_SIZE_REGISTER = 0x1;
constexpr uint32_t CC3XX_PKA_OPCODE_SUB_DEC_NEG = 0x05;
constexpr uint32_t CC3XX_PKA_OPCODE_MODADD_MODINC = 0x06;
constexpr uint32_t CC3XX_PKA_OPCODE_MODSUB_MODDEC_MODNEG = 0x07;
constexpr uint32_t CC3XX_PKA_OPCODE_SHR1 = 0x0d;
constexpr uint32_t CC3XX_PKA_OPCODE_SHL0 = 0x0e;
constexpr uint32_t CC3XX_PKA_OPCODE_SHL1 = 0x0f;
constexpr uint32_t CC3XX_PKA_OPCODE_MULLOW = 0x10;
constexpr uint32_t CC3XX_PKA_OPCODE_MODMUL = 0x11;
constexpr uint32_t CC3XX_PKA_OPCODE_MODEXP = 0x13;
constexpr uint32_t CC3XX_PKA_OPCODE_DIV = 0x14;
constexpr uint32_t CC3XX_PKA_OPCODE_MODINV = 0x15;
constexpr uint32_t CC3XX_PKA_OPCODE_MULHIGH = 0x17;
constexpr uint32_t CC3XX_PKA_OPCODE_REDUCTION = 0x1b;
constexpr uint32_t PKA_STATUS_ALU_SIGN_OUT = 1u << 8;
constexpr uint32_t PKA_STATUS_ALU_OUT_ZERO = 1u << 12;

class TestMemory : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<TestMemory, DEFAULT_TLM_BUSWIDTH> target_socket;
    std::vector<uint8_t> bytes;

    TestMemory(sc_core::sc_module_name name, size_t size)
        : sc_core::sc_module(name)
        , target_socket("target_socket")
        , bytes(size, 0)
    {
        target_socket.register_b_transport(this, &TestMemory::b_transport);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;

        const auto address = trans.get_address();
        const auto len = trans.get_data_length();
        auto* data = trans.get_data_ptr();

        if (data == nullptr || address + len > bytes.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, bytes.data() + address, len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            std::memcpy(bytes.data() + address, data, len);
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

uint32_t access32(cc3xx& dut, uint64_t offset, tlm::tlm_command command, uint32_t value = 0)
{
    tlm::tlm_generic_payload trans;
    auto data = value;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    dut.b_transport(trans, delay);

    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint32_t read32(cc3xx& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(cc3xx& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

uint32_t pka_opcode(uint32_t op, uint32_t size, bool lhs_immediate, uint32_t lhs,
                    bool rhs_immediate, uint32_t rhs, bool discard_result,
                    uint32_t result)
{
    uint32_t opcode = ((op & 0x1fu) << 27) | ((size & 0x7u) << 24);

    if (lhs_immediate) {
        opcode |= 1u << 23;
    }
    opcode |= (lhs & 0x1fu) << 18;

    if (rhs_immediate) {
        opcode |= 1u << 17;
    }
    opcode |= (rhs & 0x1fu) << 12;

    if (discard_result) {
        opcode |= 1u << 11;
    } else {
        opcode |= (result & 0x1fu) << 6;
    }

    return opcode;
}

void pka_map_reg(cc3xx& dut, uint32_t reg, uint32_t base_word)
{
    write32(dut, reg * sizeof(uint32_t), base_word);
}

void pka_write_words(cc3xx& dut, uint32_t base_word, std::initializer_list<uint32_t> words)
{
    write32(dut, PKA_SRAM_ADDR, base_word);
    for (auto word : words) {
        write32(dut, PKA_SRAM_WDATA, word);
    }
}

std::vector<uint32_t> pka_read_words(cc3xx& dut, uint32_t base_word, size_t words)
{
    std::vector<uint32_t> result;
    result.reserve(words);

    write32(dut, PKA_SRAM_RADDR, base_word);
    for (size_t i = 0; i < words; ++i) {
        result.push_back(read32(dut, PKA_SRAM_RDATA));
    }

    return result;
}

void write_reg_bytes(cc3xx& dut, uint64_t offset, const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i += sizeof(uint32_t)) {
        uint32_t value = 0;
        std::memcpy(&value, data + i, std::min(sizeof(value), len - i));
        write32(dut, offset + i, value);
    }
}

void read_reg_bytes(cc3xx& dut, uint64_t offset, uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i += sizeof(uint32_t)) {
        const uint32_t value = read32(dut, offset + i);
        std::memcpy(data + i, &value, std::min(sizeof(value), len - i));
    }
}

} // namespace

TEST(Cc3xxTest, ResetValuesCoverEarlyTfmReadiness)
{
    cc3xx dut("cc3xx");

    EXPECT_EQ(read32(dut, RNG_ISR), 0x1u);
    EXPECT_EQ(read32(dut, SAMPLE_CNT1), 0xffffu);
    EXPECT_EQ(read32(dut, AES_RBG_SEEDING_RDY), 0x1u);
    EXPECT_NE(read32(dut, AES_HW_FLAGS) & (1u << 0), 0u);
    EXPECT_NE(read32(dut, AES_HW_FLAGS) & (1u << 3), 0u);
    EXPECT_NE(read32(dut, AES_HW_FLAGS) & (1u << 12), 0u);
    EXPECT_NE(read32(dut, HOST_BOOT) & (1u << 28), 0u);
    EXPECT_NE(read32(dut, HOST_BOOT) & (1u << 11), 0u);
    EXPECT_EQ(read32(dut, HOST_CC_IS_IDLE), 0x1u);
    EXPECT_EQ(read32(dut, HOST_SF_READY), 0x1u);
    EXPECT_EQ(read32(dut, LCS_REG), 0x5u);
}

TEST(Cc3xxTest, ResetSignalRestoresRuntimeRegisters)
{
    cc3xx dut("cc3xx_reset");

    write32(dut, RNG_CLK_ENABLE, 0x1u);
    write32(dut, RND_SOURCE_ENABLE, 0x1u);
    write32(dut, AES_KEY_0, 0xdeadbeefu);

    dut.doreset(true);

    EXPECT_EQ(read32(dut, RNG_CLK_ENABLE), 0u);
    EXPECT_EQ(read32(dut, RND_SOURCE_ENABLE), 0u);
    EXPECT_EQ(read32(dut, AES_KEY_0), 0u);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x1u);
    EXPECT_EQ(read32(dut, HOST_CC_IS_IDLE), 0x1u);
}

TEST(Cc3xxTest, RngControlRegistersSupportObservedBl1Sequence)
{
    cc3xx dut("cc3xx");

    write32(dut, RNG_CLK_ENABLE, 0x1);
    EXPECT_EQ(read32(dut, RNG_CLK_ENABLE), 0x1u);

    write32(dut, RNG_SW_RESET, 0x1);
    EXPECT_EQ(read32(dut, RNG_SW_RESET), 0x1u);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x0u);

    write32(dut, RND_SOURCE_ENABLE, 0x1);
    EXPECT_EQ(read32(dut, RND_SOURCE_ENABLE), 0x1u);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x1u);

    write32(dut, RNG_ICR, 0x1);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x0u);
}

TEST(Cc3xxTest, DeterministicEntropyWordsAreReadable)
{
    cc3xx dut("cc3xx");

    EXPECT_EQ(read32(dut, EHR_DATA + 0x00), 0x243f6a88u);
    EXPECT_EQ(read32(dut, EHR_DATA + 0x04), 0x85a308d3u);
    EXPECT_EQ(read32(dut, EHR_DATA + 0x08), 0x13198a2eu);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x1u);
}

TEST(Cc3xxTest, PkaSramAccessStreamsThroughWordCursors)
{
    cc3xx dut("cc3xx_pka_sram");

    write32(dut, PKA_SRAM_ADDR, 0x18);
    write32(dut, PKA_SRAM_WDATA, 0x11223344);
    write32(dut, PKA_SRAM_WDATA, 0x55667788);

    write32(dut, PKA_SRAM_RADDR, 0x18);
    EXPECT_EQ(read32(dut, PKA_SRAM_RDATA), 0x11223344u);
    EXPECT_EQ(read32(dut, PKA_SRAM_RDATA), 0x55667788u);
}

TEST(Cc3xxTest, PkaAddImmediateExecutesCapturedTfmOpcode)
{
    cc3xx dut("cc3xx_pka_add");

    write32(dut, 3 * sizeof(uint32_t), 0x18);
    write32(dut, PKA_L_BASE + sizeof(uint32_t), 64);
    write32(dut, PKA_SRAM_ADDR, 0x18);
    write32(dut, PKA_SRAM_WDATA, 0x0);
    write32(dut, PKA_SRAM_WDATA, 0x0);

    write32(dut, PKA_OPCODE, 0x210e10c0);

    write32(dut, PKA_SRAM_RADDR, 0x18);
    EXPECT_EQ(read32(dut, PKA_SRAM_RDATA), 0x1u);
    EXPECT_EQ(read32(dut, PKA_SRAM_RDATA), 0x0u);
    EXPECT_EQ(read32(dut, PKA_STATUS) & (1u << 12), 0u);
}

TEST(Cc3xxTest, PkaStatusReportsSubBorrowForLessThan)
{
    cc3xx dut("cc3xx_pka_status");

    write32(dut, PKA_L_BASE + PKA_OP_SIZE_REGISTER * sizeof(uint32_t), 64);
    pka_map_reg(dut, 3, 0x18);
    pka_map_reg(dut, 4, 0x20);
    pka_write_words(dut, 0x18, {2, 0});
    pka_write_words(dut, 0x20, {5, 0});

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_SUB_DEC_NEG, PKA_OP_SIZE_REGISTER,
                       false, 3, false, 4, true, 0));

    EXPECT_NE(read32(dut, PKA_STATUS) & PKA_STATUS_ALU_SIGN_OUT, 0u);
    EXPECT_EQ(read32(dut, PKA_STATUS) & PKA_STATUS_ALU_OUT_ZERO, 0u);

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_SUB_DEC_NEG, PKA_OP_SIZE_REGISTER,
                       false, 4, false, 3, true, 0));

    EXPECT_EQ(read32(dut, PKA_STATUS) & PKA_STATUS_ALU_SIGN_OUT, 0u);
}

TEST(Cc3xxTest, PkaShiftOpcodesUseEncodedImmediateAmount)
{
    cc3xx dut("cc3xx_pka_shift");

    write32(dut, PKA_L_BASE + PKA_OP_SIZE_REGISTER * sizeof(uint32_t), 64);
    pka_map_reg(dut, 3, 0x18);
    pka_map_reg(dut, 4, 0x20);
    pka_map_reg(dut, 5, 0x28);

    pka_write_words(dut, 0x18, {0x80000001, 0x00000000});
    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_SHL0, PKA_OP_SIZE_REGISTER,
                       false, 3, false, 0, false, 4));
    EXPECT_EQ(pka_read_words(dut, 0x20, 2),
              (std::vector<uint32_t>{0x00000002, 0x00000001}));

    pka_write_words(dut, 0x18, {0x00000000, 0x00000000});
    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_SHR1, PKA_OP_SIZE_REGISTER,
                       false, 3, false, 3, false, 5));
    EXPECT_EQ(pka_read_words(dut, 0x28, 2),
              (std::vector<uint32_t>{0x00000000, 0xf0000000}));

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_SHL1, PKA_OP_SIZE_REGISTER,
                       false, 3, false, 3, false, 4));
    EXPECT_EQ(pka_read_words(dut, 0x20, 2),
              (std::vector<uint32_t>{0x0000000f, 0x00000000}));
}

TEST(Cc3xxTest, PkaModularOpcodesUseRegN)
{
    cc3xx dut("cc3xx_pka_modular");

    write32(dut, PKA_L_BASE + PKA_OP_SIZE_N * sizeof(uint32_t), 64);
    write32(dut, PKA_L_BASE + PKA_OP_SIZE_REGISTER * sizeof(uint32_t), 64);
    pka_map_reg(dut, 0, 0x00);
    pka_map_reg(dut, 3, 0x18);
    pka_map_reg(dut, 4, 0x20);
    pka_map_reg(dut, 5, 0x28);
    pka_write_words(dut, 0x00, {17, 0});
    pka_write_words(dut, 0x18, {15, 0});
    pka_write_words(dut, 0x20, {7, 0});

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_MODADD_MODINC, PKA_OP_SIZE_REGISTER,
                       false, 3, false, 4, false, 5));
    EXPECT_EQ(pka_read_words(dut, 0x28, 2),
              (std::vector<uint32_t>{5, 0}));

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_MODSUB_MODDEC_MODNEG, PKA_OP_SIZE_REGISTER,
                       false, 4, false, 3, false, 5));
    EXPECT_EQ(pka_read_words(dut, 0x28, 2),
              (std::vector<uint32_t>{9, 0}));

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_MODMUL, PKA_OP_SIZE_N,
                       false, 3, false, 4, false, 5));
    EXPECT_EQ(pka_read_words(dut, 0x28, 2),
              (std::vector<uint32_t>{3, 0}));
}

TEST(Cc3xxTest, PkaMultiplyLowHighSplitProduct)
{
    cc3xx dut("cc3xx_pka_mul");

    write32(dut, PKA_L_BASE + PKA_OP_SIZE_REGISTER * sizeof(uint32_t), 64);
    pka_map_reg(dut, 3, 0x18);
    pka_map_reg(dut, 4, 0x20);
    pka_map_reg(dut, 5, 0x28);
    pka_map_reg(dut, 6, 0x30);
    pka_write_words(dut, 0x18, {0xffffffff, 0xffffffff});
    pka_write_words(dut, 0x20, {2, 0});

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_MULLOW, PKA_OP_SIZE_REGISTER,
                       false, 3, false, 4, false, 5));
    EXPECT_EQ(pka_read_words(dut, 0x28, 2),
              (std::vector<uint32_t>{0xfffffffe, 0xffffffff}));

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_MULHIGH, PKA_OP_SIZE_REGISTER,
                       false, 3, false, 4, false, 6));
    EXPECT_EQ(pka_read_words(dut, 0x30, 2),
              (std::vector<uint32_t>{1, 0}));
}

TEST(Cc3xxTest, PkaDivWritesQuotientAndRemainder)
{
    cc3xx dut("cc3xx_pka_div");

    write32(dut, PKA_L_BASE + PKA_OP_SIZE_REGISTER * sizeof(uint32_t), 64);
    pka_map_reg(dut, 3, 0x18);
    pka_map_reg(dut, 4, 0x20);
    pka_map_reg(dut, 5, 0x28);
    pka_write_words(dut, 0x18, {20, 0});
    pka_write_words(dut, 0x20, {6, 0});

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_DIV, PKA_OP_SIZE_REGISTER,
                       false, 3, false, 4, false, 5));

    EXPECT_EQ(pka_read_words(dut, 0x28, 2),
              (std::vector<uint32_t>{3, 0}));
    EXPECT_EQ(pka_read_words(dut, 0x18, 2),
              (std::vector<uint32_t>{2, 0}));
}

TEST(Cc3xxTest, PkaReductionModeExpAndInverseUseRegN)
{
    cc3xx dut("cc3xx_pka_exp_inv");

    write32(dut, PKA_L_BASE + PKA_OP_SIZE_N * sizeof(uint32_t), 64);
    write32(dut, PKA_L_BASE + PKA_OP_SIZE_REGISTER * sizeof(uint32_t), 64);
    pka_map_reg(dut, 0, 0x00);
    pka_map_reg(dut, 3, 0x18);
    pka_map_reg(dut, 4, 0x20);
    pka_map_reg(dut, 5, 0x28);
    pka_write_words(dut, 0x00, {17, 0});

    pka_write_words(dut, 0x18, {20, 0});
    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_REDUCTION, PKA_OP_SIZE_N,
                       false, 3, false, 0, false, 3));
    EXPECT_EQ(pka_read_words(dut, 0x18, 2),
              (std::vector<uint32_t>{3, 0}));

    pka_write_words(dut, 0x18, {5, 0});
    pka_write_words(dut, 0x20, {3, 0});
    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_MODEXP, PKA_OP_SIZE_N,
                       false, 3, false, 4, false, 5));
    EXPECT_EQ(pka_read_words(dut, 0x28, 2),
              (std::vector<uint32_t>{6, 0}));

    write32(dut, PKA_OPCODE,
            pka_opcode(CC3XX_PKA_OPCODE_MODINV, PKA_OP_SIZE_N,
                       false, 0, false, 3, false, 5));
    EXPECT_EQ(pka_read_words(dut, 0x28, 2),
              (std::vector<uint32_t>{7, 0}));
}

TEST(Cc3xxTest, DmaLengthWritesRaiseAndClearCompletionInterrupts)
{
    cc3xx dut("cc3xx");

    EXPECT_EQ(read32(dut, HOST_RGF_IRR), 0x0u);

    write32(dut, DIN_SRC_LLI_WORD1, 0x40);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SYM_DMA_COMPLETED, 0u);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & MEM_TO_DIN_INT, 0u);
    EXPECT_EQ(read32(dut, FIFO_IN_EMPTY), 0x1u);
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | MEM_TO_DIN_INT);
    EXPECT_EQ(read32(dut, HOST_RGF_IRR), 0x0u);

    write32(dut, DOUT_DST_LLI_WORD1, 0x20);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SYM_DMA_COMPLETED, 0u);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & DOUT_TO_MEM_INT, 0u);
    EXPECT_EQ(read32(dut, DOUT_FIFO_EMPTY), 0x1u);
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | DOUT_TO_MEM_INT);
    EXPECT_EQ(read32(dut, HOST_RGF_IRR), 0x0u);
}

TEST(Cc3xxTest, SramDmaLengthWritesRaiseSourceSpecificInterrupts)
{
    cc3xx dut("cc3xx");

    write32(dut, DIN_SRAM_BYTES_LEN, 0x10);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SYM_DMA_COMPLETED, 0u);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SRAM_TO_DIN_INT, 0u);
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | SRAM_TO_DIN_INT);

    write32(dut, DOUT_SRAM_BYTES_LEN, 0x10);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SYM_DMA_COMPLETED, 0u);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & DOUT_TO_SRAM_INT, 0u);
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | DOUT_TO_SRAM_INT);
    EXPECT_EQ(read32(dut, HOST_RGF_IRR), 0x0u);
}

TEST(Cc3xxTest, Sha256EmptyHashIsExposedThroughHashStateRegisters)
{
    cc3xx dut("cc3xx");

    write32(dut, HASH_CONTROL, CC3XX_HASH_ALG_SHA256);
    write32(dut, HASH_PAD_CFG, 0x4);

    EXPECT_EQ(read32(dut, HASH_H + 0x00), 0xe3b0c442u);
    EXPECT_EQ(read32(dut, HASH_H + 0x04), 0x98fc1c14u);
    EXPECT_EQ(read32(dut, HASH_H + 0x08), 0x9afbf4c8u);
    EXPECT_EQ(read32(dut, HASH_H + 0x0c), 0x996fb924u);
    EXPECT_EQ(read32(dut, HASH_H + 0x10), 0x27ae41e4u);
    EXPECT_EQ(read32(dut, HASH_H + 0x14), 0x649b934cu);
    EXPECT_EQ(read32(dut, HASH_H + 0x18), 0xa495991bu);
    EXPECT_EQ(read32(dut, HASH_H + 0x1c), 0x7852b855u);
}

TEST(Cc3xxTest, Sha256RestoresMultipartStateFromHashRegisters)
{
    cc3xx dut("cc3xx_hash_state");
    TestMemory memory("hash_state_memory", 0x200);

    dut.initiator_socket.bind(memory.target_socket);

    for (size_t i = 0; i < 64; ++i) {
        memory.bytes[0x20 + i] = static_cast<uint8_t>(i);
    }
    memory.bytes[0xa0] = 'a';
    memory.bytes[0xa1] = 'b';
    memory.bytes[0xa2] = 'c';

    write32(dut, HASH_CONTROL, CC3XX_HASH_ALG_SHA256);
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_HASH);
    write32(dut, DIN_SRC_LLI_WORD0, 0x20);
    write32(dut, DIN_SRC_LLI_WORD1, 64);

    uint32_t saved_h[8];
    for (size_t i = 0; i < 8; ++i) {
        saved_h[i] = read32(dut, HASH_H + i * sizeof(uint32_t));
    }
    const uint32_t saved_len0 = read32(dut, HASH_CUR_LEN0);
    const uint32_t saved_len1 = read32(dut, HASH_CUR_LEN1);

    write32(dut, HASH_CONTROL, CC3XX_HASH_ALG_SHA256);
    for (size_t i = 0; i < 8; ++i) {
        write32(dut, HASH_H + i * sizeof(uint32_t), saved_h[i]);
    }
    write32(dut, HASH_CUR_LEN0, saved_len0);
    write32(dut, HASH_CUR_LEN1, saved_len1);
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_HASH);
    write32(dut, AUTO_HW_PADDING, 0x1);
    write32(dut, DIN_SRC_LLI_WORD0, 0xa0);
    write32(dut, DIN_SRC_LLI_WORD1, 3);

    EXPECT_EQ(read32(dut, HASH_H + 0x00), 0xeb1c9345u);
    EXPECT_EQ(read32(dut, HASH_H + 0x04), 0xc34c8a49u);
    EXPECT_EQ(read32(dut, HASH_H + 0x08), 0x11d0006du);
    EXPECT_EQ(read32(dut, HASH_H + 0x0c), 0xf18e6ab4u);
    EXPECT_EQ(read32(dut, HASH_H + 0x10), 0x31cb9751u);
    EXPECT_EQ(read32(dut, HASH_H + 0x14), 0xa4992197u);
    EXPECT_EQ(read32(dut, HASH_H + 0x18), 0x7fdc00a2u);
    EXPECT_EQ(read32(dut, HASH_H + 0x1c), 0x4ecaffb7u);
}

TEST(Cc3xxTest, Sha256FinalBlockWithAutoPaddingMatchesTfmBl1ImagePattern)
{
    cc3xx dut("cc3xx_hash_final_block");
    TestMemory memory("hash_final_block_memory", 0x6000);

    dut.initiator_socket.bind(memory.target_socket);

    for (size_t i = 0; i < 8192; ++i) {
        const auto value = static_cast<uint8_t>(i & 0xffu);

        if (i < 8128) {
            memory.bytes[0x1000 + i] = value;
        } else {
            memory.bytes[0x5000 + i - 8128] = value;
        }
    }

    write32(dut, HASH_CONTROL, CC3XX_HASH_ALG_SHA256);
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_HASH);
    write32(dut, DIN_SRC_LLI_WORD0, 0x1000);
    write32(dut, DIN_SRC_LLI_WORD1, 8128);
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | MEM_TO_DIN_INT);

    write32(dut, AUTO_HW_PADDING, 0x1);
    write32(dut, DIN_SRC_LLI_WORD0, 0x5000);
    write32(dut, DIN_SRC_LLI_WORD1, 64);

    EXPECT_EQ(read32(dut, HASH_H + 0x00), 0xdc404a61u);
    EXPECT_EQ(read32(dut, HASH_H + 0x04), 0x3fedaeb5u);
    EXPECT_EQ(read32(dut, HASH_H + 0x08), 0x4034514bu);
    EXPECT_EQ(read32(dut, HASH_H + 0x0c), 0xc6505f56u);
    EXPECT_EQ(read32(dut, HASH_H + 0x10), 0xb933caa5u);
    EXPECT_EQ(read32(dut, HASH_H + 0x14), 0x250299bau);
    EXPECT_EQ(read32(dut, HASH_H + 0x18), 0x7d094377u);
    EXPECT_EQ(read32(dut, HASH_H + 0x1c), 0xa51caa46u);
}

TEST(Cc3xxTest, AesCtrMemToMemDmaDecryptsIntoOutputBuffer)
{
    cc3xx dut("cc3xx");
    TestMemory memory("memory", 0x100);

    dut.initiator_socket.bind(memory.target_socket);

    const uint8_t key[] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    const uint8_t counter[] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
    };
    const uint8_t ciphertext[] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
    };
    const uint8_t plaintext[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    };

    std::memcpy(memory.bytes.data() + 0x20, ciphertext, sizeof(ciphertext));
    write_reg_bytes(dut, AES_KEY_0, key, sizeof(key));
    write_reg_bytes(dut, AES_CTR_0, counter, sizeof(counter));
    write32(dut, AES_CONTROL, (CC3XX_AES_KEYSIZE_128 << 12) | (CC3XX_AES_MODE_CTR << 2));
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_AES);
    write32(dut, DOUT_DST_LLI_WORD0, 0x60);
    write32(dut, DOUT_DST_LLI_WORD1, sizeof(ciphertext));
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | DOUT_TO_MEM_INT);
    write32(dut, DIN_SRC_LLI_WORD0, 0x20);
    write32(dut, DIN_SRC_LLI_WORD1, sizeof(ciphertext));

    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SYM_DMA_COMPLETED, 0u);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & MEM_TO_DIN_INT, 0u);
    EXPECT_EQ(std::memcmp(memory.bytes.data() + 0x60, plaintext, sizeof(plaintext)), 0);
}

TEST(Cc3xxTest, AesCtrMemToMemDmaDecryptsInPlace)
{
    cc3xx dut("cc3xx_ctr_in_place");
    TestMemory memory("ctr_in_place_memory", 0x100);

    dut.initiator_socket.bind(memory.target_socket);

    const uint8_t key[] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    const uint8_t counter[] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
    };
    const uint8_t ciphertext[] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
    };
    const uint8_t plaintext[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    };

    std::memcpy(memory.bytes.data() + 0x20, ciphertext, sizeof(ciphertext));
    write_reg_bytes(dut, AES_KEY_0, key, sizeof(key));
    write_reg_bytes(dut, AES_CTR_0, counter, sizeof(counter));
    write32(dut, AES_CONTROL, (CC3XX_AES_KEYSIZE_128 << 12) | (CC3XX_AES_MODE_CTR << 2));
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_AES);
    write32(dut, DOUT_DST_LLI_WORD0, 0x20);
    write32(dut, DOUT_DST_LLI_WORD1, sizeof(ciphertext));
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | DOUT_TO_MEM_INT);
    write32(dut, DIN_SRC_LLI_WORD0, 0x20);
    write32(dut, DIN_SRC_LLI_WORD1, sizeof(ciphertext));

    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SYM_DMA_COMPLETED, 0u);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & MEM_TO_DIN_INT, 0u);
    EXPECT_EQ(std::memcmp(memory.bytes.data() + 0x20, plaintext, sizeof(plaintext)), 0);
}

TEST(Cc3xxTest, AesEcbMemToMemDmaDecryptsIntoOutputBuffer)
{
    cc3xx dut("cc3xx_ecb");
    TestMemory memory("ecb_memory", 0x100);

    dut.initiator_socket.bind(memory.target_socket);

    const uint8_t key[] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    const uint8_t ciphertext[] = {
        0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
        0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97,
    };
    const uint8_t plaintext[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    };

    std::memcpy(memory.bytes.data() + 0x20, ciphertext, sizeof(ciphertext));
    write_reg_bytes(dut, AES_KEY_0, key, sizeof(key));
    write32(dut, AES_CONTROL,
            (CC3XX_AES_KEYSIZE_128 << 12) | (CC3XX_AES_MODE_ECB << 2) | 0x1u);
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_AES);
    write32(dut, DOUT_DST_LLI_WORD0, 0x60);
    write32(dut, DOUT_DST_LLI_WORD1, sizeof(ciphertext));
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | DOUT_TO_MEM_INT);
    write32(dut, DIN_SRC_LLI_WORD0, 0x20);
    write32(dut, DIN_SRC_LLI_WORD1, sizeof(ciphertext));

    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SYM_DMA_COMPLETED, 0u);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & MEM_TO_DIN_INT, 0u);
    EXPECT_EQ(read32(dut, AES_REMAINING_BYTES), 0u);
    EXPECT_EQ(std::memcmp(memory.bytes.data() + 0x60, plaintext, sizeof(plaintext)), 0);
}

TEST(Cc3xxTest, Aes256EcbMemToMemDmaDecryptsIntoOutputBuffer)
{
    cc3xx dut("cc3xx_ecb_256");
    TestMemory memory("ecb_256_memory", 0x100);

    dut.initiator_socket.bind(memory.target_socket);

    const uint8_t key[] = {
        0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
        0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
        0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
        0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4,
    };
    const uint8_t ciphertext[] = {
        0xf3, 0xee, 0xd1, 0xbd, 0xb5, 0xd2, 0xa0, 0x3c,
        0x06, 0x4b, 0x5a, 0x7e, 0x3d, 0xb1, 0x81, 0xf8,
    };
    const uint8_t plaintext[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    };

    std::memcpy(memory.bytes.data() + 0x20, ciphertext, sizeof(ciphertext));
    write_reg_bytes(dut, AES_KEY_0, key, sizeof(key));
    write32(dut, AES_CONTROL,
            (CC3XX_AES_KEYSIZE_256 << 12) | (CC3XX_AES_MODE_ECB << 2) | 0x1u);
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_AES);
    write32(dut, DOUT_DST_LLI_WORD0, 0x60);
    write32(dut, DOUT_DST_LLI_WORD1, sizeof(ciphertext));
    write32(dut, HOST_RGF_ICR, SYM_DMA_COMPLETED | DOUT_TO_MEM_INT);
    write32(dut, DIN_SRC_LLI_WORD0, 0x20);
    write32(dut, DIN_SRC_LLI_WORD1, sizeof(ciphertext));

    EXPECT_NE(read32(dut, HOST_RGF_IRR) & SYM_DMA_COMPLETED, 0u);
    EXPECT_NE(read32(dut, HOST_RGF_IRR) & MEM_TO_DIN_INT, 0u);
    EXPECT_EQ(read32(dut, AES_REMAINING_BYTES), 0u);
    EXPECT_EQ(std::memcmp(memory.bytes.data() + 0x60, plaintext, sizeof(plaintext)), 0);
}

TEST(Cc3xxTest, DmaTraceFilterCapturesTransferProgramming)
{
    cc3xx dut("cc3xx_trace_dma");

    dut.p_trace = true;
    dut.p_trace_filter = std::string("dma");
    dut.p_trace_limit = 16;

    std::stringstream captured;
    auto* old_cerr = std::cerr.rdbuf(captured.rdbuf());

    write32(dut, HASH_CONTROL, CC3XX_HASH_ALG_SHA256);
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_AES);
    write32(dut, DOUT_DST_LLI_WORD0, 0x60);
    write32(dut, DOUT_DST_LLI_WORD1, 0x10);
    write32(dut, DIN_SRC_LLI_WORD0, 0x20);
    write32(dut, DIN_SRC_LLI_WORD1, 0x10);

    std::cerr.rdbuf(old_cerr);

    const auto log = captured.str();
    EXPECT_NE(log.find("offset=0x900"), std::string::npos);
    EXPECT_NE(log.find("offset=0xd28"), std::string::npos);
    EXPECT_NE(log.find("offset=0xd2c"), std::string::npos);
    EXPECT_NE(log.find("offset=0xc28"), std::string::npos);
    EXPECT_NE(log.find("offset=0xc2c"), std::string::npos);
    EXPECT_EQ(log.find("offset=0x7c0"), std::string::npos);
}

TEST(Cc3xxTest, DmaTraceCanFilterBelowAddressThreshold)
{
    cc3xx dut("cc3xx_trace_dma_threshold");

    dut.p_trace = true;
    dut.p_trace_filter = std::string("dma");
    dut.p_trace_address_min = 0x70000000u;
    dut.p_trace_limit = 16;

    std::stringstream captured;
    auto* old_cerr = std::cerr.rdbuf(captured.rdbuf());

    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_AES);
    write32(dut, DOUT_DST_LLI_WORD0, 0x60);
    write32(dut, DOUT_DST_LLI_WORD1, 0x10);
    write32(dut, DIN_SRC_LLI_WORD0, 0x20);
    write32(dut, DIN_SRC_LLI_WORD1, 0x10);
    write32(dut, DOUT_DST_LLI_WORD0, 0x70000020);
    write32(dut, DOUT_DST_LLI_WORD1, 0x10);
    write32(dut, DIN_SRC_LLI_WORD0, 0x70000040);
    write32(dut, DIN_SRC_LLI_WORD1, 0x10);

    std::cerr.rdbuf(old_cerr);

    const auto log = captured.str();
    EXPECT_EQ(log.find("offset=0xd28 len=0x4 value=0x60"), std::string::npos);
    EXPECT_EQ(log.find("offset=0xc28 len=0x4 value=0x20"), std::string::npos);
    EXPECT_NE(log.find("offset=0xd28 len=0x4 value=0x70000020"), std::string::npos);
    EXPECT_NE(log.find("offset=0xc28 len=0x4 value=0x70000040"), std::string::npos);
}

TEST(Cc3xxTest, AesCmacFinishExposesTagThroughIvRegisters)
{
    cc3xx dut("cc3xx_cmac");
    TestMemory memory("cmac_memory", 0x100);

    dut.initiator_socket.bind(memory.target_socket);

    const uint8_t key[] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    const uint8_t message[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    };
    const uint8_t expected_tag[] = {
        0x07, 0x0a, 0x16, 0xb4, 0x6b, 0x4d, 0x41, 0x44,
        0xf7, 0x9b, 0xdd, 0x9d, 0xd0, 0x4a, 0x28, 0x7c,
    };
    uint8_t actual_tag[sizeof(expected_tag)] = {};

    std::memcpy(memory.bytes.data() + 0x20, message, sizeof(message));
    write_reg_bytes(dut, AES_KEY_0, key, sizeof(key));
    write32(dut, AES_CONTROL, (CC3XX_AES_KEYSIZE_128 << 12) | (CC3XX_AES_MODE_CMAC << 2));
    write32(dut, AES_CMAC_INIT, 0x1);
    write32(dut, CRYPTO_CTL, CC3XX_ENGINE_AES);
    write32(dut, AES_REMAINING_BYTES, sizeof(message));
    write32(dut, DIN_SRC_LLI_WORD0, 0x20);
    write32(dut, DIN_SRC_LLI_WORD1, sizeof(message));
    write32(dut, AES_REMAINING_BYTES, 0x0);

    read_reg_bytes(dut, AES_IV_0, actual_tag, sizeof(actual_tag));
    EXPECT_EQ(std::memcmp(actual_tag, expected_tag, sizeof(expected_tag)), 0);
}

TEST(Cc3xxTest, RejectsUnsupportedAndOutOfRangeTransactions)
{
    cc3xx dut("cc3xx");
    tlm::tlm_generic_payload trans;
    uint8_t data[3] = {};
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0);
    trans.set_data_ptr(data);
    trans.set_data_length(sizeof(data));
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_ADDRESS_ERROR_RESPONSE);

    trans.set_data_length(sizeof(uint32_t));
    trans.set_address(0x10000 - 1);
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
