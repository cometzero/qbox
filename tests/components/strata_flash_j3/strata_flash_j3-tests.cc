/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <strata_flash_j3.h>
#include <tests/initiator-tester.h>

namespace {

constexpr uint8_t CMD_READ_ARRAY = 0xff;
constexpr uint8_t CMD_READ_ID_CODE = 0x90;
constexpr uint8_t CMD_READ_STATUS_REG = 0x70;
constexpr uint8_t CMD_CLEAR_STATUS_REG = 0x50;
constexpr uint8_t CMD_WRITE_TO_BUFFER = 0xe8;
constexpr uint8_t CMD_WORD_PROGRAM = 0x40;
constexpr uint8_t CMD_BLOCK_ERASE = 0x20;
constexpr uint8_t CMD_BLOCK_ERASE_ACK = 0xd0;
constexpr uint8_t STATUS_READY = 0x80;

tlm::tlm_response_status access(strata_flash_j3& dut, uint64_t offset,
                                tlm::tlm_command command, uint8_t* data,
                                unsigned int len)
{
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    trans.set_data_ptr(data);

    dut.b_transport(trans, delay);
    return trans.get_response_status();
}

uint8_t read8(strata_flash_j3& dut, uint64_t offset)
{
    uint8_t value = 0;
    EXPECT_EQ(access(dut, offset, tlm::TLM_READ_COMMAND, &value, sizeof(value)),
              tlm::TLM_OK_RESPONSE);
    return value;
}

void write8(strata_flash_j3& dut, uint64_t offset, uint8_t value)
{
    EXPECT_EQ(access(dut, offset, tlm::TLM_WRITE_COMMAND, &value, sizeof(value)),
              tlm::TLM_OK_RESPONSE);
}

void write_bytes(strata_flash_j3& dut, uint64_t offset, const uint8_t* data,
                 unsigned int len)
{
    EXPECT_EQ(access(dut, offset, tlm::TLM_WRITE_COMMAND,
                     const_cast<uint8_t*>(data), len),
              tlm::TLM_OK_RESPONSE);
}

void write32(strata_flash_j3& dut, uint64_t offset, uint32_t value)
{
    uint8_t data[sizeof(value)];

    std::memcpy(data, &value, sizeof(data));
    write_bytes(dut, offset, data, sizeof(data));
}

class TempImage {
public:
    explicit TempImage(const std::vector<uint8_t>& bytes)
    {
#ifdef _WIN32
        char path[L_tmpnam];
        std::tmpnam(path);
        m_path = path;
#else
        char path[] = "/tmp/strata_flash_j3_test_XXXXXX";
        int fd = mkstemp(path);
        EXPECT_GE(fd, 0);
        if (fd >= 0) {
            close(fd);
        }
        m_path = path;
#endif
        std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
        EXPECT_TRUE(out.good());
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    ~TempImage()
    {
        if (!m_path.empty()) {
            std::remove(m_path.c_str());
        }
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

std::vector<uint8_t> read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

std::string read_text_file(const std::string& path)
{
    const std::vector<uint8_t> bytes = read_file(path);
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

TEST(StrataFlashJ3Test, LoadAndReadArrayModeReturnsImageBytes)
{
    strata_flash_j3 dut("strata_flash_read");
    const uint8_t image[] = {0x12, 0x34, 0x56, 0x78};

    dut.load_image(image, 0x20, sizeof(image));

    EXPECT_EQ(read8(dut, 0x20), 0x12);
    EXPECT_EQ(read8(dut, 0x21), 0x34);
    EXPECT_EQ(read8(dut, 0x22), 0x56);
    EXPECT_EQ(read8(dut, 0x23), 0x78);
}

TEST(StrataFlashJ3Test, RseBootFlashLoaderPathLoadsImageBytes)
{
    strata_flash_j3 dut("strata_flash_rse_boot_loader");
    uint8_t image[] = {0xa5, 0x5a, 0xc3, 0x3c};

    dut.load.ptr_load(image, 0x27000, sizeof(image));

    EXPECT_EQ(read8(dut, 0x27000), 0xa5);
    EXPECT_EQ(read8(dut, 0x27001), 0x5a);
    EXPECT_EQ(read8(dut, 0x27002), 0xc3);
    EXPECT_EQ(read8(dut, 0x27003), 0x3c);
}

TEST(StrataFlashJ3Test, ReadIdAndStatusCommandsDoNotMutateArray)
{
    strata_flash_j3 dut("strata_flash_id");
    const uint8_t image[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee};

    dut.load_image(image, 0, sizeof(image));

    write8(dut, 0, CMD_READ_ID_CODE);
    EXPECT_EQ(read8(dut, 0), 0x89);
    EXPECT_EQ(read8(dut, 4), 0x18);

    write8(dut, 0, CMD_READ_STATUS_REG);
    EXPECT_EQ(read8(dut, 0), 0x80);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0), 0xaa);
    EXPECT_EQ(read8(dut, 4), 0xee);
}

TEST(StrataFlashJ3Test, ProgramUsesNorBitClearSemantics)
{
    strata_flash_j3 dut("strata_flash_program");
    const uint8_t image[] = {0xff, 0xf0};

    dut.load_image(image, 0x100, sizeof(image));

    write8(dut, 0x100, CMD_CLEAR_STATUS_REG);
    write8(dut, 0x100, CMD_WORD_PROGRAM);
    write8(dut, 0x100, 0x0f);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x100), 0x0f);

