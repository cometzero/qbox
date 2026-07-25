/* SPDX-License-Identifier: BSD-3-Clause */

#include <gtest/gtest.h>

#include <systemc>

#include <libqemu-cxx/libqemu-cxx.h>
#include <limits>
#include <tlm-extensions/request-context.h>

TEST(RequestContext, IdentityValidityIsExplicit)
{
    RequestContext context = make_request_context(
        0x1001, 1, 3, std::numeric_limits<uint32_t>::max(),
        REQUEST_CONTEXT_CAP_BOOT_LOADER);

    EXPECT_TRUE(context.origin_valid);
    EXPECT_TRUE(context.domain_valid);
    EXPECT_TRUE(context.requester_valid);
    EXPECT_FALSE(context.substream_valid);
    EXPECT_EQ(context.origin_id, 0x1001u);
    EXPECT_EQ(context.domain_id, 1u);
    EXPECT_EQ(context.requester_id, 3u);
    EXPECT_EQ(context.capabilities, REQUEST_CONTEXT_CAP_BOOT_LOADER);
}

TEST(RequestContext, QemuNormalizationMapsSecurityAndPrivilege)
{
    RequestContext base = make_request_context(0x1100, 1, 0x40, 7);
    RequestContext context = normalize_qemu_request_context(
        base, true, true, RequestAccessPath::REENTRANT);

    EXPECT_TRUE(context.secure_valid);
    EXPECT_TRUE(context.secure);
    EXPECT_TRUE(context.privileged_valid);
    EXPECT_FALSE(context.privileged);
    EXPECT_EQ(context.access_path, RequestAccessPath::REENTRANT);
    EXPECT_FALSE(context.instruction_valid);
    EXPECT_FALSE(context.ats_valid);
    EXPECT_EQ(context.requester_id, 0x40u);
    EXPECT_EQ(context.substream_id, 7u);
}

TEST(RequestContext, FixedSecurityContextOverridesQemuAttribute)
{
    RequestContext base = make_request_context(0x4000, 4, 0, 0);
    base.secure = true;
    base.secure_valid = true;

    RequestContext context = normalize_qemu_request_context(
        base, false, false, RequestAccessPath::REGULAR);

    EXPECT_TRUE(context.secure_valid);
    EXPECT_TRUE(context.secure);
    EXPECT_EQ(context.access_path, RequestAccessPath::REGULAR);
}

TEST(RequestContext, FixedPrivilegeContextOverridesQemuAttribute)
{
    RequestContext base = make_request_context(0x4000, 4, 0, 0);
    base.privileged = false;
    base.privileged_valid = true;

    RequestContext context = normalize_qemu_request_context(
        base, false, false, RequestAccessPath::REGULAR);

    EXPECT_TRUE(context.privileged_valid);
    EXPECT_FALSE(context.privileged);
}

TEST(RequestContext, CloneAndCopyPreserveEveryField)
{
    RequestContext context = make_request_context(
        0x5003, 5, 3, 9, REQUEST_CONTEXT_CAP_AUTHENTICATED_IMAGE);
    context.secure = true;
    context.secure_valid = true;
    context.privileged = true;
    context.privileged_valid = true;
    context.instruction = true;
    context.instruction_valid = true;
    context.ats = true;
    context.ats_valid = true;
    context.access_path = RequestAccessPath::DMI;
    RequestContextTlmExtension source(context);

    tlm::tlm_extension_base* clone = source.clone();
    auto* cloned = static_cast<RequestContextTlmExtension*>(clone);
    RequestContextTlmExtension copied;
    copied.copy_from(*cloned);
    const RequestContext& observed = copied.get_context();

    EXPECT_EQ(observed.origin_id, context.origin_id);
    EXPECT_EQ(observed.domain_id, context.domain_id);
    EXPECT_EQ(observed.requester_id, context.requester_id);
    EXPECT_EQ(observed.substream_id, context.substream_id);
    EXPECT_EQ(observed.capabilities, context.capabilities);
    EXPECT_EQ(observed.secure, context.secure);
    EXPECT_EQ(observed.privileged, context.privileged);
    EXPECT_EQ(observed.instruction, context.instruction);
    EXPECT_EQ(observed.ats, context.ats);
    EXPECT_EQ(observed.access_path, context.access_path);
    clone->free();
}

TEST(RequestContext, QemuMemTxAttrsCarryPciRequesterId)
{
    qemu::MemoryRegionOps::MemTxAttrs attrs;
    attrs.user = true;
    attrs.requester_id = 0x8;

    EXPECT_TRUE(attrs.user);
    EXPECT_EQ(attrs.requester_id, 0x8u);
}

int sc_main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
