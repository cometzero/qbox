/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef _LIBQBOX_TLM_EXTENSIONS_REQUEST_CONTEXT_H
#define _LIBQBOX_TLM_EXTENSIONS_REQUEST_CONTEXT_H

#include <cstdint>
#include <limits>

#include <tlm>

enum class RequestAccessPath : uint8_t {
    UNKNOWN,
    REGULAR,
    DEBUG,
    DIRECT,
    REENTRANT,
    DMI,
};

enum RequestContextCapability : uint32_t {
    REQUEST_CONTEXT_CAP_NONE = 0,
    REQUEST_CONTEXT_CAP_BOOT_LOADER = 1u << 0,
    REQUEST_CONTEXT_CAP_AUTHENTICATED_IMAGE = 1u << 1,
    REQUEST_CONTEXT_CAP_DEBUGGER = 1u << 2,
};

struct RequestContext {
    uint64_t origin_id = std::numeric_limits<uint64_t>::max();
    uint32_t domain_id = std::numeric_limits<uint32_t>::max();
    uint32_t requester_id = std::numeric_limits<uint32_t>::max();
    uint32_t substream_id = std::numeric_limits<uint32_t>::max();
    uint32_t capabilities = REQUEST_CONTEXT_CAP_NONE;
    bool origin_valid = false;
    bool domain_valid = false;
    bool requester_valid = false;
    bool substream_valid = false;
    bool secure = false;
    bool secure_valid = false;
    bool privileged = false;
    bool privileged_valid = false;
    bool instruction = false;
    bool instruction_valid = false;
    bool ats = false;
    bool ats_valid = false;
    RequestAccessPath access_path = RequestAccessPath::UNKNOWN;
};

inline RequestContext make_request_context(uint64_t origin_id, uint32_t domain_id,
                                           uint32_t requester_id, uint32_t substream_id,
                                           uint32_t capabilities = REQUEST_CONTEXT_CAP_NONE)
{
    RequestContext context;
    context.origin_id = origin_id;
    context.domain_id = domain_id;
    context.requester_id = requester_id;
    context.substream_id = substream_id;
    context.capabilities = capabilities;
    context.origin_valid = origin_id != std::numeric_limits<uint64_t>::max();
    context.domain_valid = domain_id != std::numeric_limits<uint32_t>::max();
    context.requester_valid = requester_id != std::numeric_limits<uint32_t>::max();
    context.substream_valid = substream_id != std::numeric_limits<uint32_t>::max();
    return context;
}

inline RequestContext normalize_qemu_request_context(const RequestContext& base,
                                                     bool secure,
                                                     bool user,
                                                     RequestAccessPath access_path)
{
    RequestContext context = base;
    if (!context.secure_valid) {
        context.secure = secure;
        context.secure_valid = true;
    }
    if (!context.privileged_valid) {
        context.privileged = !user;
        context.privileged_valid = true;
    }
    context.access_path = access_path;
    return context;
}

class RequestContextTlmExtension : public tlm::tlm_extension<RequestContextTlmExtension>
{
private:
    RequestContext m_context;

public:
    RequestContextTlmExtension() = default;
    RequestContextTlmExtension(const RequestContextTlmExtension&) = default;
    explicit RequestContextTlmExtension(const RequestContext& context): m_context(context) {}

    tlm_extension_base* clone() const override { return new RequestContextTlmExtension(*this); }

    void copy_from(tlm_extension_base const& ext) override
    {
        m_context = static_cast<const RequestContextTlmExtension&>(ext).m_context;
    }

    const RequestContext& get_context() const { return m_context; }
    RequestContext& get_context() { return m_context; }
    void set_context(const RequestContext& context) { m_context = context; }
    void set_access_path(RequestAccessPath access_path) { m_context.access_path = access_path; }
};

#endif
