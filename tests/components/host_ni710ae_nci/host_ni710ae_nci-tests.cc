/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <host_ni710ae_nci.h>
#include <systemc>
#include <tlm>

namespace {

constexpr uint64_t ROOT_CHILD_INFO = 0x004;
constexpr uint64_t ROOT_FIRST_POINTER = 0x008;
constexpr uint64_t COMPONENT_BASE = 0x0100;
constexpr uint64_t COMPONENT_STRIDE = 0x0100;
constexpr uint64_t COMPONENT_NUM_SUBFEATURES = 0x024;
constexpr uint64_t COMPONENT_SUBFEATURES = 0x028;
constexpr uint64_t APU_BASE = 0x2000;
constexpr uint64_t APU_STRIDE = 0x1000;
constexpr uint64_t APU_CTLR = 0x0ff8;
constexpr uint64_t APU_IIDR = 0x0ffc;

constexpr uint32_t NODE_ASNI = 0x04;
constexpr uint32_t NODE_AMNI = 0x05;
constexpr uint32_t NODE_APU = 0x00;

uint32_t node(uint32_t type, uint32_t id)
{
    return (id << 16) | type;
}

uint32_t access32(host_ni710ae_nci& dut, uint64_t offset,
                  tlm::tlm_command command, uint32_t value = 0)
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

uint32_t read32(host_ni710ae_nci& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(host_ni710ae_nci& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(HostNi710AeNciTest, MhuMidTopologyExposesConfiguredAsniApu)
{
    host_ni710ae_nci dut("host_ni710ae_mhu_mid");
    dut.p_topology = 1u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, ROOT_CHILD_INFO), 1u);
    EXPECT_EQ(read32(dut, ROOT_FIRST_POINTER), COMPONENT_BASE);
    EXPECT_EQ(read32(dut, COMPONENT_BASE), node(NODE_ASNI, 4));
    EXPECT_EQ(read32(dut, COMPONENT_BASE + COMPONENT_NUM_SUBFEATURES), 1u);
    EXPECT_EQ(read32(dut, COMPONENT_BASE + COMPONENT_SUBFEATURES), NODE_APU);
    EXPECT_EQ(read32(dut, COMPONENT_BASE + COMPONENT_SUBFEATURES + 4),
              APU_BASE);
}

TEST(HostNi710AeNciTest, SecondaryTopologyExposesRseMmAsni)
{
    host_ni710ae_nci dut("host_ni710ae_secondary");
    dut.p_topology = 2u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, ROOT_CHILD_INFO), 1u);
    EXPECT_EQ(read32(dut, COMPONENT_BASE), node(NODE_ASNI, 1));
}

TEST(HostNi710AeNciTest, PrimaryMidTopologyExposesApolloConfiguredComponents)
{
    host_ni710ae_nci dut("host_ni710ae_primary_mid");
    dut.p_topology = 4u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, ROOT_CHILD_INFO), 5u);
    EXPECT_EQ(read32(dut, COMPONENT_BASE), node(NODE_ASNI, 0));
    EXPECT_EQ(read32(dut, COMPONENT_BASE + COMPONENT_STRIDE), node(NODE_ASNI, 5));
    EXPECT_EQ(read32(dut, COMPONENT_BASE + 2 * COMPONENT_STRIDE),
              node(NODE_ASNI, 6));
    EXPECT_EQ(read32(dut, COMPONENT_BASE + 3 * COMPONENT_STRIDE),
              node(NODE_ASNI, 7));
    EXPECT_EQ(read32(dut, COMPONENT_BASE + 4 * COMPONENT_STRIDE),
              node(NODE_AMNI, 14));
}

TEST(HostNi710AeNciTest, ApuRegionAndControlWritesArePreserved)
{
    host_ni710ae_nci dut("host_ni710ae_apu_writes");
    dut.p_topology = 1u;
    dut.before_end_of_elaboration();

    write32(dut, APU_BASE + 0x000, 0x3c000001u);
    write32(dut, APU_BASE + 0x008, 0x3c0bfff1u);
    write32(dut, APU_BASE + APU_CTLR, 0x00000005u);

    EXPECT_EQ(read32(dut, APU_BASE + 0x000), 0x3c000001u);
    EXPECT_EQ(read32(dut, APU_BASE + 0x008), 0x3c0bfff1u);
    EXPECT_EQ(read32(dut, APU_BASE + APU_CTLR), 0x00000005u);
}

TEST(HostNi710AeNciTest, ApuIidrResetValueIsConfigurableAndReadOnly)
{
    host_ni710ae_nci dut("host_ni710ae_iidr");
    dut.p_topology = 1u;
    dut.p_apu_iidr = 0x12345678u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, APU_BASE + APU_IIDR), 0x12345678u);
    write32(dut, APU_BASE + APU_IIDR, 0);
    EXPECT_EQ(read32(dut, APU_BASE + APU_IIDR), 0x12345678u);
}

TEST(HostNi710AeNciTest, EachPrimaryComponentGetsDistinctApuBlock)
{
    host_ni710ae_nci dut("host_ni710ae_apu_blocks");
    dut.p_topology = 4u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, COMPONENT_BASE + COMPONENT_SUBFEATURES + 4),
              APU_BASE);
    EXPECT_EQ(read32(dut, COMPONENT_BASE + COMPONENT_STRIDE +
                         COMPONENT_SUBFEATURES + 4),
              APU_BASE + APU_STRIDE);

    write32(dut, APU_BASE + 0x000, 0x11111111u);
    write32(dut, APU_BASE + APU_STRIDE + 0x000, 0x22222222u);
    EXPECT_EQ(read32(dut, APU_BASE + 0x000), 0x11111111u);
    EXPECT_EQ(read32(dut, APU_BASE + APU_STRIDE + 0x000), 0x22222222u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
