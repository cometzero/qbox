/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <host_cmn_cyprus.h>
#include <systemc>
#include <tlm>

namespace {

constexpr uint64_t CFGM_PERIPH_ID1 = 0x010;
constexpr uint64_t CFGM_CHILD_INFO = 0x080;
constexpr uint64_t CFGM_CHILD_POINTER = 0x100;
constexpr uint64_t MXP_BASE = 0x10000;
constexpr uint64_t NODE_BASE = 0x20000;
constexpr uint64_t NODE_STRIDE = 0x10000;
constexpr uint64_t MXP_CHILD_INFO = MXP_BASE + 0x080;
constexpr uint64_t MXP_CHILD_POINTER = MXP_BASE + 0x100;
constexpr uint64_t MXP_PORT_DISABLE = MXP_BASE + 0xa70;
constexpr uint64_t HNS0_SAM_CONTROL = NODE_BASE + 0xd00;
constexpr uint64_t RNSAM_BASE = NODE_BASE + (8 * NODE_STRIDE);
constexpr uint64_t RNSAM_UNIT_INFO0 = RNSAM_BASE + 0x900;
constexpr uint64_t RNSAM_UNIT_INFO1 = RNSAM_BASE + 0x908;
constexpr uint64_t RNSAM_STATUS = RNSAM_BASE + 0x1100;

constexpr uint64_t NODE_TYPE_CFG = 0x002;
constexpr uint64_t NODE_TYPE_XP = 0x006;
constexpr uint64_t NODE_TYPE_RN_SAM = 0x00f;
constexpr uint64_t NODE_TYPE_HN_S = 0x200;
constexpr uint64_t RNSAM_NONHASH_RCOMP_EN = UINT64_C(1) << 31;
constexpr uint64_t RNSAM_HTG_RCOMP_EN = UINT64_C(1) << 27;

uint64_t node(uint64_t type, uint64_t node_id, uint64_t logical_id)
{
    return (logical_id << 32) | (node_id << 16) | type;
}

uint64_t access(host_cmn_cyprus& dut, uint64_t offset,
                tlm::tlm_command command, uint64_t value = 0,
                unsigned int len = sizeof(uint64_t),
                tlm::tlm_response_status expected = tlm::TLM_OK_RESPONSE)
{
    tlm::tlm_generic_payload trans;
    auto data = value;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    dut.b_transport(trans, delay);

    EXPECT_EQ(trans.get_response_status(), expected);
    return data;
}

uint64_t read64(host_cmn_cyprus& dut, uint64_t offset)
{
    return access(dut, offset, tlm::TLM_READ_COMMAND);
}

void write64(host_cmn_cyprus& dut, uint64_t offset, uint64_t value)
{
    (void)access(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(HostCmnCyprusTest, ExposesCfgmRootAndMxpDiscovery)
{
    host_cmn_cyprus dut("host_cmn_cfgm");
    dut.p_revision = 2u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read64(dut, 0), node(NODE_TYPE_CFG, 0, 0));
    EXPECT_EQ(read64(dut, CFGM_PERIPH_ID1), 0x20u);
    EXPECT_EQ(read64(dut, CFGM_CHILD_INFO), 1u);
    EXPECT_EQ(read64(dut, CFGM_CHILD_POINTER), MXP_BASE);
    EXPECT_EQ(read64(dut, MXP_BASE) & 0xffffu, NODE_TYPE_XP);
    EXPECT_EQ((read64(dut, MXP_BASE) >> 48) & 0xfu, 4u);
    EXPECT_EQ(read64(dut, MXP_CHILD_INFO), 9u);
}

TEST(HostCmnCyprusTest, ExposesEightHnsNodesAndOneRnSam)
{
    host_cmn_cyprus dut("host_cmn_nodes");
    dut.before_end_of_elaboration();

    EXPECT_EQ(read64(dut, MXP_CHILD_POINTER), NODE_BASE);
    EXPECT_EQ(read64(dut, NODE_BASE), node(NODE_TYPE_HN_S, 64, 0));
    EXPECT_EQ(read64(dut, NODE_BASE + (7 * NODE_STRIDE)),
              node(NODE_TYPE_HN_S, 280, 7));
    EXPECT_EQ(read64(dut, MXP_CHILD_POINTER + (8 * sizeof(uint64_t))),
              RNSAM_BASE);
    EXPECT_EQ(read64(dut, RNSAM_BASE), node(NODE_TYPE_RN_SAM, 12, 0));
}

TEST(HostCmnCyprusTest, RnSamAdvertisesRangeComparisonMode)
{
    host_cmn_cyprus dut("host_cmn_rnsam");
    dut.before_end_of_elaboration();

    EXPECT_EQ(read64(dut, RNSAM_UNIT_INFO0),
              RNSAM_NONHASH_RCOMP_EN | RNSAM_HTG_RCOMP_EN);
    EXPECT_EQ(read64(dut, RNSAM_UNIT_INFO1), (20u << 5) | 20u);
    EXPECT_EQ(read64(dut, RNSAM_STATUS), 0x2u);
}

TEST(HostCmnCyprusTest, PreservesFirmwareProgrammingWrites)
{
    host_cmn_cyprus dut("host_cmn_writes");
    dut.before_end_of_elaboration();

    write64(dut, HNS0_SAM_CONTROL, 0x123456789abcdef0ULL);
    write64(dut, RNSAM_STATUS, 0x0000000000000001ULL);
    write64(dut, MXP_PORT_DISABLE, 0x00000000ffff0000ULL);

    EXPECT_EQ(read64(dut, HNS0_SAM_CONTROL), 0x123456789abcdef0ULL);
    EXPECT_EQ(read64(dut, RNSAM_STATUS), 0x0000000000000001ULL);
    EXPECT_EQ(read64(dut, MXP_PORT_DISABLE), 0x00000000ffff0000ULL);
}

TEST(HostCmnCyprusTest, UnseededRegistersReadAsZero)
{
    host_cmn_cyprus dut("host_cmn_zero");
    dut.before_end_of_elaboration();

    EXPECT_EQ(read64(dut, 0x1234560), 0u);
}

TEST(HostCmnCyprusTest, RejectsAccessOutsideOneGiBWindow)
{
    host_cmn_cyprus dut("host_cmn_bounds");
    dut.before_end_of_elaboration();

    (void)access(dut, 0x40000000, tlm::TLM_READ_COMMAND, 0, sizeof(uint64_t),
                 tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