    write8(dut, 0x101, CMD_WORD_PROGRAM);
    write8(dut, 0x101, 0xff);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x101), 0xf0);
}

TEST(StrataFlashJ3Test, OptionalProgramFfCanRestoreErasedBytes)
{
    strata_flash_j3 dut("strata_flash_program_ff_sets_bits");
    const uint8_t image[] = {0x00};

    dut.p_program_ff_sets_bits = true;
    dut.load_image(image, 0x180, sizeof(image));

    write8(dut, 0x180, CMD_CLEAR_STATUS_REG);
    write8(dut, 0x180, CMD_WORD_PROGRAM);
    write8(dut, 0x180, 0xff);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x180), 0xff);
}

TEST(StrataFlashJ3Test, ProgramFfParameterChangeUpdatesFastPath)
{
    strata_flash_j3 dut("strata_flash_program_ff_param_change");
    const uint8_t image[] = {0x00, 0x00};

    dut.load_image(image, 0x190, sizeof(image));

    write8(dut, 0x190, CMD_WORD_PROGRAM);
    write8(dut, 0x190, 0xff);
    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x190), 0x00);

    dut.p_program_ff_sets_bits = true;
    write8(dut, 0x191, CMD_WORD_PROGRAM);
    write8(dut, 0x191, 0xff);
    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x191), 0xff);
}

TEST(StrataFlashJ3Test, OptionalSectorAlignedProgramFfCanEraseSector)
{
    strata_flash_j3 dut("strata_flash_program_ff_erases_sector");
    uint8_t image[0x200];

    std::memset(image, 0x00, sizeof(image));
    dut.p_sector_size = 0x100;
    dut.p_program_ff_erases_sector = true;
    dut.load_image(image, 0, sizeof(image));

    write8(dut, 0x100, CMD_CLEAR_STATUS_REG);
    write8(dut, 0x100, CMD_WORD_PROGRAM);
    write8(dut, 0x100, 0xff);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x100), 0xff);
    EXPECT_EQ(read8(dut, 0x17f), 0xff);
    EXPECT_EQ(read8(dut, 0x180), 0xff);
    EXPECT_EQ(read8(dut, 0x0ff), 0x00);
}

TEST(StrataFlashJ3Test, SectorEraseCompatibilityRequiresSectorAlignment)
{
    strata_flash_j3 dut("strata_flash_program_ff_requires_alignment");
    uint8_t image[0x200];

    std::memset(image, 0x00, sizeof(image));
    dut.p_sector_size = 0x100;
    dut.p_program_ff_erases_sector = true;
    dut.p_program_ff_sets_bits = true;
    dut.load_image(image, 0, sizeof(image));

    write8(dut, 0x101, CMD_CLEAR_STATUS_REG);
    write8(dut, 0x101, CMD_WORD_PROGRAM);
    write8(dut, 0x101, 0xff);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x100), 0x00);
    EXPECT_EQ(read8(dut, 0x101), 0xff);
    EXPECT_EQ(read8(dut, 0x102), 0x00);
}

