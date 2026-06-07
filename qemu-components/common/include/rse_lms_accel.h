/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_CPU_RSE_LMS_ACCEL_H
#define _LIBQBOX_COMPONENTS_CPU_RSE_LMS_ACCEL_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace qbox {
namespace rse_lms_accel {

static constexpr uint32_t LMS_SHA256_M32_H10 = 0x00000006u;
static constexpr uint32_t LMOTS_SHA256_N32_W8 = 0x00000004u;
static constexpr size_t HASH_LEN = 32;
static constexpr size_t I_KEY_ID_LEN = 16;
static constexpr size_t Q_LEAF_ID_LEN = 4;
static constexpr size_t LMOTS_P = 34;
static constexpr size_t LMS_HEIGHT = 10;
static constexpr size_t LMS_PUBLIC_KEY_LEN = 4 + 4 + I_KEY_ID_LEN + HASH_LEN;
static constexpr size_t LMOTS_SIG_LEN = 4 + HASH_LEN + LMOTS_P * HASH_LEN;
static constexpr size_t LMS_SIG_LEN = Q_LEAF_ID_LEN + LMOTS_SIG_LEN + 4 + LMS_HEIGHT * HASH_LEN;

inline uint32_t load_be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

inline void store_be16(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value >> 8);
    p[1] = static_cast<uint8_t>(value);
}

inline void store_be32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

inline void store_be64(uint8_t* p, uint64_t value)
{
    for (int i = 7; i >= 0; --i) {
        p[7 - i] = static_cast<uint8_t>(value >> (i * 8));
    }
}

inline uint32_t rotr32(uint32_t value, unsigned int shift)
{
    return (value >> shift) | (value << (32 - shift));
}

class Sha256
{
public:
    Sha256() { reset(); }

    void reset()
    {
        m_h = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        };
        m_block.fill(0);
        m_bytes = 0;
        m_block_len = 0;
    }

    void update(const uint8_t* data, size_t len)
    {
        if (data == nullptr && len != 0) {
            return;
        }

        m_bytes += len;
        while (len != 0) {
            const size_t copy_len = std::min(len, m_block.size() - m_block_len);
            std::memcpy(m_block.data() + m_block_len, data, copy_len);
            m_block_len += copy_len;
            data += copy_len;
            len -= copy_len;

            if (m_block_len == m_block.size()) {
                transform(m_block.data());
                m_block_len = 0;
            }
        }
    }

    std::array<uint8_t, HASH_LEN> finish()
    {
        const uint64_t bit_len = m_bytes * 8u;
        uint8_t padding[64] = {};
        uint8_t len_bytes[8];
        const size_t pad_len = m_block_len < 56 ?
            56 - m_block_len :
            120 - m_block_len;

        padding[0] = 0x80;
        update(padding, pad_len);
        store_be64(len_bytes, bit_len);
        update(len_bytes, sizeof(len_bytes));

        std::array<uint8_t, HASH_LEN> out{};
        for (size_t i = 0; i < m_h.size(); ++i) {
            store_be32(out.data() + i * sizeof(uint32_t), m_h[i]);
        }
        return out;
    }

    static std::array<uint8_t, HASH_LEN> digest(const uint8_t* data, size_t len)
    {
        Sha256 sha;
        sha.update(data, len);
        return sha.finish();
    }

private:
    void transform(const uint8_t* block)
    {
        static const uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };
        uint32_t w[64];

        for (size_t i = 0; i < 16; ++i) {
            w[i] = load_be32(block + i * sizeof(uint32_t));
        }
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr32(w[i - 15], 7) ^
                                rotr32(w[i - 15], 18) ^
                                (w[i - 15] >> 3);
            const uint32_t s1 = rotr32(w[i - 2], 17) ^
                                rotr32(w[i - 2], 19) ^
                                (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = m_h[0];
        uint32_t b = m_h[1];
        uint32_t c = m_h[2];
        uint32_t d = m_h[3];
        uint32_t e = m_h[4];
        uint32_t f = m_h[5];
        uint32_t g = m_h[6];
        uint32_t h = m_h[7];

        for (size_t i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        m_h[0] += a;
        m_h[1] += b;
        m_h[2] += c;
        m_h[3] += d;
        m_h[4] += e;
        m_h[5] += f;
        m_h[6] += g;
        m_h[7] += h;
    }

    std::array<uint32_t, 8> m_h{};
    std::array<uint8_t, 64> m_block{};
    uint64_t m_bytes = 0;
    size_t m_block_len = 0;
};

inline std::array<uint8_t, HASH_LEN> sha256_concat(
    std::initializer_list<std::pair<const uint8_t*, size_t>> chunks)
{
    Sha256 sha;
    for (const auto& chunk : chunks) {
        sha.update(chunk.first, chunk.second);
    }
    return sha.finish();
}

