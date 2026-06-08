/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>

#include <tlm>

namespace qbox {
namespace mmu720ae {

class request_attrs_extension
    : public tlm::tlm_extension<request_attrs_extension>
{
public:
    uint32_t sid = 0;
    uint32_t ssid = 0;
    bool sid_valid = false;
    bool ssid_valid = false;
    bool secure = false;
    bool privileged = false;
    bool instruction = false;
    bool ats_request = false;

    request_attrs_extension() = default;
    request_attrs_extension(const request_attrs_extension&) = default;

    tlm_extension_base* clone() const override
    {
        return new request_attrs_extension(*this);
    }

    void copy_from(const tlm_extension_base& ext) override
    {
        *this = static_cast<const request_attrs_extension&>(ext);
    }
};

} // namespace mmu720ae
} // namespace qbox
