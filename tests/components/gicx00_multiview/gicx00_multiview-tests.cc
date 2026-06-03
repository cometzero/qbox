/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <cci/utils/broker.h>
#include <gicx00_multiview.h>
#include <gtest/gtest.h>
#include <systemc>
#include <tlm>

namespace {

constexpr uint64_t GICD_CTLR = 0x0000;
constexpr uint64_t GICD_CFGID = 0xf000;
constexpr uint64_t GICD_IVIEWR0 = 0xf600;
constexpr uint64_t GICR_PWRR = 0x0024;
constexpr uint64_t GICR_VIEWR = 0x002c;
constexpr uint64_t GICD_CFGID_VIEW = 1ull << 53;

template <typename T>
T access(gicx00_multiview& dut, bool dist, unsigned int redist,
         uint64_t offset, tlm::tlm_command command, T value = 0)
{
    tlm::tlm_generic_payload trans;
    auto data = value;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    if (dist) {
        dut.b_transport_dist(trans, delay);
    } else {
        dut.b_transport_redist(redist, trans, delay);
    }

    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint32_t read_dist32(gicx00_multiview& dut, uint64_t offset)
{
    return access<uint32_t>(dut, true, 0, offset, tlm::TLM_READ_COMMAND);
}

uint64_t read_dist64(gicx00_multiview& dut, uint64_t offset)
{
    return access<uint64_t>(dut, true, 0, offset, tlm::TLM_READ_COMMAND);
}

void write_dist32(gicx00_multiview& dut, uint64_t offset, uint32_t value)
{
    (void)access<uint32_t>(dut, true, 0, offset, tlm::TLM_WRITE_COMMAND, value);
}

uint32_t read_redist32(gicx00_multiview& dut, unsigned int redist,
                       uint64_t offset)
{
    return access<uint32_t>(dut, false, redist, offset, tlm::TLM_READ_COMMAND);
}

void write_redist32(gicx00_multiview& dut, unsigned int redist,
                    uint64_t offset, uint32_t value)
{
    (void)access<uint32_t>(
        dut, false, redist, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(Gicx00MultiviewTest, AdvertisesViewCapability)
{
    gicx00_multiview dut("gicx00_multiview_cfgid");

    EXPECT_EQ(read_dist64(dut, GICD_CFGID) & GICD_CFGID_VIEW,
              GICD_CFGID_VIEW);
}

TEST(Gicx00MultiviewTest, StoresDistributorViewRegisters)
{
    gicx00_multiview dut("gicx00_multiview_dist");

    write_dist32(dut, GICD_CTLR, 0x7u);
    write_dist32(dut, GICD_IVIEWR0, 0x55555555u);

    EXPECT_EQ(read_dist32(dut, GICD_CTLR), 0x7u);
    EXPECT_EQ(read_dist32(dut, GICD_IVIEWR0), 0x55555555u);
}

TEST(Gicx00MultiviewTest, StoresRedistributorViewAndKeepsPowered)
{
    gicx00_multiview dut("gicx00_multiview_redist");

    write_redist32(dut, 0, GICR_PWRR, 0xffffffffu);
    write_redist32(dut, 0, GICR_VIEWR, 0x1u);
    write_redist32(dut, 4, GICR_VIEWR, 0x2u);
    write_redist32(dut, 15, GICR_VIEWR, 0x1u);

    EXPECT_EQ(read_redist32(dut, 0, GICR_PWRR) & 0x1u, 0x0u);
    EXPECT_EQ(read_redist32(dut, 0, GICR_VIEWR), 0x1u);
    EXPECT_EQ(read_redist32(dut, 4, GICR_VIEWR), 0x2u);
    EXPECT_EQ(read_redist32(dut, 15, GICR_VIEWR), 0x1u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
