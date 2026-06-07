/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <rse_p256_ecdsa.h>

namespace {

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

} // namespace

TEST(RseP256EcdsaTest, VerifiesKnownP256Sha256Signature)
{
    const auto public_key = hex_bytes(
        "3059301306072a8648ce3d020106082a8648ce3d03010703420004"
        "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a139"
        "45d898c2964fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b"
        "315ececbb6406837bf51f5");
    const auto hash = hex_bytes(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410"
        "ff61f20015ad");
    const auto signature = hex_bytes(
        "304402207bebda32fedbe57fda43854238a9f29b018cad94c6c3"
        "b70cde337e4b21235887022005a81918bb6e7d9e89d40fcf140b"
        "6d01f46f356a0f164c0e810acf3c785b806d");

    EXPECT_TRUE(qbox::rse_p256_ecdsa::verify(public_key, hash, signature));
}

TEST(RseP256EcdsaTest, RejectsModifiedSignature)
{
    const auto public_key = hex_bytes(
        "3059301306072a8648ce3d020106082a8648ce3d03010703420004"
        "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a139"
        "45d898c2964fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b"
        "315ececbb6406837bf51f5");
    const auto hash = hex_bytes(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410"
        "ff61f20015ad");
    auto signature = hex_bytes(
        "304402207bebda32fedbe57fda43854238a9f29b018cad94c6c3"
        "b70cde337e4b21235887022005a81918bb6e7d9e89d40fcf140b"
        "6d01f46f356a0f164c0e810acf3c785b806d");
    signature.back() ^= 0x01;

    EXPECT_FALSE(qbox::rse_p256_ecdsa::verify(public_key, hash, signature));
}

TEST(RseP256EcdsaTest, RejectsUnsupportedHashLength)
{
    const auto public_key = hex_bytes(
        "3059301306072a8648ce3d020106082a8648ce3d03010703420004"
        "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a139"
        "45d898c2964fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b"
        "315ececbb6406837bf51f5");
    auto hash = hex_bytes(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410"
        "ff61f20015ad");
    const auto signature = hex_bytes(
        "304402207bebda32fedbe57fda43854238a9f29b018cad94c6c3"
        "b70cde337e4b21235887022005a81918bb6e7d9e89d40fcf140b"
        "6d01f46f356a0f164c0e810acf3c785b806d");
    hash.pop_back();

    EXPECT_FALSE(qbox::rse_p256_ecdsa::verify(public_key, hash, signature));
}