TEST(StrataFlashJ3Test, ByteProgramReportsReadyDuringStatusPolling)
{
    strata_flash_j3 dut("strata_flash_byte_program");
    const uint8_t image[] = {0xff};

    dut.load_image(image, 0x200, sizeof(image));

    write8(dut, 0x200, CMD_CLEAR_STATUS_REG);
    write8(dut, 0x200, CMD_WORD_PROGRAM);
    write8(dut, 0x200, 0x5a);

    write8(dut, 0x200, CMD_READ_STATUS_REG);
    EXPECT_EQ(read8(dut, 0x200), STATUS_READY);

    write8(dut, 0x200, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x200), 0x5a);
}

TEST(StrataFlashJ3Test, WriteBufferProgramsSequentialBytes)
{
    strata_flash_j3 dut("strata_flash_write_buffer");
    std::vector<uint8_t> image(0x300, 0xff);
    const uint8_t data[] = {0x12, 0x34, 0x56, 0x78};

    dut.load_image(image.data(), 0, image.size());

    write8(dut, 0x100, CMD_WRITE_TO_BUFFER);
    EXPECT_EQ(read8(dut, 0x100), STATUS_READY);
    write8(dut, 0x100, sizeof(data) - 1);
    write_bytes(dut, 0x100, data, sizeof(data));
    write8(dut, 0x100, CMD_BLOCK_ERASE_ACK);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x100), 0x12);
    EXPECT_EQ(read8(dut, 0x101), 0x34);
    EXPECT_EQ(read8(dut, 0x102), 0x56);
    EXPECT_EQ(read8(dut, 0x103), 0x78);
}

TEST(StrataFlashJ3Test, WriteBufferAcceptsWordPayloadWrites)
{
    strata_flash_j3 dut("strata_flash_write_buffer_word_payload");
    std::vector<uint8_t> image(0x300, 0xff);

    dut.load_image(image.data(), 0, image.size());

    write8(dut, 0x180, CMD_WRITE_TO_BUFFER);
    write32(dut, 0x180, 3);
    write32(dut, 0x180, 0x78563412);
    write8(dut, 0x180, CMD_BLOCK_ERASE_ACK);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x180), 0x12);
    EXPECT_EQ(read8(dut, 0x181), 0x34);
    EXPECT_EQ(read8(dut, 0x182), 0x56);
    EXPECT_EQ(read8(dut, 0x183), 0x78);
}

TEST(StrataFlashJ3Test, DmiIsReadOnlyAndOnlyGrantedInArrayMode)
{
    strata_flash_j3 dut("strata_flash_dmi_array");
    const uint8_t image[] = {0x12, 0x34, 0x56, 0x78};
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi;
    uint8_t value = 0;

    dut.p_enable_dmi = true;
    dut.load_image(image, 0, sizeof(image));

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(1);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(&value);

    EXPECT_TRUE(dut.get_direct_mem_ptr(trans, dmi));
    EXPECT_TRUE(dmi.is_read_allowed());
    EXPECT_FALSE(dmi.is_write_allowed());
    EXPECT_EQ(dmi.get_start_address(), 0u);
    EXPECT_GE(dmi.get_end_address(), sizeof(image) - 1);

    write8(dut, 0, CMD_READ_STATUS_REG);
    EXPECT_FALSE(dut.get_direct_mem_ptr(trans, dmi));
}

