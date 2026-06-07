/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <rse_mcuboot_image.h>

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

void store_le16(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

void store_le32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> synthetic_ram_load_image()
{
    std::vector<uint8_t> image(qbox::rse_mcuboot_image::IMAGE_HEADER_SIZE + 3);
    store_le32(image.data(), qbox::rse_mcuboot_image::IMAGE_MAGIC);
    store_le32(image.data() + 4, 0x31040000u);
    store_le16(image.data() + 8, qbox::rse_mcuboot_image::IMAGE_HEADER_SIZE);
    store_le16(image.data() + 10, 0);
    store_le32(image.data() + 12, 3);
    store_le32(image.data() + 16, 0x20);
    std::memcpy(image.data() + qbox::rse_mcuboot_image::IMAGE_HEADER_SIZE,
                "abc", 3);
    return image;
}

} // namespace

TEST(RseMcubootImageTest, ParsesImageHeaderAndHashRegionSize)
{
    const auto image = synthetic_ram_load_image();
    qbox::rse_mcuboot_image::ImageHeader header;
    size_t hash_size = 0;

    ASSERT_TRUE(qbox::rse_mcuboot_image::parse_header(
        image.data(), image.size(), header));
    EXPECT_EQ(header.load_addr, 0x31040000u);
    EXPECT_EQ(header.hdr_size, qbox::rse_mcuboot_image::IMAGE_HEADER_SIZE);
    EXPECT_EQ(header.img_size, 3u);
    EXPECT_TRUE(qbox::rse_mcuboot_image::hash_region_size(header, hash_size));
    EXPECT_EQ(hash_size, image.size());
}

TEST(RseMcubootImageTest, HashesImageRegionWithOptionalSeed)
{
    const auto image = synthetic_ram_load_image();
    const auto expected_no_seed = hex_bytes(
        "4487b4ab8db83e9193e204164516a97ed5fa0ac7ff409022"
        "1808d2fc35019ac8");
    const auto expected_seed = hex_bytes(
        "851f47738dc40aead4cca4540cd902a7306356831578ce53"
        "6cb67dffa9f0dedb");
    const uint8_t seed[] = { 's', 'e', 'e', 'd' };

    const auto no_seed = qbox::rse_mcuboot_image::sha256(
        nullptr, 0, image.data(), image.size());
    const auto with_seed = qbox::rse_mcuboot_image::sha256(
        seed, sizeof(seed), image.data(), image.size());

    EXPECT_EQ(std::vector<uint8_t>(no_seed.begin(), no_seed.end()),
              expected_no_seed);
    EXPECT_EQ(std::vector<uint8_t>(with_seed.begin(), with_seed.end()),
              expected_seed);
}

TEST(RseMcubootImageTest, RejectsInvalidMagic)
{
    auto image = synthetic_ram_load_image();
    image[0] ^= 0x01;
    qbox::rse_mcuboot_image::ImageHeader header;

    EXPECT_FALSE(qbox::rse_mcuboot_image::parse_header(
        image.data(), image.size(), header));
}
