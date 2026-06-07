/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_CPU_RSE_MCUBOOT_IMAGE_H
#define _LIBQBOX_COMPONENTS_CPU_RSE_MCUBOOT_IMAGE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "rse_lms_accel.h"

namespace qbox {
namespace rse_mcuboot_image {

static constexpr uint32_t IMAGE_MAGIC = 0x96f3b83du;
static constexpr uint16_t IMAGE_TLV_INFO_MAGIC = 0x6907u;
static constexpr uint16_t IMAGE_TLV_PROT_INFO_MAGIC = 0x6908u;
static constexpr uint32_t IMAGE_F_ENCRYPTED_AES128 = 0x00000004u;
static constexpr uint32_t IMAGE_F_ENCRYPTED_AES256 = 0x00000008u;
static constexpr uint32_t IMAGE_F_RAM_LOAD = 0x00000020u;
static constexpr size_t IMAGE_HEADER_SIZE = 32;
static constexpr size_t IMAGE_TLV_INFO_SIZE = 4;
static constexpr size_t SHA256_SIZE = 32;

struct ImageHeader {
    uint32_t magic = 0;
    uint32_t load_addr = 0;
    uint16_t hdr_size = 0;
    uint16_t protect_tlv_size = 0;
    uint32_t img_size = 0;
    uint32_t flags = 0;
};

inline uint16_t load_le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t load_le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline bool parse_header(const uint8_t* data, size_t size, ImageHeader& header)
{
    if (data == nullptr || size < IMAGE_HEADER_SIZE) {
        return false;
    }

    header.magic = load_le32(data);
    header.load_addr = load_le32(data + 4);
    header.hdr_size = load_le16(data + 8);
    header.protect_tlv_size = load_le16(data + 10);
    header.img_size = load_le32(data + 12);
    header.flags = load_le32(data + 16);
    return header.magic == IMAGE_MAGIC &&
           header.hdr_size >= IMAGE_HEADER_SIZE;
}

inline bool checked_add(size_t lhs, size_t rhs, size_t& out)
{
    if (lhs > static_cast<size_t>(-1) - rhs) {
        return false;
    }
    out = lhs + rhs;
    return true;
}

inline bool hash_region_size(const ImageHeader& header, size_t& size)
{
    size_t total = 0;
    if (!checked_add(header.hdr_size, header.img_size, total)) {
        return false;
    }
    if (!checked_add(total, header.protect_tlv_size, total)) {
        return false;
    }
    size = total;
    return true;
}

inline bool parse_tlv_info(const uint8_t* data, size_t size,
                           uint16_t& magic, uint16_t& total_size)
{
    if (data == nullptr || size < IMAGE_TLV_INFO_SIZE) {
        return false;
    }

    magic = load_le16(data);
    total_size = load_le16(data + 2);
    return true;
}

inline std::array<uint8_t, SHA256_SIZE> sha256(
    const uint8_t* seed, size_t seed_size,
    const uint8_t* image, size_t image_size)
{
    qbox::rse_lms_accel::Sha256 sha;
    if (seed != nullptr && seed_size != 0) {
        sha.update(seed, seed_size);
    }
    sha.update(image, image_size);
    return sha.finish();
}

} // namespace rse_mcuboot_image
} // namespace qbox

#endif /* _LIBQBOX_COMPONENTS_CPU_RSE_MCUBOOT_IMAGE_H */
