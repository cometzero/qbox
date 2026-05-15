/*
 * Apollo SMMU StreamID TLM extension.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_TLM_EXTENSIONS_APOLLO_SMMU_STREAM_ID_H
#define _LIBQBOX_TLM_EXTENSIONS_APOLLO_SMMU_STREAM_ID_H

#include <cstdint>

#include <tlm>

namespace gs {

class ApolloSmmuStreamIdExtension : public tlm::tlm_extension<ApolloSmmuStreamIdExtension>
{
public:
    uint32_t stream_id = 0;
    uint32_t substream_id = 0;
    bool substream_id_valid = false;
    bool privileged = false;
    bool instruction = false;
    bool translated = false;
    uint8_t security_state = 0;
    bool mpam_valid = false;
    bool mpam_remapped = false;
    bool mpam_unknown = false;
    uint8_t mpam_partid_space = 1;
    uint16_t mpam_partid = 0;
    uint8_t mpam_pmg = 0;
    bool output_attrs_valid = false;
    bool output_mtcfg = false;
    uint8_t output_mem_type = 0;
    uint8_t output_shareability = 0;
    uint8_t output_alloc_hint = 0;
    uint8_t output_inst_cfg = 0;
    uint8_t output_priv_cfg = 0;
    uint8_t output_ns_cfg = 0;

    ApolloSmmuStreamIdExtension() = default;
    explicit ApolloSmmuStreamIdExtension(uint32_t sid): stream_id(sid) {}
    ApolloSmmuStreamIdExtension(uint32_t sid, uint32_t ssid, bool ssid_valid,
                                bool privileged_access = false,
                                bool instruction_access = false,
                                bool translated_access = false,
                                uint8_t security = 0)
        : stream_id(sid)
        , substream_id(ssid)
        , substream_id_valid(ssid_valid)
        , privileged(privileged_access)
        , instruction(instruction_access)
        , translated(translated_access)
        , security_state(security)
    {
    }
    ApolloSmmuStreamIdExtension(const ApolloSmmuStreamIdExtension&) = default;

    tlm_extension_base* clone() const override { return new ApolloSmmuStreamIdExtension(*this); }

    void copy_from(const tlm_extension_base& ext) override
    {
        const auto& other = static_cast<const ApolloSmmuStreamIdExtension&>(ext);
        stream_id = other.stream_id;
        substream_id = other.substream_id;
        substream_id_valid = other.substream_id_valid;
        privileged = other.privileged;
        instruction = other.instruction;
        translated = other.translated;
        security_state = other.security_state;
        mpam_valid = other.mpam_valid;
        mpam_remapped = other.mpam_remapped;
        mpam_unknown = other.mpam_unknown;
        mpam_partid_space = other.mpam_partid_space;
        mpam_partid = other.mpam_partid;
        mpam_pmg = other.mpam_pmg;
        output_attrs_valid = other.output_attrs_valid;
        output_mtcfg = other.output_mtcfg;
        output_mem_type = other.output_mem_type;
        output_shareability = other.output_shareability;
        output_alloc_hint = other.output_alloc_hint;
        output_inst_cfg = other.output_inst_cfg;
        output_priv_cfg = other.output_priv_cfg;
        output_ns_cfg = other.output_ns_cfg;
    }
};

} // namespace gs

#endif
