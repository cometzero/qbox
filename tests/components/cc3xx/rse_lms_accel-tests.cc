/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <array>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <rse_lms_accel.h>

namespace {

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> values)
{
    return std::vector<uint8_t>(values);
}

uint8_t hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<uint8_t>(10 + c - 'A');
    }
    throw std::runtime_error("invalid hex character");
}

std::vector<uint8_t> hex_bytes(const std::string& hex)
{
    if ((hex.size() % 2) != 0) {
        throw std::runtime_error("odd hex length");
    }

    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>((hex_nibble(hex[i]) << 4) |
                                           hex_nibble(hex[i + 1])));
    }
    return out;
}

std::string quoted_field(const std::string& line, size_t& pos)
{
    const size_t begin = line.find('"', pos);
    if (begin == std::string::npos) {
        throw std::runtime_error("missing field quote");
    }
    const size_t end = line.find('"', begin + 1);
    if (end == std::string::npos) {
        throw std::runtime_error("unterminated field quote");
    }
    pos = end + 1;
    return line.substr(begin + 1, end - begin - 1);
}

std::tuple<std::vector<uint8_t>, std::vector<uint8_t>, std::vector<uint8_t>>
load_first_success_vector(const std::string& path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open LMS test data");
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.find("lms_verify_test:") != 0 || line.rfind(":0") == std::string::npos) {
            continue;
        }

        size_t pos = 0;
        auto msg = hex_bytes(quoted_field(line, pos));
        auto sig = hex_bytes(quoted_field(line, pos));
        auto pub_key = hex_bytes(quoted_field(line, pos));
        return std::make_tuple(std::move(msg), std::move(sig), std::move(pub_key));
    }

    throw std::runtime_error("missing successful LMS vector");
}

} // namespace

TEST(RseLmsAccelTest, Sha256MatchesKnownVector)
{
    const char message[] = "abc";
    const auto digest = qbox::rse_lms_accel::Sha256::digest(
        reinterpret_cast<const uint8_t*>(message), 3);
    const std::array<uint8_t, 32> expected = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };

    EXPECT_EQ(digest, expected);
}

TEST(RseLmsAccelTest, RejectsUnsupportedLengths)
{
    const auto message = bytes({ 0x01, 0x02, 0x03 });
    std::vector<uint8_t> public_key(qbox::rse_lms_accel::LMS_PUBLIC_KEY_LEN, 0);
    std::vector<uint8_t> signature(qbox::rse_lms_accel::LMS_SIG_LEN, 0);

    qbox::rse_lms_accel::store_be32(
        public_key.data(), qbox::rse_lms_accel::LMS_SHA256_M32_H10);
    qbox::rse_lms_accel::store_be32(
        public_key.data() + 4, qbox::rse_lms_accel::LMOTS_SHA256_N32_W8);
    qbox::rse_lms_accel::store_be32(
        signature.data() + 4, qbox::rse_lms_accel::LMOTS_SHA256_N32_W8);
    qbox::rse_lms_accel::store_be32(
        signature.data() + 4 + qbox::rse_lms_accel::LMOTS_SIG_LEN,
        qbox::rse_lms_accel::LMS_SHA256_M32_H10);

    EXPECT_FALSE(qbox::rse_lms_accel::verify(
        public_key.data(), public_key.size() - 1,
        message.data(), message.size(),
        signature.data(), signature.size()));
    EXPECT_FALSE(qbox::rse_lms_accel::verify(
        public_key.data(), public_key.size(),
        message.data(), message.size(),
        signature.data(), signature.size() - 1));
}

TEST(RseLmsAccelTest, RejectsMismatchedSignature)
{
    const auto message = bytes({ 0x60, 0xda, 0x1a, 0x17 });
    std::vector<uint8_t> public_key(qbox::rse_lms_accel::LMS_PUBLIC_KEY_LEN, 0);
    std::vector<uint8_t> signature(qbox::rse_lms_accel::LMS_SIG_LEN, 0);

    qbox::rse_lms_accel::store_be32(
        public_key.data(), qbox::rse_lms_accel::LMS_SHA256_M32_H10);
    qbox::rse_lms_accel::store_be32(
        public_key.data() + 4, qbox::rse_lms_accel::LMOTS_SHA256_N32_W8);
    qbox::rse_lms_accel::store_be32(
        signature.data() + 4, qbox::rse_lms_accel::LMOTS_SHA256_N32_W8);
    qbox::rse_lms_accel::store_be32(
        signature.data() + 4 + qbox::rse_lms_accel::LMOTS_SIG_LEN,
        qbox::rse_lms_accel::LMS_SHA256_M32_H10);

    EXPECT_FALSE(qbox::rse_lms_accel::verify(public_key, message, signature));
}

TEST(RseLmsAccelTest, VerifiesMbedTlsInteropVectorWhenAvailable)
{
    const char* path = std::getenv("QBOX_RSE_LMS_TEST_DATA");
    if (path == nullptr || path[0] == '\0') {
        GTEST_SKIP() << "QBOX_RSE_LMS_TEST_DATA is not set";
    }

    std::vector<uint8_t> message;
    std::vector<uint8_t> signature;
    std::vector<uint8_t> public_key;
    std::tie(message, signature, public_key) = load_first_success_vector(path);

    ASSERT_EQ(public_key.size(), qbox::rse_lms_accel::LMS_PUBLIC_KEY_LEN);
    ASSERT_EQ(signature.size(), qbox::rse_lms_accel::LMS_SIG_LEN);
    EXPECT_TRUE(qbox::rse_lms_accel::verify(public_key, message, signature));
}