TEST(StrataFlashJ3Test, ArrayReadMarksPayloadDmiAllowed)
{
    strata_flash_j3 dut("strata_flash_dmi_hint");
    const uint8_t image[] = {0x12, 0x34};
    uint8_t value = 0;
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    dut.p_enable_dmi = true;
    dut.load_image(image, 0, sizeof(image));

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(&value);

    dut.b_transport(trans, delay);

    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(value, 0x12);
    EXPECT_TRUE(trans.is_dmi_allowed());

    write8(dut, 0, CMD_READ_STATUS_REG);
    value = 0;
    trans.set_dmi_allowed(false);
    dut.b_transport(trans, delay);

    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    EXPECT_FALSE(trans.is_dmi_allowed());
}

TEST(StrataFlashJ3Test, DmiParameterChangeUpdatesFastPath)
{
    strata_flash_j3 dut("strata_flash_dmi_param_change");
    const uint8_t image[] = {0x12, 0x34};
    uint8_t value = 0;
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    dut.load_image(image, 0, sizeof(image));

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(&value);

    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    EXPECT_FALSE(trans.is_dmi_allowed());

    trans.set_dmi_allowed(false);
    dut.p_enable_dmi = true;
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    EXPECT_TRUE(trans.is_dmi_allowed());
}

TEST(StrataFlashJ3Test, IgnoreCommandCanQueryArrayDmiForQemuMap)
{
    strata_flash_j3 dut("strata_flash_dmi_map_query");
    const uint8_t image[] = {0x12, 0x34, 0x56, 0x78};
    uint8_t value = 0;
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi;

    dut.p_enable_dmi = true;
    dut.load_image(image, 0, sizeof(image));

    trans.set_command(tlm::TLM_IGNORE_COMMAND);
    trans.set_address(0);
    trans.set_data_length(0);
    trans.set_streaming_width(0);
    trans.set_data_ptr(&value);

    EXPECT_TRUE(dut.get_direct_mem_ptr(trans, dmi));
    EXPECT_TRUE(dmi.is_read_allowed());
    EXPECT_FALSE(dmi.is_write_allowed());
    EXPECT_EQ(dmi.get_start_address(), 0u);
    EXPECT_GE(dmi.get_end_address(), sizeof(image) - 1);
}

TEST(StrataFlashJ3Test, DmiRangesLimitDirectMapWindow)
{
    strata_flash_j3 dut("strata_flash_dmi_ranges");
    std::vector<uint8_t> image(0x400, 0xff);
    uint8_t value = 0;
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi;

    image[0x110] = 0x5a;
    dut.p_enable_dmi = true;
    dut.p_dmi_ranges = "0x100:0x80,0x300-0x33f";
    dut.load_image(image.data(), 0, image.size());

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x110);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(&value);

    EXPECT_TRUE(dut.get_direct_mem_ptr(trans, dmi));
    EXPECT_TRUE(dmi.is_read_allowed());
    EXPECT_FALSE(dmi.is_write_allowed());
    EXPECT_EQ(dmi.get_start_address(), 0x100u);
    EXPECT_EQ(dmi.get_end_address(), 0x17fu);
    EXPECT_EQ(dmi.get_dmi_ptr()[0x10], 0x5a);

    trans.set_address(0x200);
    EXPECT_FALSE(dut.get_direct_mem_ptr(trans, dmi));
}

TEST(StrataFlashJ3Test, ProgramSequenceInvalidatesActiveDmiOnlyOnce)
{
    strata_flash_j3 dut("strata_flash_dmi_invalidate_once");
    InitiatorTester initiator("strata_flash_dmi_initiator");
    std::vector<uint8_t> image(0x400, 0xff);
    uint8_t value = 0;
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi;
    unsigned int invalidations = 0;

    dut.p_enable_dmi = true;
    dut.load_image(image.data(), 0, image.size());
    initiator.socket.bind(dut.target_socket);
    initiator.register_invalidate_direct_mem_ptr(
        [&](uint64_t start, uint64_t end) {
            ++invalidations;
            EXPECT_EQ(start, 0u);
            EXPECT_GE(end, image.size() - 1);
        });

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x100);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(&value);

    EXPECT_TRUE(initiator.socket->get_direct_mem_ptr(trans, dmi));
    EXPECT_EQ(invalidations, 0u);

    write8(dut, 0x120, CMD_WORD_PROGRAM);
    EXPECT_EQ(invalidations, 1u);

    write8(dut, 0x120, 0x5a);
    EXPECT_EQ(invalidations, 1u);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(invalidations, 1u);
    EXPECT_TRUE(initiator.socket->get_direct_mem_ptr(trans, dmi));

    write8(dut, 0, CMD_READ_STATUS_REG);
    EXPECT_EQ(invalidations, 2u);
}

