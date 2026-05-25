/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cci/utils/broker.h>

#include <rse_kmu.h>

namespace {

constexpr uint64_t KMUBC = 0x000;
constexpr uint64_t KMUIS = 0x004;
constexpr uint64_t KMUIE = 0x008;
constexpr uint64_t KMUIC = 0x00c;
constexpr uint64_t KMUPRBGSI = 0x010;
constexpr uint64_t KMUKSC0 = 0x030;
constexpr uint64_t KMUKSC4 = 0x030 + 4 * 4;
constexpr uint64_t KMUKSC10 = 0x030 + 10 * 4;
constexpr uint64_t KMUDKPA0 = 0x0b0;
constexpr uint64_t KMUDKPA4 = 0x0b0 + 4 * 4;
constexpr uint64_t KMUDKPA10 = 0x0b0 + 10 * 4;
constexpr uint64_t KMUKSK10 = 0x130 + 10 * 0x20;
constexpr uint32_t KMUIS_KEC = 1u << 0;
constexpr uint32_t KMUKSC_LKSKR = 1u << 23;
constexpr uint32_t KMUKSC_VKS = 1u << 24;
constexpr uint32_t KMUKSC_KSR = 1u << 25;
constexpr uint32_t KMUKSC_IKS = 1u << 26;
constexpr uint32_t KMUKSC_KSIP = 1u << 27;
constexpr uint32_t KMUKSC_EK = 1u << 28;

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

uint32_t access32(rse_kmu& dut, uint64_t offset, tlm::tlm_command command,
                  uint32_t value = 0)
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

uint32_t read32(rse_kmu& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(rse_kmu& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(RseKmuTest, ResetValuesMatchRseTfMDriverExpectations)
{
    rse_kmu dut("rse_kmu");

    EXPECT_EQ(read32(dut, KMUBC), 0x003d0005u);
    EXPECT_EQ(read32(dut, KMUIS), 0x00000000u);
    EXPECT_EQ(read32(dut, KMUIE), 0x00000000u);
    EXPECT_EQ(read32(dut, KMUKSC0), 0x00d60100u);
    EXPECT_EQ(read32(dut, KMUDKPA0), 0x50154400u);
}

TEST(RseKmuTest, InitSeedAndInterruptRegistersAreWritable)
{
    rse_kmu dut("rse_kmu_init");

    write32(dut, KMUPRBGSI, 0x12345678u);
    write32(dut, KMUKSC0, read32(dut, KMUKSC0) | 0x80000000u);
    write32(dut, KMUIE, 0xffffffffu);

    EXPECT_EQ(read32(dut, KMUPRBGSI), 0x12345678u);
    EXPECT_EQ(read32(dut, KMUKSC0) & 0x80000000u, 0x80000000u);
    EXPECT_EQ(read32(dut, KMUIE), 0xffffffffu);
}

TEST(RseKmuTest, ResetSignalRestoresRuntimeRegisters)
{
    rse_kmu dut("rse_kmu_reset");

    write32(dut, KMUPRBGSI, 0x12345678u);
    write32(dut, KMUIE, 0xffffffffu);
    write32(dut, KMUKSC10, KMUKSC_LKSKR | KMUKSC_VKS);

    dut.doreset(true);

    EXPECT_EQ(read32(dut, KMUPRBGSI), 0u);
    EXPECT_EQ(read32(dut, KMUIE), 0u);
    EXPECT_EQ(read32(dut, KMUKSC10), 0u);
}

TEST(RseKmuTest, KeyReadyExportAndInvalidateCompleteWithoutPollingHang)
{
    rse_kmu dut("rse_kmu_export");

    write32(dut, KMUKSC10, KMUKSC_LKSKR | KMUKSC_VKS);
    EXPECT_EQ(read32(dut, KMUKSC10) & KMUKSC_VKS, 0u);
    EXPECT_EQ(read32(dut, KMUKSC10) & KMUKSC_KSR, KMUKSC_KSR);

    write32(dut, KMUKSC10, read32(dut, KMUKSC10) | KMUKSC_EK);
    EXPECT_EQ(read32(dut, KMUKSC10) & KMUKSC_EK, 0u);
    EXPECT_EQ(read32(dut, KMUIS) & KMUIS_KEC, KMUIS_KEC);

    write32(dut, KMUIC, 0xffffffffu);
    EXPECT_EQ(read32(dut, KMUIS), 0u);

    write32(dut, KMUKSC10, read32(dut, KMUKSC10) | KMUKSC_IKS);
    EXPECT_EQ(read32(dut, KMUKSC10) & KMUKSC_IKS, 0u);
    EXPECT_EQ(read32(dut, KMUKSC10) & KMUKSC_KSIP, KMUKSC_KSIP);
}

TEST(RseKmuTest, KeySlotWordsAreWritable)
{
    rse_kmu dut("rse_kmu_key");

    write32(dut, KMUKSK10, 0xaaaaaaaau);
    write32(dut, KMUKSK10 + 4, 0x55555555u);

    EXPECT_EQ(read32(dut, KMUKSK10), 0xaaaaaaaau);
    EXPECT_EQ(read32(dut, KMUKSK10 + 4), 0x55555555u);
}

TEST(RseKmuTest, KeyTraceFilterSuppressesRandomDelayReads)
{
    rse_kmu dut("rse_kmu_trace_key");

    dut.p_trace = true;
    dut.p_trace_filter = std::string("key");
    dut.p_trace_limit = 8;

    std::stringstream captured;
    auto* old_cerr = std::cerr.rdbuf(captured.rdbuf());

    (void)read32(dut, 0x538);
    write32(dut, KMUDKPA10, 0x20);
    write32(dut, KMUKSK10, 0xaaaaaaaau);
    write32(dut, KMUKSC10, KMUKSC_EK);

    std::cerr.rdbuf(old_cerr);

    const auto log = captured.str();
    EXPECT_EQ(log.find("offset=0x538"), std::string::npos);
    EXPECT_NE(log.find("offset=0xd8"), std::string::npos);
    EXPECT_NE(log.find("offset=0x270"), std::string::npos);
    EXPECT_NE(log.find("offset=0x58"), std::string::npos);
}

TEST(RseKmuTest, ExportWritesKeyWordsToDestinationPort)
{
    rse_kmu dut("rse_kmu_tlm_export");
    TestMemory export_window("export_window", 0x100);

    dut.initiator_socket.bind(export_window.target_socket);

    const uint32_t words[] = {
        0x03020100u, 0x07060504u, 0x0b0a0908u, 0x0f0e0d0cu,
        0x13121110u, 0x17161514u, 0x1b1a1918u, 0x1f1e1d1cu,
    };

    write32(dut, KMUDKPA10, 0x20);
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        write32(dut, KMUKSK10 + i * sizeof(uint32_t), words[i]);
    }

    write32(dut, KMUKSC10, read32(dut, KMUKSC10) | KMUKSC_EK);

    EXPECT_EQ(std::memcmp(export_window.bytes.data() + 0x20,
                          reinterpret_cast<const uint8_t*>(words),
                          sizeof(words)),
              0);
    EXPECT_NE(read32(dut, KMUIS) & KMUIS_KEC, 0u);
}

TEST(RseKmuTest, LoadsKceCmHardwareSlotFromOtpImage)
{
    const std::string otp_path = ::testing::TempDir() + "/rse_kmu_otp.bin";
    std::vector<uint8_t> otp(0x100, 0);
    const uint32_t kce_cm_words[] = {
        0x03020100u, 0x07060504u, 0x0b0a0908u, 0x0f0e0d0cu,
        0x13121110u, 0x17161514u, 0x1b1a1918u, 0x1f1e1d1cu,
    };
    std::memcpy(otp.data() + 0x60, kce_cm_words, sizeof(kce_cm_words));

    {
        std::ofstream otp_file(otp_path, std::ios::binary);
        ASSERT_TRUE(otp_file);
        otp_file.write(reinterpret_cast<const char*>(otp.data()), otp.size());
        ASSERT_TRUE(otp_file);
    }

    cci::cci_get_global_broker(cci::cci_originator("rse_kmu_test"))
        .set_preset_cci_value("rse_kmu_from_otp.otp_image", cci::cci_value(otp_path));

    rse_kmu dut("rse_kmu_from_otp");
    TestMemory export_window("otp_export_window", 0x100);

    dut.initiator_socket.bind(export_window.target_socket);
    write32(dut, KMUDKPA4, 0x20);
    write32(dut, KMUKSC4, read32(dut, KMUKSC4) | KMUKSC_EK);

    EXPECT_EQ(std::memcmp(export_window.bytes.data() + 0x20,
                          reinterpret_cast<const uint8_t*>(kce_cm_words),
                          sizeof(kce_cm_words)),
              0);
}

TEST(RseKmuTest, RejectsOutOfRangeTransactions)
{
    rse_kmu dut("rse_kmu_bounds");
    tlm::tlm_generic_payload trans;
    uint32_t data = 0;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x1000 - 1);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    trans.set_data_length(sizeof(data));
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
