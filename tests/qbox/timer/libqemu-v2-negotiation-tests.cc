/* SPDX-License-Identifier: BSD-3-Clause */

#include <cstring>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <systemc>

#include <libqemu-cxx/libqemu-cxx.h>

namespace {

LibQemuExports* truncated_init_v2(int, char**, uint32_t, size_t,
                                  size_t* actual_size)
{
    static LibQemuExports exports = {};
    *actual_size = LIBQEMU_ARM_TIMER_REQUIRED_STRUCT_SIZE - 1;
    return &exports;
}

class TruncatedV2Library : public qemu::LibraryIface
{
public:
    bool symbol_exists(const char* symbol) override
    {
        return std::strcmp(symbol, LIBQEMU_INIT_V2_SYM_STR) == 0;
    }

    void* get_symbol(const char*) override
    {
        return reinterpret_cast<void*>(&truncated_init_v2);
    }
};

class TruncatedV2Loader : public qemu::LibraryLoaderIface
{
public:
    LibraryIfacePtr load_library(const std::string&, bool) override
    {
        return std::make_shared<TruncatedV2Library>();
    }

    const char* get_lib_ext() override { return ".so"; }
    const char* get_last_error() override { return ""; }
};

TEST(LibQemuV2Negotiation, RejectsTruncatedTimerExportsAtInit)
{
    TruncatedV2Loader loader;
    qemu::LibQemu instance(loader, "truncated-v2");
    instance.push_qemu_arg("qemu-system-aarch64");

    EXPECT_THROW(instance.init(), qemu::LibQemuException);
}

}

int sc_main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