TEST(StrataFlashJ3Test, ArrayReadDmiHintRequiresConfiguredRange)
{
    strata_flash_j3 dut("strata_flash_dmi_range_hint");
    std::vector<uint8_t> image(0x400, 0xff);
    uint8_t value = 0;
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    image[0x110] = 0x5a;
    image[0x200] = 0xa5;
    dut.p_enable_dmi = true;
    dut.p_dmi_ranges = "0x100:0x80";
    dut.load_image(image.data(), 0, image.size());

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x110);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(&value);

    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(value, 0x5a);
    EXPECT_TRUE(trans.is_dmi_allowed());

    value = 0;
    trans.set_address(0x200);
    trans.set_dmi_allowed(true);
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(value, 0xa5);
    EXPECT_FALSE(trans.is_dmi_allowed());
}

TEST(StrataFlashJ3Test, EraseCommandRestoresSectorToErasedValue)
{
    strata_flash_j3 dut("strata_flash_erase");
    const uint8_t image[] = {0x00, 0x11, 0x22, 0x33};

    dut.p_sector_size = 0x100;
    dut.load_image(image, 0x120, sizeof(image));

    write8(dut, 0x120, CMD_BLOCK_ERASE);
    write8(dut, 0x120, CMD_BLOCK_ERASE_ACK);

    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x120), 0xff);
    EXPECT_EQ(read8(dut, 0x123), 0xff);
}

TEST(StrataFlashJ3Test, ProgramWritesThroughToBackingFile)
{
    std::vector<uint8_t> image(0x200, 0xff);
    TempImage backing(image);
    strata_flash_j3 dut("strata_flash_program_backing");

    dut.p_backing_file = backing.path();
    dut.load_image(image.data(), 0, image.size());

    write8(dut, 0x120, CMD_WORD_PROGRAM);
    write8(dut, 0x120, 0x5a);

    const std::vector<uint8_t> persisted = read_file(backing.path());
    ASSERT_GT(persisted.size(), 0x120u);
    EXPECT_EQ(persisted[0x120], 0x5a);
}

TEST(StrataFlashJ3Test, DeferredBackingWriteFlushesOnDestruction)
{
    std::vector<uint8_t> image(0x200, 0xff);
    TempImage backing(image);

    {
        strata_flash_j3 dut("strata_flash_defer_backing");

        dut.p_backing_file = backing.path();
        dut.p_defer_backing_write = true;
        dut.load_image(image.data(), 0, image.size());

        write8(dut, 0x120, CMD_WORD_PROGRAM);
        write8(dut, 0x120, 0x5a);

        const std::vector<uint8_t> before_flush = read_file(backing.path());
        ASSERT_GT(before_flush.size(), 0x120u);
        EXPECT_EQ(before_flush[0x120], 0xff);
    }

    const std::vector<uint8_t> persisted = read_file(backing.path());
    ASSERT_GT(persisted.size(), 0x120u);
    EXPECT_EQ(persisted[0x120], 0x5a);
}

TEST(StrataFlashJ3Test, NoopProgramSkipsBackingFileWrite)
{
    std::vector<uint8_t> image(0x200, 0xff);
    TempImage backing(std::vector<uint8_t>(0x100, 0xff));
    strata_flash_j3 dut("strata_flash_noop_backing");

    dut.p_backing_file = backing.path();
    dut.p_program_ff_sets_bits = true;
    dut.load_image(image.data(), 0, image.size());

    write8(dut, 0x180, CMD_WORD_PROGRAM);
    testing::internal::CaptureStderr();
    write8(dut, 0x180, 0xff);
    const std::string stderr_output = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(stderr_output.empty());
    write8(dut, 0, CMD_READ_ARRAY);
    EXPECT_EQ(read8(dut, 0x180), 0xff);
}

