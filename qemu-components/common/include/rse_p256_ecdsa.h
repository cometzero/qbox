/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_CPU_RSE_P256_ECDSA_H
#define _LIBQBOX_COMPONENTS_CPU_RSE_P256_ECDSA_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>

namespace qbox {
namespace rse_p256_ecdsa {

using boost::multiprecision::cpp_int;

struct Point {
    cpp_int x = 0;
    cpp_int y = 0;
    bool infinity = true;
};

inline cpp_int from_hex(const char* hex)
{
    cpp_int value = 0;
    for (const char* p = hex; *p != '\0'; ++p) {
        uint8_t digit = 0;
        if (*p >= '0' && *p <= '9') {
            digit = static_cast<uint8_t>(*p - '0');
        } else if (*p >= 'a' && *p <= 'f') {
            digit = static_cast<uint8_t>(10 + *p - 'a');
        } else if (*p >= 'A' && *p <= 'F') {
            digit = static_cast<uint8_t>(10 + *p - 'A');
        } else {
            continue;
        }
        value <<= 4;
        value += digit;
    }
    return value;
}

inline const cpp_int& p()
{
    static const cpp_int value = from_hex(
        "ffffffff00000001000000000000000000000000ffffffffffffffffffffffff");
    return value;
}

inline const cpp_int& n()
{
    static const cpp_int value = from_hex(
        "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551");
    return value;
}

inline const cpp_int& a()
{
    static const cpp_int value = p() - 3;
    return value;
}

inline const cpp_int& b()
{
    static const cpp_int value = from_hex(
        "5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b");
    return value;
}

inline const Point& generator()
{
    static const Point g = {
        from_hex("6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"),
        from_hex("4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5"),
        false,
    };
    return g;
}

inline cpp_int mod(cpp_int value, const cpp_int& modulus)
{
    value %= modulus;
    if (value < 0) {
        value += modulus;
    }
    return value;
}

inline cpp_int from_be(const uint8_t* data, size_t size)
{
    cpp_int value = 0;
    for (size_t i = 0; i < size; ++i) {
        value <<= 8;
        value += data[i];
    }
    return value;
}

inline cpp_int mod_inverse(cpp_int value, const cpp_int& modulus)
{
    value = mod(value, modulus);
    cpp_int t = 0;
    cpp_int new_t = 1;
    cpp_int r = modulus;
    cpp_int new_r = value;

    while (new_r != 0) {
        const cpp_int quotient = r / new_r;
        const cpp_int next_t = t - quotient * new_t;
        t = new_t;
        new_t = next_t;
        const cpp_int next_r = r - quotient * new_r;
        r = new_r;
        new_r = next_r;
    }

    if (r != 1) {
        return 0;
    }
    return mod(t, modulus);
}

inline bool is_on_curve(const Point& point)
{
    if (point.infinity) {
        return false;
    }

    const cpp_int lhs = mod(point.y * point.y, p());
    const cpp_int rhs = mod(point.x * point.x * point.x + a() * point.x + b(), p());
    return lhs == rhs;
}

inline Point point_double(const Point& point)
{
    if (point.infinity || point.y == 0) {
        return Point{};
    }

    const cpp_int lambda = mod(
        (3 * point.x * point.x + a()) * mod_inverse(2 * point.y, p()), p());
    Point out;
    out.infinity = false;
    out.x = mod(lambda * lambda - 2 * point.x, p());
    out.y = mod(lambda * (point.x - out.x) - point.y, p());
    return out;
}

inline Point point_add(const Point& lhs, const Point& rhs)
{
    if (lhs.infinity) {
        return rhs;
    }
    if (rhs.infinity) {
        return lhs;
    }
    if (lhs.x == rhs.x) {
        if (mod(lhs.y + rhs.y, p()) == 0) {
            return Point{};
        }
        return point_double(lhs);
    }

    const cpp_int lambda = mod(
        (rhs.y - lhs.y) * mod_inverse(rhs.x - lhs.x, p()), p());
    Point out;
    out.infinity = false;
    out.x = mod(lambda * lambda - lhs.x - rhs.x, p());
    out.y = mod(lambda * (lhs.x - out.x) - lhs.y, p());
    return out;
}

inline Point scalar_multiply(Point point, cpp_int scalar)
{
    Point result;
    scalar = mod(scalar, n());
    while (scalar > 0) {
        if ((scalar & 1) != 0) {
            result = point_add(result, point);
        }
        scalar >>= 1;
        if (scalar != 0) {
            point = point_double(point);
        }
    }
    return result;
}

inline bool parse_der_length(const uint8_t* data, size_t size,
                             size_t& offset, size_t& length)
{
    if (offset >= size) {
        return false;
    }
    const uint8_t first = data[offset++];
    if ((first & 0x80) == 0) {
        length = first;
        return offset + length <= size;
    }

    const size_t bytes = first & 0x7f;
    if (bytes == 0 || bytes > sizeof(size_t) || offset + bytes > size) {
        return false;
    }

    length = 0;
    for (size_t i = 0; i < bytes; ++i) {
        length = (length << 8) | data[offset++];
    }
    return offset + length <= size;
}

inline bool parse_der_integer(const uint8_t* data, size_t size,
                              size_t& offset, cpp_int& value)
{
    if (offset >= size || data[offset++] != 0x02) {
        return false;
    }

    size_t length = 0;
    if (!parse_der_length(data, size, offset, length) || length == 0) {
        return false;
    }
    if ((data[offset] & 0x80) != 0) {
        return false;
    }

    while (length > 1 && data[offset] == 0) {
        ++offset;
        --length;
    }

    value = from_be(data + offset, length);
    offset += length;
    return true;
}

inline bool parse_ecdsa_der_signature(const uint8_t* sig, size_t sig_size,
                                      cpp_int& r, cpp_int& s)
{
    if (sig == nullptr || sig_size == 0) {
        return false;
    }

    size_t offset = 0;
    if (sig[offset++] != 0x30) {
        return false;
    }
    size_t seq_length = 0;
    if (!parse_der_length(sig, sig_size, offset, seq_length)) {
        return false;
    }
    const size_t seq_end = offset + seq_length;
    if (seq_end != sig_size) {
        return false;
    }

    if (!parse_der_integer(sig, seq_end, offset, r) ||
        !parse_der_integer(sig, seq_end, offset, s) ||
        offset != seq_end) {
        return false;
    }

    return r > 0 && r < n() && s > 0 && s < n();
}

inline bool parse_public_key_point(const uint8_t* key, size_t key_size,
                                   Point& point)
{
    if (key == nullptr || key_size < 65) {
        return false;
    }

    if (key_size == 65 && key[0] == 0x04) {
        point.x = from_be(key + 1, 32);
        point.y = from_be(key + 33, 32);
        point.infinity = false;
        return is_on_curve(point);
    }

    for (size_t i = 0; i + 65 <= key_size; ++i) {
        if (key[i] != 0x04) {
            continue;
        }
        Point candidate;
        candidate.x = from_be(key + i + 1, 32);
        candidate.y = from_be(key + i + 33, 32);
        candidate.infinity = false;
        if (is_on_curve(candidate)) {
            point = candidate;
            return true;
        }
    }
    return false;
}

inline bool verify(const uint8_t* public_key, size_t public_key_size,
                   const uint8_t* hash, size_t hash_size,
                   const uint8_t* signature, size_t signature_size)
{
    if (hash == nullptr || hash_size != 32) {
        return false;
    }

    Point q;
    cpp_int r = 0;
    cpp_int s = 0;
    if (!parse_public_key_point(public_key, public_key_size, q) ||
        !parse_ecdsa_der_signature(signature, signature_size, r, s)) {
        return false;
    }

    const cpp_int e = from_be(hash, hash_size);
    const cpp_int w = mod_inverse(s, n());
    if (w == 0) {
        return false;
    }

    const cpp_int u1 = mod(e * w, n());
    const cpp_int u2 = mod(r * w, n());
    const Point point = point_add(
        scalar_multiply(generator(), u1),
        scalar_multiply(q, u2));
    if (point.infinity) {
        return false;
    }

    return mod(point.x, n()) == r;
}

inline bool verify(const std::vector<uint8_t>& public_key,
                   const std::vector<uint8_t>& hash,
                   const std::vector<uint8_t>& signature)
{
    return verify(public_key.data(), public_key.size(),
                  hash.data(), hash.size(),
                  signature.data(), signature.size());
}

} // namespace rse_p256_ecdsa
} // namespace qbox

#endif /* _LIBQBOX_COMPONENTS_CPU_RSE_P256_ECDSA_H */