inline bool lmots_candidate(const uint8_t* msg, size_t msg_size,
                            const uint8_t* sig, size_t sig_size,
                            const uint8_t* i_key_id,
                            const uint8_t* q_leaf_id,
                            std::array<uint8_t, HASH_LEN>& out)
{
    if (sig_size != LMOTS_SIG_LEN || load_be32(sig) != LMOTS_SHA256_N32_W8) {
        return false;
    }

    static const uint8_t d_message[2] = { 0x81, 0x81 };
    static const uint8_t d_public[2] = { 0x80, 0x80 };
    const uint8_t* c_random = sig + 4;
    const uint8_t* y = sig + 4 + HASH_LEN;

    auto q_hash = sha256_concat({
        { i_key_id, I_KEY_ID_LEN },
        { q_leaf_id, Q_LEAF_ID_LEN },
        { d_message, sizeof(d_message) },
        { c_random, HASH_LEN },
        { msg, msg_size },
    });

    uint32_t checksum = 0;
    for (const uint8_t digit : q_hash) {
        checksum += 0xffu - digit;
    }
    uint8_t digits[LMOTS_P];
    std::memcpy(digits, q_hash.data(), HASH_LEN);
    store_be16(digits + HASH_LEN, static_cast<uint16_t>(checksum));

    std::array<uint8_t, LMOTS_P * HASH_LEN> hashed_digits{};
    for (size_t i = 0; i < LMOTS_P; ++i) {
        std::array<uint8_t, HASH_LEN> tmp{};
        std::memcpy(tmp.data(), y + i * HASH_LEN, HASH_LEN);

        uint8_t i_bytes[2];
        store_be16(i_bytes, static_cast<uint16_t>(i));
        for (uint32_t j = digits[i]; j < 0xffu; ++j) {
            const uint8_t j_byte = static_cast<uint8_t>(j);
            tmp = sha256_concat({
                { i_key_id, I_KEY_ID_LEN },
                { q_leaf_id, Q_LEAF_ID_LEN },
                { i_bytes, sizeof(i_bytes) },
                { &j_byte, sizeof(j_byte) },
                { tmp.data(), tmp.size() },
            });
        }
        std::memcpy(hashed_digits.data() + i * HASH_LEN, tmp.data(), HASH_LEN);
    }

    out = sha256_concat({
        { i_key_id, I_KEY_ID_LEN },
        { q_leaf_id, Q_LEAF_ID_LEN },
        { d_public, sizeof(d_public) },
        { hashed_digits.data(), hashed_digits.size() },
    });
    return true;
}

inline bool verify(const uint8_t* public_key, size_t public_key_len,
                   const uint8_t* msg, size_t msg_size,
                   const uint8_t* sig, size_t sig_size)
{
    if (public_key == nullptr || (msg == nullptr && msg_size != 0) ||
        sig == nullptr) {
        return false;
    }
    if (public_key_len != LMS_PUBLIC_KEY_LEN || sig_size != LMS_SIG_LEN) {
        return false;
    }
    if (load_be32(public_key) != LMS_SHA256_M32_H10 ||
        load_be32(public_key + 4) != LMOTS_SHA256_N32_W8) {
        return false;
    }

    const uint8_t* i_key_id = public_key + 8;
    const uint8_t* public_root = public_key + 24;
    if (load_be32(sig + 4 + LMOTS_SIG_LEN) != LMS_SHA256_M32_H10) {
        return false;
    }

    const uint32_t q_leaf = load_be32(sig);
    if (q_leaf >= (1u << LMS_HEIGHT)) {
        return false;
    }

    uint8_t q_leaf_id[Q_LEAF_ID_LEN];
    store_be32(q_leaf_id, q_leaf);
    std::array<uint8_t, HASH_LEN> kc{};
    if (!lmots_candidate(msg, msg_size, sig + 4, LMOTS_SIG_LEN,
                         i_key_id, q_leaf_id, kc)) {
        return false;
    }

    static const uint8_t d_leaf[2] = { 0x82, 0x82 };
    static const uint8_t d_intr[2] = { 0x83, 0x83 };

    uint8_t node_id_bytes[4];
    uint32_t curr_node_id = (1u << LMS_HEIGHT) + q_leaf;
    store_be32(node_id_bytes, curr_node_id);
    std::array<uint8_t, HASH_LEN> node = sha256_concat({
        { i_key_id, I_KEY_ID_LEN },
        { node_id_bytes, sizeof(node_id_bytes) },
        { d_leaf, sizeof(d_leaf) },
        { kc.data(), kc.size() },
    });

    const uint8_t* path = sig + 4 + LMOTS_SIG_LEN + 4;
    for (size_t height = 0; height < LMS_HEIGHT; ++height) {
        const uint32_t child_node_id = curr_node_id;
        const uint8_t* sibling = path + height * HASH_LEN;
        curr_node_id /= 2;
        store_be32(node_id_bytes, curr_node_id);
        if (child_node_id & 1u) {
            node = sha256_concat({
                { i_key_id, I_KEY_ID_LEN },
                { node_id_bytes, sizeof(node_id_bytes) },
                { d_intr, sizeof(d_intr) },
                { sibling, HASH_LEN },
                { node.data(), node.size() },
            });
        } else {
            node = sha256_concat({
                { i_key_id, I_KEY_ID_LEN },
                { node_id_bytes, sizeof(node_id_bytes) },
                { d_intr, sizeof(d_intr) },
                { node.data(), node.size() },
                { sibling, HASH_LEN },
            });
        }
    }

    return std::memcmp(node.data(), public_root, HASH_LEN) == 0;
}

inline bool verify(const std::vector<uint8_t>& public_key,
                   const std::vector<uint8_t>& msg,
                   const std::vector<uint8_t>& sig)
{
    return verify(public_key.data(), public_key.size(),
                  msg.data(), msg.size(), sig.data(), sig.size());
}

} // namespace rse_lms_accel
} // namespace qbox

#endif