TEST(StrataFlashJ3Test, StatsFileRecordsProgramAndNoopCounters)
{
    std::vector<uint8_t> image(0x200, 0xff);
    TempImage stats({});
    strata_flash_j3 dut("strata_flash_stats_program");

    dut.p_stats_file = stats.path();
    dut.p_stats_interval = 1;
    dut.p_program_ff_sets_bits = true;
    dut.load_image(image.data(), 0, image.size());

    write8(dut, 0x120, CMD_WORD_PROGRAM);
    write8(dut, 0x120, 0x5a);
    write8(dut, 0x121, CMD_WORD_PROGRAM);
    write8(dut, 0x121, 0xff);

    const std::string text = read_text_file(stats.path());
    EXPECT_NE(text.find("\"word_program_cmds\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"program_ops\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"program_bytes\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"program_changed_bytes\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"program_noop_bytes\": 1"), std::string::npos);
}

TEST(StrataFlashJ3Test, StatsParameterChangeUpdatesFastPath)
{
    std::vector<uint8_t> image(0x200, 0xff);
    TempImage stats({});

    {
        strata_flash_j3 dut("strata_flash_stats_param_change");

        dut.load_image(image.data(), 0, image.size());

        write8(dut, 0x120, CMD_WORD_PROGRAM);
        write8(dut, 0x120, 0x5a);

        dut.p_stats_file = stats.path();
        write8(dut, 0x121, CMD_WORD_PROGRAM);
        write8(dut, 0x121, 0xa5);
    }

    const std::string text = read_text_file(stats.path());
    EXPECT_NE(text.find("\"word_program_cmds\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"program_ops\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"program_changed_bytes\": 1"), std::string::npos);
}

TEST(StrataFlashJ3Test, StatsFileRecordsSectorEraseCompatibility)
{
    std::vector<uint8_t> image(0x200, 0x00);
    TempImage stats({});
    strata_flash_j3 dut("strata_flash_stats_sector_compat");

    dut.p_stats_file = stats.path();
    dut.p_stats_interval = 1;
    dut.p_sector_size = 0x100;
    dut.p_program_ff_erases_sector = true;
    dut.load_image(image.data(), 0, image.size());

    write8(dut, 0x100, CMD_WORD_PROGRAM);
    write8(dut, 0x100, 0xff);

    const std::string text = read_text_file(stats.path());
    EXPECT_NE(text.find("\"program_ops\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"compat_ff_sector_erase_ops\": 1"),
              std::string::npos);
    EXPECT_NE(text.find("\"sector_erase_ops\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"sector_erase_bytes\": 256"), std::string::npos);
}

TEST(StrataFlashJ3Test, StatsFileRecordsWriteBufferCounters)
{
    std::vector<uint8_t> image(0x200, 0xff);
    TempImage stats({});
    strata_flash_j3 dut("strata_flash_stats_write_buffer");
    const uint8_t data[] = {0xa5, 0x5a, 0x3c, 0xc3};

    dut.p_stats_file = stats.path();
    dut.p_stats_interval = 1;
    dut.load_image(image.data(), 0, image.size());

    write8(dut, 0x40, CMD_WRITE_TO_BUFFER);
    write8(dut, 0x40, sizeof(data) - 1);
    write_bytes(dut, 0x40, data, sizeof(data));
    write8(dut, 0x40, CMD_BLOCK_ERASE_ACK);

    const std::string text = read_text_file(stats.path());
    EXPECT_NE(text.find("\"write_buffer_cmds\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"write_buffer_count_writes\": 1"),
              std::string::npos);
    EXPECT_NE(text.find("\"write_buffer_data_writes\": 1"),
              std::string::npos);
    EXPECT_NE(text.find("\"write_buffer_confirm_cmds\": 1"),
              std::string::npos);
    EXPECT_NE(text.find("\"write_buffer_ops\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"write_buffer_bytes\": 4"), std::string::npos);
}

TEST(StrataFlashJ3Test, StatsFileRecordsDeferredBackingCounters)
{
    std::vector<uint8_t> image(0x200, 0xff);
    TempImage backing(image);
    TempImage stats({});

    {
        strata_flash_j3 dut("strata_flash_stats_defer_backing");

        dut.p_backing_file = backing.path();
        dut.p_defer_backing_write = true;
        dut.p_stats_file = stats.path();
        dut.p_stats_interval = 1;
        dut.load_image(image.data(), 0, image.size());

        write8(dut, 0x120, CMD_WORD_PROGRAM);
        write8(dut, 0x120, 0x5a);
        write8(dut, 0x121, CMD_WORD_PROGRAM);
        write8(dut, 0x121, 0xa5);
    }

    const std::string text = read_text_file(stats.path());
    EXPECT_NE(text.find("\"backing_write_ops\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"backing_deferred_ranges\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"backing_flush_ops\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"backing_flush_bytes\": 2"), std::string::npos);
}

TEST(StrataFlashJ3Test, StatsFileRecordsDmiCounters)
{
    std::vector<uint8_t> image(0x400, 0xff);
    TempImage stats({});
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi;
    uint8_t value = 0;

    {
        strata_flash_j3 dut("strata_flash_stats_dmi");
        InitiatorTester initiator("strata_flash_stats_dmi_initiator");

        image[0x110] = 0x5a;
        dut.p_stats_file = stats.path();
        dut.p_stats_interval = 1;
        dut.p_enable_dmi = true;
        dut.p_dmi_ranges = "0x100:0x80";
        dut.load_image(image.data(), 0, image.size());
        initiator.socket.bind(dut.target_socket);

        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(0x110);
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_data_ptr(&value);
        EXPECT_EQ(access(dut, 0x110, tlm::TLM_READ_COMMAND, &value,
                         sizeof(value)),
                  tlm::TLM_OK_RESPONSE);
        EXPECT_EQ(value, 0x5a);
        EXPECT_TRUE(initiator.socket->get_direct_mem_ptr(trans, dmi));
        const std::string grant_text = read_text_file(stats.path());
        EXPECT_NE(grant_text.find("\"dmi_grants\": 1"), std::string::npos);

        trans.set_address(0x200);
        EXPECT_FALSE(initiator.socket->get_direct_mem_ptr(trans, dmi));

        write8(dut, 0, CMD_READ_STATUS_REG);
        trans.set_address(0x110);
        EXPECT_FALSE(initiator.socket->get_direct_mem_ptr(trans, dmi));
    }

    const std::string text = read_text_file(stats.path());
    EXPECT_NE(text.find("\"dmi_read_hints\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"dmi_requests\": 3"), std::string::npos);
    EXPECT_NE(text.find("\"dmi_grants\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"dmi_reject_state\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"dmi_reject_range\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"dmi_invalidations\": 1"), std::string::npos);
}

TEST(StrataFlashJ3Test, EraseWritesThroughToBackingFile)
{
    std::vector<uint8_t> image(0x200, 0x00);
    TempImage backing(image);
    strata_flash_j3 dut("strata_flash_erase_backing");

    dut.p_backing_file = backing.path();
    dut.p_sector_size = 0x100;
    dut.load_image(image.data(), 0, image.size());

    write8(dut, 0x120, CMD_BLOCK_ERASE);
    write8(dut, 0x120, CMD_BLOCK_ERASE_ACK);

    const std::vector<uint8_t> persisted = read_file(backing.path());
    ASSERT_GT(persisted.size(), 0x1ffu);
    EXPECT_EQ(persisted[0x0ff], 0x00);
    EXPECT_EQ(persisted[0x100], 0xff);
    EXPECT_EQ(persisted[0x1ff], 0xff);
}

TEST(StrataFlashJ3Test, RejectsDmi)
{
    strata_flash_j3 dut("strata_flash_dmi");
    uint8_t value = 0;
    tlm::tlm_generic_payload trans;
    tlm::tlm_dmi dmi;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(&value);

    EXPECT_FALSE(dut.get_direct_mem_ptr(trans, dmi));
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
