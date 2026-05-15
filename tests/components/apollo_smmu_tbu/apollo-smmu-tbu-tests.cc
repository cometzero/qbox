/*
 * Apollo SMMUv3 TBU compliance-slice tests.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <libgsutils.h>

#define APOLLO_SMMU_TBU_TESTING
#include <apollo_smmu_tbu.h>
#undef APOLLO_SMMU_TBU_TESTING

#include <tests/initiator-tester.h>
#include <tests/target-tester.h>
#include <tests/test-bench.h>
#include <ports/target-signal-socket.h>

class ApolloSmmuTbuTestBench : public TestBench
{
protected:
    static constexpr uint64_t MEM_SIZE = 0x200000;
    static constexpr uint32_t ARCH_CR0_ALL_QUEUES =
        apollo_smmu_tbu::ARCH_CR0_SMMUEN |
        apollo_smmu_tbu::ARCH_CR0_PRIQEN |
        apollo_smmu_tbu::ARCH_CR0_EVENTQEN |
        apollo_smmu_tbu::ARCH_CR0_CMDQEN |
        apollo_smmu_tbu::ARCH_CR0_ATSCHK;

    apollo_smmu_tbu m_tbu;
    InitiatorTester m_upstream;
    InitiatorTester m_regs;
    TargetTester m_memory;
    sc_core::sc_vector<TargetSignalSocket<bool>> m_irq_lines;
    std::vector<uint8_t> m_memory_bytes;
    uint32_t m_async_stream_id = 0;
    uint64_t m_async_iova = 0;
    uint32_t m_async_payload = 0;
    tlm::tlm_response_status m_async_status = tlm::TLM_INCOMPLETE_RESPONSE;
    bool m_async_done = false;

    static uint64_t smmu_reg(uint64_t off)
    {
        return apollo_smmu_tbu::REG_SMMUV3_BASE + off;
    }

    static uint64_t smmu_s_reg(uint64_t off)
    {
        return smmu_reg(apollo_smmu_tbu::SMMUV3_SECURE_PAGE + off);
    }

    static uint64_t smmu_vatos_reg(uint64_t off)
    {
        return smmu_reg(apollo_smmu_tbu::SMMUV3_VATOS_PAGE + off);
    }

    static uint64_t atos_addr(uint64_t addr, uint32_t type, bool read = true,
                              bool privileged = false, bool instruction = false)
    {
        return (addr & apollo_smmu_tbu::ARCH_ATOS_ADDR_ADDR_MASK) |
               (static_cast<uint64_t>(type & apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_MASK)
                << apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_SHIFT) |
               (read ? apollo_smmu_tbu::ARCH_ATOS_ADDR_RNW : 0) |
               (privileged ? apollo_smmu_tbu::ARCH_ATOS_ADDR_PNU : 0) |
               (instruction ? apollo_smmu_tbu::ARCH_ATOS_ADDR_IND : 0);
    }

    tlm::tlm_response_status mem_read(uint64_t addr, uint8_t* data, size_t len)
    {
        if (addr > m_memory_bytes.size() || len > m_memory_bytes.size() - addr) {
            return tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }

        std::memcpy(data, &m_memory_bytes[addr], len);
        return tlm::TLM_OK_RESPONSE;
    }

    tlm::tlm_response_status mem_write(uint64_t addr, uint8_t* data, size_t len)
    {
        if (addr > m_memory_bytes.size() || len > m_memory_bytes.size() - addr) {
            return tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }

        std::memcpy(&m_memory_bytes[addr], data, len);
        return tlm::TLM_OK_RESPONSE;
    }

    void reg_write32(uint64_t addr, uint32_t value)
    {
        const auto status = m_regs.do_write_with_ptr(addr, reinterpret_cast<const uint8_t*>(&value), sizeof(value));

        ASSERT_EQ(tlm::TLM_OK_RESPONSE, status);
    }

    uint32_t reg_read32(uint64_t addr)
    {
        uint32_t value = 0;
        const auto status = m_regs.do_read_with_ptr(addr, reinterpret_cast<uint8_t*>(&value), sizeof(value));

        EXPECT_EQ(tlm::TLM_OK_RESPONSE, status);
        return value;
    }

    uint32_t reg_read_gerror_active()
    {
        return (reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR)) ^
                reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN))) &
               apollo_smmu_tbu::ARCH_GERROR_KNOWN_MASK;
    }

    uint32_t reg_read_secure_gerror_active()
    {
        return (reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERROR)) ^
                reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERRORN))) &
               apollo_smmu_tbu::ARCH_GERROR_KNOWN_MASK;
    }

    void store_u64(uint64_t addr, uint64_t value)
    {
        ASSERT_LE(addr, static_cast<uint64_t>(m_memory_bytes.size()));
        ASSERT_LE(sizeof(value), static_cast<uint64_t>(m_memory_bytes.size()) - addr);
        std::memcpy(&m_memory_bytes[addr], &value, sizeof(value));
    }

    uint64_t load_u64(uint64_t addr)
    {
        uint64_t value = 0;

        EXPECT_LE(addr, static_cast<uint64_t>(m_memory_bytes.size()));
        EXPECT_LE(sizeof(value), static_cast<uint64_t>(m_memory_bytes.size()) - addr);
        std::memcpy(&value, &m_memory_bytes[addr], sizeof(value));
        return value;
    }

    uint32_t load_u32(uint64_t addr)
    {
        uint32_t value = 0;

        EXPECT_LE(addr, static_cast<uint64_t>(m_memory_bytes.size()));
        EXPECT_LE(sizeof(value), static_cast<uint64_t>(m_memory_bytes.size()) - addr);
        std::memcpy(&value, &m_memory_bytes[addr], sizeof(value));
        return value;
    }

    static uint64_t arch_level_index_cfg(uint64_t iova, uint32_t granule, uint32_t levels,
                                         uint32_t level)
    {
        const uint32_t index_bits = apollo_smmu_tbu::arch_index_bits(granule);
        const uint32_t shift = apollo_smmu_tbu::arch_page_shift(granule) +
                               (levels - level - 1) * index_bits;

        return (iova >> shift) & ((1ULL << index_bits) - 1);
    }

    void stage_translation_tables_cfg(uint64_t ttbr, uint64_t iova, uint64_t output_base,
                                      uint32_t granule, uint32_t start_level,
                                      bool block_leaf = false, uint64_t leaf_attrs = 0)
    {
        const uint32_t levels = apollo_smmu_tbu::arch_walk_levels(granule);
        const uint64_t table_size = apollo_smmu_tbu::arch_granule_size(granule);
        uint64_t table = ttbr;

        ASSERT_LT(start_level, levels);
        for (uint32_t level = start_level; level < levels; level++) {
            const uint64_t index = arch_level_index_cfg(iova, granule, levels, level);
            const bool leaf = level + 1 == levels || (block_leaf && level + 2 == levels);
            const uint64_t desc_pa = table + index * sizeof(uint64_t);

            if (leaf) {
                const uint64_t offset_mask =
                    apollo_smmu_tbu::arch_level_offset_mask(
                        apollo_smmu_tbu::arch_walk_config {granule, start_level, levels,
                                                            apollo_smmu_tbu::ARCH_FAULT_STAGE_S1},
                        level);
                const uint64_t output_mask = 0x0000ffffffffffffULL & ~offset_mask;
                const uint64_t type = (level + 1 == levels) ? apollo_smmu_tbu::ARCH_DESC_PAGE :
                                                              apollo_smmu_tbu::ARCH_DESC_BLOCK;

                store_u64(desc_pa, (output_base & output_mask) | leaf_attrs |
                                       apollo_smmu_tbu::ARCH_DESC_AF | type);
                return;
            }

            const uint64_t next_table = table + table_size;
            store_u64(desc_pa, (next_table & apollo_smmu_tbu::arch_output_mask(granule)) |
                                  apollo_smmu_tbu::ARCH_DESC_TABLE);
            table = next_table;
        }
    }

    void stage_translation_tables(uint64_t ttbr, uint64_t iova, uint64_t output_base)
    {
        const uint64_t l1_table = ttbr + apollo_smmu_tbu::PAGE_SIZE;
        const uint64_t l2_table = ttbr + 2 * apollo_smmu_tbu::PAGE_SIZE;
        const uint64_t l3_table = ttbr + 3 * apollo_smmu_tbu::PAGE_SIZE;
        const uint64_t l0_index = apollo_smmu_tbu::arch_level_index(iova, 0);
        const uint64_t l1_index = apollo_smmu_tbu::arch_level_index(iova, 1);
        const uint64_t l2_index = apollo_smmu_tbu::arch_level_index(iova, 2);
        const uint64_t l3_index = apollo_smmu_tbu::arch_level_index(iova, 3);

        store_u64(ttbr + l0_index * sizeof(uint64_t),
                  (l1_table & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                      apollo_smmu_tbu::ARCH_DESC_TABLE);
        store_u64(l1_table + l1_index * sizeof(uint64_t),
                  (l2_table & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                      apollo_smmu_tbu::ARCH_DESC_TABLE);
        store_u64(l2_table + l2_index * sizeof(uint64_t),
                  (l3_table & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                      apollo_smmu_tbu::ARCH_DESC_TABLE);
        store_u64(l3_table + l3_index * sizeof(uint64_t),
                  (output_base & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                      apollo_smmu_tbu::ARCH_DESC_AF |
                      apollo_smmu_tbu::ARCH_DESC_PAGE);
    }

    static uint64_t arch_s1_ste(uint64_t cd_base)
    {
        return apollo_smmu_tbu::ARCH_STE_VALID |
               (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_S1_TRANS)
                << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT) |
               (cd_base & apollo_smmu_tbu::ARCH_STE_S1CTXPTR_MASK);
    }

    static uint64_t arch_ste_eats(uint32_t eats)
    {
        return static_cast<uint64_t>(eats & apollo_smmu_tbu::ARCH_STE_EATS_MASK)
               << apollo_smmu_tbu::ARCH_STE_EATS_SHIFT;
    }

    static uint64_t arch_ste_output_attrs(bool mtcfg, uint32_t memattr,
                                          uint32_t shcfg, uint32_t alloccfg,
                                          uint32_t instcfg, uint32_t privcfg,
                                          uint32_t nscfg)
    {
        return (mtcfg ? apollo_smmu_tbu::ARCH_STE_MTCFG : 0) |
               (static_cast<uint64_t>(memattr & apollo_smmu_tbu::ARCH_STE_MEMATTR_MASK)
                << apollo_smmu_tbu::ARCH_STE_MEMATTR_SHIFT) |
               (static_cast<uint64_t>(shcfg & apollo_smmu_tbu::ARCH_STE_SHCFG_MASK)
                << apollo_smmu_tbu::ARCH_STE_SHCFG_SHIFT) |
               (static_cast<uint64_t>(alloccfg & apollo_smmu_tbu::ARCH_STE_ALLOCCFG_MASK)
                << apollo_smmu_tbu::ARCH_STE_ALLOCCFG_SHIFT) |
               (static_cast<uint64_t>(instcfg & apollo_smmu_tbu::ARCH_STE_INSTCFG_MASK)
                << apollo_smmu_tbu::ARCH_STE_INSTCFG_SHIFT) |
               (static_cast<uint64_t>(privcfg & apollo_smmu_tbu::ARCH_STE_PRIVCFG_MASK)
                << apollo_smmu_tbu::ARCH_STE_PRIVCFG_SHIFT) |
               (static_cast<uint64_t>(nscfg & apollo_smmu_tbu::ARCH_STE_NSCFG_MASK)
                << apollo_smmu_tbu::ARCH_STE_NSCFG_SHIFT);
    }

    static uint64_t arch_valid_cd()
    {
        return apollo_smmu_tbu::ARCH_CD_VALID_ARCHITECTED;
    }

    static uint64_t arch_valid_cd(uint32_t granule, uint32_t start_level = 0)
    {
        return arch_valid_cd() |
               (static_cast<uint64_t>(granule) << apollo_smmu_tbu::ARCH_GRANULE_SHIFT) |
               (static_cast<uint64_t>(start_level) << apollo_smmu_tbu::ARCH_START_LEVEL_SHIFT);
    }

    static uint64_t arch_s2_ste(uint64_t, uint32_t granule = apollo_smmu_tbu::ARCH_GRANULE_4K,
                                uint32_t start_level = 0)
    {
        return apollo_smmu_tbu::ARCH_STE_VALID |
               (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_S2_TRANS)
                << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT) |
               (static_cast<uint64_t>(granule) << apollo_smmu_tbu::ARCH_GRANULE_SHIFT) |
               (static_cast<uint64_t>(start_level) << apollo_smmu_tbu::ARCH_START_LEVEL_SHIFT);
    }

    static uint64_t arch_nested_ste(uint32_t granule = apollo_smmu_tbu::ARCH_GRANULE_4K,
                                    uint32_t start_level = 0)
    {
        return apollo_smmu_tbu::ARCH_STE_VALID |
               (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_NESTED)
                << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT) |
               (static_cast<uint64_t>(granule) << apollo_smmu_tbu::ARCH_GRANULE_SHIFT) |
               (static_cast<uint64_t>(start_level) << apollo_smmu_tbu::ARCH_START_LEVEL_SHIFT);
    }

    void reg_write64(uint64_t lo_off, uint64_t hi_off, uint64_t value)
    {
        reg_write32(smmu_reg(lo_off), static_cast<uint32_t>(value));
        reg_write32(smmu_reg(hi_off), static_cast<uint32_t>(value >> 32));
    }

    void reg_s_write64(uint64_t lo_off, uint64_t hi_off, uint64_t value)
    {
        reg_write32(smmu_s_reg(lo_off), static_cast<uint32_t>(value));
        reg_write32(smmu_s_reg(hi_off), static_cast<uint32_t>(value >> 32));
    }

    void tbu_write64(uint64_t lo_off, uint64_t hi_off, uint64_t value)
    {
        reg_write32(lo_off, static_cast<uint32_t>(value));
        reg_write32(hi_off, static_cast<uint32_t>(value >> 32));
    }

    void add_map(uint32_t stream_id, uint64_t iova, uint64_t pa, uint64_t size)
    {
        reg_write32(apollo_smmu_tbu::REG_MAP_STREAM_ID, stream_id);
        tbu_write64(apollo_smmu_tbu::REG_MAP_IOVA_LO, apollo_smmu_tbu::REG_MAP_IOVA_HI, iova);
        tbu_write64(apollo_smmu_tbu::REG_MAP_PA_LO, apollo_smmu_tbu::REG_MAP_PA_HI, pa);
        tbu_write64(apollo_smmu_tbu::REG_MAP_SIZE_LO, apollo_smmu_tbu::REG_MAP_SIZE_HI, size);
        reg_write32(apollo_smmu_tbu::REG_MAP_CTRL, apollo_smmu_tbu::MAP_CTRL_ADD);
        EXPECT_EQ(apollo_smmu_tbu::MAP_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_MAP_STATUS));
    }

    void remove_map(uint32_t stream_id, uint64_t iova)
    {
        reg_write32(apollo_smmu_tbu::REG_MAP_STREAM_ID, stream_id);
        tbu_write64(apollo_smmu_tbu::REG_MAP_IOVA_LO, apollo_smmu_tbu::REG_MAP_IOVA_HI, iova);
        reg_write32(apollo_smmu_tbu::REG_MAP_CTRL, apollo_smmu_tbu::MAP_CTRL_REMOVE);
        EXPECT_EQ(apollo_smmu_tbu::MAP_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_MAP_STATUS));
    }

    tlm::tlm_response_status stream_read32(uint32_t stream_id, uint64_t iova, uint32_t& value)
    {
        return stream_read32(stream_id, false, 0, iova, value);
    }

    tlm::tlm_response_status stream_read32(uint32_t stream_id, bool ssid_valid,
                                           uint32_t ssid, uint64_t iova, uint32_t& value)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        gs::ApolloSmmuStreamIdExtension stream_id_ext(stream_id, ssid, ssid_valid);

        value = 0;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(iova);
        trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        trans.set_extension(&stream_id_ext);
        m_tbu.b_transport(trans, delay);
        trans.clear_extension(&stream_id_ext);
        return trans.get_response_status();
    }

    tlm::tlm_response_status stream_read32_access(uint32_t stream_id, uint64_t iova,
                                                  bool privileged, uint32_t& value,
                                                  bool instruction = false)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        gs::ApolloSmmuStreamIdExtension stream_id_ext(stream_id, 0, false,
                                                      privileged, instruction, false);

        value = 0;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(iova);
        trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        trans.set_extension(&stream_id_ext);
        m_tbu.b_transport(trans, delay);
        trans.clear_extension(&stream_id_ext);
        return trans.get_response_status();
    }

    tlm::tlm_response_status stream_read32_security(uint32_t stream_id,
                                                    uint8_t security_state,
                                                    uint64_t iova, uint32_t& value)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        gs::ApolloSmmuStreamIdExtension stream_id_ext(stream_id);

        stream_id_ext.security_state = security_state;
        value = 0;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(iova);
        trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        trans.set_extension(&stream_id_ext);
        m_tbu.b_transport(trans, delay);
        trans.clear_extension(&stream_id_ext);
        return trans.get_response_status();
    }

    tlm::tlm_response_status stream_write32(uint32_t stream_id, uint64_t iova, uint32_t value)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        gs::ApolloSmmuStreamIdExtension stream_id_ext(stream_id);

        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(iova);
        trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        trans.set_extension(&stream_id_ext);
        m_tbu.b_transport(trans, delay);
        trans.clear_extension(&stream_id_ext);
        return trans.get_response_status();
    }

    tlm::tlm_response_status translated_stream_read32(uint32_t stream_id, uint64_t iova,
                                                      uint32_t& value)
    {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        gs::ApolloSmmuStreamIdExtension stream_id_ext(stream_id, 0, false,
                                                      false, false, true);

        value = 0;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(iova);
        trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        trans.set_extension(&stream_id_ext);
        m_tbu.b_transport(trans, delay);
        trans.clear_extension(&stream_id_ext);
        return trans.get_response_status();
    }

public:
    void endpoint_blocking_write_body()
    {
        m_async_status = stream_write32(m_async_stream_id, m_async_iova, m_async_payload);
        m_async_done = true;
    }

protected:

    unsigned int stream_dbg_read32(uint32_t stream_id, uint64_t iova, uint32_t& value)
    {
        tlm::tlm_generic_payload trans;

        value = 0;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(iova);
        trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        trans.set_extension(new gs::ApolloSmmuStreamIdExtension(stream_id));
        const unsigned int bytes = m_tbu.transport_dbg(trans);
        return bytes;
    }

    unsigned int stream_dbg_read32_security(uint32_t stream_id, uint8_t security_state,
                                            uint64_t iova, uint32_t& value)
    {
        tlm::tlm_generic_payload trans;
        auto* stream_id_ext = new gs::ApolloSmmuStreamIdExtension(stream_id);

        stream_id_ext->security_state = security_state;
        value = 0;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(iova);
        trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        trans.set_extension(stream_id_ext);
        const unsigned int bytes = m_tbu.transport_dbg(trans);
        return bytes;
    }

    bool irq_line(size_t index)
    {
        return m_irq_lines[index].read();
    }

public:
    explicit ApolloSmmuTbuTestBench(const sc_core::sc_module_name& n)
        : TestBench(n)
        , m_tbu("tbu")
        , m_upstream("upstream")
        , m_regs("regs")
        , m_memory("memory", MEM_SIZE)
        , m_irq_lines("irq_lines", 4, [](const char* n, size_t) { return new TargetSignalSocket<bool>(n); })
        , m_memory_bytes(MEM_SIZE, 0)
    {
        m_tbu.m_arch_cr0 = ARCH_CR0_ALL_QUEUES;
        m_tbu.m_arch_cr0ack = ARCH_CR0_ALL_QUEUES;
        m_upstream.socket.bind(m_tbu.upstream);
        m_regs.socket.bind(m_tbu.regs);
        m_tbu.downstream.bind(m_memory.socket);
        for (size_t i = 0; i < m_irq_lines.size(); i++) {
            m_tbu.irq_out[i].bind(m_irq_lines[i]);
        }
        m_memory.register_read_cb([this](uint64_t addr, uint8_t* data, size_t len) {
            return mem_read(addr, data, len);
        });
        m_memory.register_write_cb([this](uint64_t addr, uint8_t* data, size_t len) {
            return mem_write(addr, data, len);
        });
        m_memory.register_debug_read_cb([this](uint64_t addr, uint8_t* data, size_t len) {
            return mem_read(addr, data, len) == tlm::TLM_OK_RESPONSE ? static_cast<int>(len) : 0;
        });
        m_memory.register_debug_write_cb([this](uint64_t addr, uint8_t* data, size_t len) {
            return mem_write(addr, data, len) == tlm::TLM_OK_RESPONSE ? static_cast<int>(len) : 0;
        });
    }
};

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedCoreOwnsCanonicalTranslationState)
{
    const auto& ownership = m_tbu.m_arch_core.ownership();
    const auto& state = m_tbu.m_arch_core.state_ownership();

    EXPECT_EQ(apollo::smmuv3::apollo_smmu_arch_core::translation_owner::apollo_systemc,
              ownership.owner);
    EXPECT_TRUE(m_tbu.m_arch_core.owns_translation_state());
    EXPECT_TRUE(m_tbu.m_arch_core.preserves_compatibility_adapter());
    EXPECT_FALSE(ownership.qemu_translation_bridge_enabled);
    EXPECT_TRUE(state.register_queue_state);
    EXPECT_TRUE(state.stream_context_descriptor_state);
    EXPECT_TRUE(state.page_table_walker_state);
    EXPECT_TRUE(state.fault_replay_state);
    EXPECT_TRUE(m_tbu.m_arch_core.owns_register_queue_state());
    EXPECT_TRUE(m_tbu.m_arch_core.owns_stream_context_descriptor_state());
    EXPECT_TRUE(m_tbu.m_arch_core.owns_page_table_walker_state());
    EXPECT_TRUE(m_tbu.m_arch_core.owns_fault_replay_state());
    EXPECT_STREQ("apollo_smmu_tbu/SystemC",
                 apollo::smmuv3::apollo_smmu_arch_core::canonical_owner_name());
    EXPECT_EQ(0x0000u, apollo::smmuv3::apollo_smmu_arch_core::COMPATIBILITY_APERTURE_BASE);
    EXPECT_EQ(apollo_smmu_tbu::REG_SMMUV3_BASE,
              apollo::smmuv3::apollo_smmu_arch_core::SMMUV3_APERTURE_BASE);
    EXPECT_EQ(apollo::smmuv3::apollo_smmu_arch_core::register_aperture::compatibility,
              m_tbu.m_arch_core.classify_register(apollo_smmu_tbu::REG_FEATURES));
    EXPECT_EQ(apollo::smmuv3::apollo_smmu_arch_core::register_aperture::smmuv3,
              m_tbu.m_arch_core.classify_register(
                  apollo_smmu_tbu::REG_SMMUV3_BASE + apollo_smmu_tbu::SMMUV3_IDR0));
    EXPECT_EQ(8u, apollo::smmuv3::apollo_smmu_arch_core::queue_entries(0x00012003));
    EXPECT_EQ(0u, apollo::smmuv3::apollo_smmu_arch_core::queue_entries(0x00012000));
    EXPECT_EQ(0x00012000u,
              apollo::smmuv3::apollo_smmu_arch_core::queue_base_addr(0x00012003));
    EXPECT_EQ(2u, apollo::smmuv3::apollo_smmu_arch_core::queue_index(10, 8));
    EXPECT_EQ(0u, apollo::smmuv3::apollo_smmu_arch_core::queue_index(10, 0));
    EXPECT_EQ(12u, apollo::smmuv3::apollo_smmu_arch_core::walker_page_shift(
                        apollo_smmu_tbu::ARCH_GRANULE_4K));
    EXPECT_EQ(14u, apollo::smmuv3::apollo_smmu_arch_core::walker_page_shift(
                        apollo_smmu_tbu::ARCH_GRANULE_16K));
    EXPECT_EQ(16u, apollo::smmuv3::apollo_smmu_arch_core::walker_page_shift(
                        apollo_smmu_tbu::ARCH_GRANULE_64K));
    EXPECT_TRUE(apollo::smmuv3::apollo_smmu_arch_core::walker_granule_supported(
        apollo_smmu_tbu::ARCH_GRANULE_4K));
    EXPECT_FALSE(apollo::smmuv3::apollo_smmu_arch_core::walker_granule_supported(3));
    EXPECT_EQ(3u, apollo::smmuv3::apollo_smmu_arch_core::walker_levels(
                      apollo_smmu_tbu::ARCH_GRANULE_64K));
    EXPECT_EQ(0x80u, apollo::smmuv3::apollo_smmu_arch_core::walker_level_index(
                       0x10000000, apollo_smmu_tbu::ARCH_GRANULE_4K,
                       apollo_smmu_tbu::ARCH_LEVELS, 2));
    EXPECT_EQ(0xfffu,
              apollo::smmuv3::apollo_smmu_arch_core::walker_level_offset_mask(
                  apollo_smmu_tbu::ARCH_GRANULE_4K, apollo_smmu_tbu::ARCH_LEVELS, 3));
    const apollo::smmuv3::apollo_smmu_arch_core::descriptor_walk_config core_walk {
        apollo_smmu_tbu::ARCH_GRANULE_4K,
        0,
        apollo_smmu_tbu::ARCH_LEVELS,
        apollo_smmu_tbu::ARCH_FAULT_STAGE_S1,
    };
    EXPECT_TRUE(m_tbu.m_arch_core.begin_descriptor_walk(0x10000000, 0x8000,
                                                        core_walk));
    const auto core_fetch =
        m_tbu.m_arch_core.descriptor_fetch_address(0x8000, 0x10000000,
                                                   core_walk, 2);
    EXPECT_EQ(0x80u, core_fetch.index);
    EXPECT_EQ(0x8400u, core_fetch.desc_pa);
    const auto core_fetch_lifecycle =
        m_tbu.m_arch_core.begin_descriptor_fetch(0x8000, 0x10000000,
                                                 core_walk, 2);
    EXPECT_EQ(0x8400u, core_fetch_lifecycle.desc_pa);
    EXPECT_EQ(0x8400u, m_tbu.m_arch_core.walker_state().last_fetch_addr);
    m_tbu.m_arch_core.complete_descriptor_fetch(core_walk.stage,
                                                core_fetch_lifecycle.desc_pa,
                                                0x9003);
    EXPECT_EQ(0x9003u, m_tbu.m_arch_core.walker_state().last_desc);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S1,
              m_tbu.m_arch_core.fault_replay_state_storage().fault_stage);
    m_tbu.m_arch_core.fail_descriptor_fetch(core_walk.stage, 0xdead000);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_WALK_EABT,
              m_tbu.m_arch_core.fault_replay_state_storage().fault_reason);
    EXPECT_EQ(0xdead000u, m_tbu.m_arch_core.walker_state().last_fetch_addr);
    const auto core_read = m_tbu.m_arch_core.begin_descriptor_memory_read(
        core_fetch_lifecycle, 0xbeef000, core_walk, true);
    EXPECT_EQ(core_fetch_lifecycle.desc_pa, core_read.desc_pa);
    EXPECT_EQ(0xbeef000u, core_read.transaction_pa);
    EXPECT_TRUE(core_read.stage2_translated);
    EXPECT_EQ(0xbeef000u, m_tbu.m_arch_core.walker_state().last_fetch_addr);
    m_tbu.m_arch_core.complete_descriptor_memory_read(core_read, 0xa003);
    EXPECT_EQ(0xa003u, m_tbu.m_arch_core.walker_state().last_desc);
    m_tbu.m_arch_core.fail_descriptor_memory_read(core_read);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_WALK_EABT,
              m_tbu.m_arch_core.fault_replay_state_storage().fault_reason);
    EXPECT_EQ(0xbeef000u, m_tbu.m_arch_core.walker_state().last_fetch_addr);
    auto core_table_step = m_tbu.m_arch_core.evaluate_descriptor_step(
        0x10000000, 0x8400,
        0x9000 | apollo_smmu_tbu::ARCH_DESC_TABLE, core_walk, 2, false);
    EXPECT_EQ(apollo::smmuv3::apollo_smmu_arch_core::descriptor_step_kind::table,
              core_table_step.kind);
    EXPECT_EQ(0x9000u, core_table_step.next_table_pa);
    auto core_leaf_step = m_tbu.m_arch_core.evaluate_descriptor_step(
        0x10000000, 0x9000,
        0xa000 | apollo_smmu_tbu::ARCH_DESC_AF |
            apollo_smmu_tbu::ARCH_DESC_PAGE,
        core_walk, 3, false);
    EXPECT_EQ(apollo::smmuv3::apollo_smmu_arch_core::descriptor_step_kind::leaf,
              core_leaf_step.kind);
    EXPECT_EQ(0xa000u, core_leaf_step.pa);
    EXPECT_EQ(4u, m_tbu.m_arch_core.walker_state().walk_depth);
    auto core_fault_step = m_tbu.m_arch_core.evaluate_descriptor_step(
        0x10000000, 0x9000, apollo_smmu_tbu::ARCH_DESC_PAGE, core_walk, 3,
        false);
    EXPECT_EQ(apollo::smmuv3::apollo_smmu_arch_core::descriptor_step_kind::fault,
              core_fault_step.kind);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_ACCESS,
              m_tbu.m_arch_core.fault_replay_state_storage().fault_reason);
    const uint32_t strtab_cfg =
        8u | (4u << apollo_smmu_tbu::ARCH_STRTAB_CFG_SPLIT_SHIFT) |
        (apollo_smmu_tbu::ARCH_STRTAB_FMT_2LVL <<
         apollo_smmu_tbu::ARCH_STRTAB_CFG_FMT_SHIFT);
    EXPECT_EQ(8u, apollo::smmuv3::apollo_smmu_arch_core::stream_table_log2size(
                      strtab_cfg));
    EXPECT_EQ(4u, apollo::smmuv3::apollo_smmu_arch_core::stream_table_split(
                      strtab_cfg));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STRTAB_FMT_2LVL,
              apollo::smmuv3::apollo_smmu_arch_core::stream_table_format(strtab_cfg));
    EXPECT_TRUE(apollo::smmuv3::apollo_smmu_arch_core::stream_id_in_bounds(
        strtab_cfg, 0x42));
    EXPECT_TRUE(apollo::smmuv3::apollo_smmu_arch_core::stream_table_split_valid(
        strtab_cfg));
    EXPECT_EQ(4u, apollo::smmuv3::apollo_smmu_arch_core::stream_table_l1_index(
                      0x42, 4));
    EXPECT_EQ(2u, apollo::smmuv3::apollo_smmu_arch_core::stream_table_l2_index(
                      0x42, 4));
    EXPECT_TRUE(apollo::smmuv3::apollo_smmu_arch_core::stream_table_l1_desc_valid(
        0x00090005, 4, 2));
    const uint64_t ste0 =
        apollo_smmu_tbu::ARCH_STE_VALID |
        (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_S1_TRANS)
         << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_CFG_S1_TRANS,
              apollo::smmuv3::apollo_smmu_arch_core::ste_config(ste0));
    EXPECT_TRUE(apollo::smmuv3::apollo_smmu_arch_core::ste_config_supported(ste0));
    EXPECT_TRUE(apollo::smmuv3::apollo_smmu_arch_core::ste_is_s1_enabled(ste0));
    const uint64_t ste1 =
        static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT)
        << apollo_smmu_tbu::ARCH_STE_EATS_SHIFT;
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_DISABLED,
              apollo::smmuv3::apollo_smmu_arch_core::effective_eats(
                  ste0, ste1, false));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT,
              apollo::smmuv3::apollo_smmu_arch_core::effective_eats(
                  ste0, ste1, true));
    const uint64_t cd0 =
        apollo_smmu_tbu::ARCH_CD_VALID_ARCHITECTED |
        (0x12ULL << apollo_smmu_tbu::ARCH_CD_ASID_SHIFT);
    EXPECT_TRUE(apollo::smmuv3::apollo_smmu_arch_core::cd_is_valid(cd0));
    EXPECT_EQ(0x12u, apollo::smmuv3::apollo_smmu_arch_core::cd_asid(cd0));
    EXPECT_EQ(0x12345000u,
              apollo::smmuv3::apollo_smmu_arch_core::cd_ttbr(0x12345000));

    EXPECT_EQ(&m_tbu.m_arch_core.cmdq_state(), &m_tbu.m_cmdq);
    EXPECT_EQ(&m_tbu.m_arch_core.eventq_state(), &m_tbu.m_eventq);
    EXPECT_EQ(&m_tbu.m_arch_core.priq_state(), &m_tbu.m_priq);
    m_tbu.m_cmdq.base = 0x00024003;
    m_tbu.m_eventq.prod = 3;
    m_tbu.m_priq.ovflg = true;
    EXPECT_EQ(0x00024003u, m_tbu.m_arch_core.cmdq_state().base);
    EXPECT_EQ(3u, m_tbu.m_arch_core.eventq_state().prod);
    EXPECT_TRUE(m_tbu.m_arch_core.priq_state().ovflg);
    m_tbu.m_arch_core.reset_register_queue_state();
    EXPECT_EQ(0u, m_tbu.m_cmdq.base);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_FALSE(m_tbu.m_priq.ovflg);

    EXPECT_EQ(&m_tbu.m_arch_core.stream_context_state(), &m_tbu.m_arch_stream_context);
    m_tbu.m_arch_strtab_base = 0x00034000;
    m_tbu.m_arch_strtab_cfg = 0x10003;
    m_tbu.m_arch_ste_base = 0x00044000;
    m_tbu.m_arch_cd_base = 0x00054000;
    m_tbu.m_arch_stream_id = 0x42;
    m_tbu.m_arch_stream_id_valid = true;
    m_tbu.m_arch_selected_ssid = 0x123;
    m_tbu.m_arch_selected_ssid_valid = true;
    EXPECT_EQ(0x00034000u, m_tbu.m_arch_core.stream_context_state().strtab_base);
    EXPECT_EQ(0x10003u, m_tbu.m_arch_core.stream_context_state().strtab_cfg);
    EXPECT_EQ(0x00044000u,
              m_tbu.m_arch_core.stream_context_state().compatibility_ste_base);
    EXPECT_EQ(0x00054000u, m_tbu.m_arch_core.stream_context_state().current_cd_base);
    EXPECT_EQ(0x42u, m_tbu.m_arch_core.stream_context_state().selected_stream_id);
    EXPECT_TRUE(m_tbu.m_arch_core.stream_context_state().selected_stream_id_valid);
    EXPECT_EQ(0x123u, m_tbu.m_arch_core.stream_context_state().selected_ssid);
    EXPECT_TRUE(m_tbu.m_arch_core.stream_context_state().selected_ssid_valid);
    m_tbu.m_arch_core.reset_stream_context_descriptor_state();
    EXPECT_EQ(0u, m_tbu.m_arch_strtab_base);
    EXPECT_EQ(0u, m_tbu.m_arch_strtab_cfg);
    EXPECT_EQ(0u, m_tbu.m_arch_ste_base);
    EXPECT_EQ(0u, m_tbu.m_arch_cd_base);
    EXPECT_FALSE(m_tbu.m_arch_stream_id_valid);
    EXPECT_FALSE(m_tbu.m_arch_selected_ssid_valid);

    EXPECT_EQ(&m_tbu.m_arch_core.walker_state(), &m_tbu.m_arch_walker);
    m_tbu.m_arch_ttbr = 0x00064000;
    m_tbu.m_arch_iova = 0x10000000;
    m_tbu.m_arch_s2ttb = 0x00074000;
    m_tbu.m_arch_last_desc = 0x00075003;
    m_tbu.m_arch_last_pa = 0x000a0000;
    m_tbu.m_arch_last_ipa = 0x000b0000;
    m_tbu.m_arch_last_fetch_addr = 0x00076000;
    m_tbu.m_arch_walk_depth = 4;
    m_tbu.m_arch_last_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S2;
    EXPECT_EQ(0x00064000u, m_tbu.m_arch_core.walker_state().ttbr);
    EXPECT_EQ(0x10000000u, m_tbu.m_arch_core.walker_state().iova);
    EXPECT_EQ(0x00074000u, m_tbu.m_arch_core.walker_state().s2ttb);
    EXPECT_EQ(0x00075003u, m_tbu.m_arch_core.walker_state().last_desc);
    EXPECT_EQ(0x000a0000u, m_tbu.m_arch_core.walker_state().last_pa);
    EXPECT_EQ(0x000b0000u, m_tbu.m_arch_core.walker_state().last_ipa);
    EXPECT_EQ(0x00076000u, m_tbu.m_arch_core.walker_state().last_fetch_addr);
    EXPECT_EQ(4u, m_tbu.m_arch_core.walker_state().walk_depth);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2,
              m_tbu.m_arch_core.walker_state().last_stage);
    m_tbu.m_arch_core.reset_page_table_walker_state();
    EXPECT_EQ(0u, m_tbu.m_arch_ttbr);
    EXPECT_EQ(0u, m_tbu.m_arch_iova);
    EXPECT_EQ(0u, m_tbu.m_arch_s2ttb);
    EXPECT_EQ(0u, m_tbu.m_arch_walk_depth);
    EXPECT_EQ(0u, m_tbu.m_arch_last_stage);

    EXPECT_EQ(&m_tbu.m_arch_core.fault_replay_state_storage(), &m_tbu.m_arch_fault_replay);
    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_PERMISSION;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S1;
    m_tbu.m_arch_fault_event_class = apollo_smmu_tbu::ARCH_EVENT_CLASS_TT;
    m_tbu.m_arch_fault_gpcf = true;
    m_tbu.m_arch_fault_record_suppressed = true;
    m_tbu.m_arch_last_fault_detail = 0x5a;
    m_tbu.m_arch_stall_pending = 2;
    m_tbu.m_arch_endpoint_replay_succeeded = 3;
    m_tbu.m_arch_early_retry_discarded = 4;
    m_tbu.m_arch_next_fault_replay_id = 9;
    m_tbu.m_arch_next_stag = 7;
    m_tbu.m_arch_next_prg = 8;
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
              m_tbu.m_arch_core.fault_replay_state_storage().fault_reason);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S1,
              m_tbu.m_arch_core.fault_replay_state_storage().fault_stage);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_TT,
              m_tbu.m_arch_core.fault_replay_state_storage().fault_event_class);
    EXPECT_TRUE(m_tbu.m_arch_core.fault_replay_state_storage().fault_gpcf);
    EXPECT_TRUE(m_tbu.m_arch_core.fault_replay_state_storage().fault_record_suppressed);
    EXPECT_EQ(0x5au, m_tbu.m_arch_core.fault_replay_state_storage().last_fault_detail);
    EXPECT_EQ(2u, m_tbu.m_arch_core.fault_replay_state_storage().stall_pending);
    EXPECT_EQ(3u, m_tbu.m_arch_core.fault_replay_state_storage().endpoint_replay_succeeded);
    EXPECT_EQ(4u, m_tbu.m_arch_core.fault_replay_state_storage().early_retry_discarded);
    EXPECT_EQ(9u, m_tbu.m_arch_core.fault_replay_state_storage().next_fault_replay_id);
    EXPECT_EQ(7u, m_tbu.m_arch_core.fault_replay_state_storage().next_stag);
    EXPECT_EQ(8u, m_tbu.m_arch_core.fault_replay_state_storage().next_prg);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_CLASS_TRANSLATION,
              apollo::smmuv3::apollo_smmu_arch_core::fault_class(
                  apollo_smmu_tbu::ARCH_FAULT_PERMISSION));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_PERMISSION,
              apollo::smmuv3::apollo_smmu_arch_core::event_number_for_fault(
                  apollo_smmu_tbu::ARCH_FAULT_PERMISSION));
    EXPECT_EQ((apollo_smmu_tbu::ARCH_FAULT_PERMISSION & 0xffu) |
                  (apollo_smmu_tbu::ARCH_FAULT_CLASS_TRANSLATION << 8) |
                  (apollo_smmu_tbu::ARCH_FAULT_STAGE_S1 << 16) |
                  (apollo_smmu_tbu::ARCH_FAULT_ATTR_STALL << 20) | (9u << 28),
              apollo::smmuv3::apollo_smmu_arch_core::fault_detail_word(
                  apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
                  apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, 9, true, false));
    const auto layout = m_tbu.m_arch_core.build_event_record(
        0x42, apollo_smmu_tbu::ARCH_FAULT_PERMISSION, 0x10000000, 0x1000,
        true, false, true, 0x123, 0x55, true, true);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_PERMISSION, layout.event_number);
    EXPECT_EQ(0x42u, static_cast<uint32_t>(layout.words[0] >> 32));
    EXPECT_NE(0ULL, layout.words[0] & (1ULL << 11));
    EXPECT_EQ(0x123u, (layout.words[0] >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_NE(0ULL, layout.words[1] & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0ULL, layout.words[1] & (1ULL << apollo_smmu_tbu::ARCH_EVENT_PNU_SHIFT));
    EXPECT_NE(0ULL, layout.words[1] & (1ULL << apollo_smmu_tbu::ARCH_EVENT_IND_SHIFT));
    EXPECT_NE(0ULL, layout.words[1] & (1ULL << apollo_smmu_tbu::ARCH_EVENT_RNW_SHIFT));
    EXPECT_EQ(0x10000000u, layout.words[2]);
    EXPECT_EQ(0u, layout.words[3]);
    EXPECT_EQ((9ULL << 32) | layout.detail, layout.detail64);
    EXPECT_EQ(3u << 16, m_tbu.m_arch_core.endpoint_replay_status());
    EXPECT_EQ(4u << 24, m_tbu.m_arch_core.early_retry_status());
    EXPECT_EQ(&m_tbu.m_arch_core.stall_records(), &m_tbu.m_arch_stalls);
    m_tbu.m_arch_stalls[0] = {0x42, 0x55, 0x10000000, 0x123, true, true};
    EXPECT_TRUE(m_tbu.m_arch_core.stag_pending(0x55));
    ASSERT_NE(nullptr, m_tbu.m_arch_core.find_stall(0x42, 0x55));
    EXPECT_EQ(0x10000000u, m_tbu.m_arch_core.find_stall(0x42, 0x55)->iova);
    ASSERT_NE(nullptr,
              m_tbu.m_arch_core.find_stall_by_fault(0x42, 0x10000000, true, 0x123));
    m_tbu.m_arch_core.reset_stall_records();
    EXPECT_FALSE(m_tbu.m_arch_stalls[0].pending);
    EXPECT_EQ(&m_tbu.m_arch_core.endpoint_replay_records(),
              &m_tbu.m_arch_endpoint_replays);
    std::array<uint8_t, 0x40> endpoint_payload {};
    endpoint_payload[0] = 0xde;
    endpoint_payload[1] = 0xad;
    endpoint_payload[2] = 0xbe;
    endpoint_payload[3] = 0xef;
    auto endpoint_allocation = m_tbu.m_arch_core.allocate_endpoint_replay_record(
        0x42, 0x66, 0x20000000, 0x40, true, true, 0x123,
        endpoint_payload.data(), endpoint_payload.size());
    ASSERT_TRUE(endpoint_allocation.allocated);
    ASSERT_NE(nullptr, endpoint_allocation.record);
    EXPECT_EQ(1u, m_tbu.m_arch_endpoint_replay_pending);
    ASSERT_NE(nullptr, m_tbu.m_arch_core.find_pending_endpoint_replay(0x42, 0x66));
    EXPECT_EQ(0x20000000u,
              m_tbu.m_arch_core.find_pending_endpoint_replay(0x42, 0x66)->iova);
    EXPECT_EQ(0x40u,
              m_tbu.m_arch_core.find_pending_endpoint_replay(0x42, 0x66)->payload.size());
    auto endpoint_duplicate = m_tbu.m_arch_core.allocate_endpoint_replay_record(
        0x42, 0x66, 0x20000000, 0x40, true, true, 0x123,
        endpoint_payload.data(), endpoint_payload.size());
    EXPECT_TRUE(endpoint_duplicate.duplicate);
    EXPECT_EQ(1u, m_tbu.m_arch_endpoint_replay_pending);
    ASSERT_TRUE(m_tbu.m_arch_core.begin_endpoint_replay_redrive(
        *endpoint_allocation.record));
    const auto endpoint_segment =
        m_tbu.m_arch_core.prepare_endpoint_replay_segment(
            *endpoint_allocation.record, 0, 0x30000000, 0x40);
    ASSERT_TRUE(endpoint_segment.valid);
    EXPECT_TRUE(endpoint_segment.first_segment);
    EXPECT_EQ(endpoint_allocation.record->payload.data(), endpoint_segment.payload);
    EXPECT_EQ(endpoint_payload[0], endpoint_segment.payload[0]);
    const auto endpoint_transaction =
        m_tbu.m_arch_core.begin_endpoint_replay_transaction(
            *endpoint_allocation.record, endpoint_segment);
    ASSERT_TRUE(endpoint_transaction.valid);
    EXPECT_EQ(0x30000000u, endpoint_transaction.pa);
    EXPECT_EQ(0x40u, endpoint_transaction.len);
    EXPECT_EQ(endpoint_segment.payload, endpoint_transaction.payload);
    EXPECT_TRUE(endpoint_transaction.write);
    class FakeArchIoExecutor : public apollo_smmu_tbu::arch_io_executor {
    public:
        bool read_called = false;
        bool replay_called = false;
        uint64_t last_read_pa = 0;
        uint64_t last_replay_pa = 0;
        uint64_t last_replay_len = 0;
        uint8_t* last_replay_payload = nullptr;
        bool last_replay_write = false;
        uint64_t desc_value = 0xcafef00d5a5a1234ULL;
        tlm::tlm_response_status replay_status = tlm::TLM_OK_RESPONSE;

        bool read_descriptor(apollo_smmu_tbu&,
                             const apollo_smmu_tbu::arch_descriptor_memory_read& read,
                             uint64_t& desc) override
        {
            read_called = true;
            last_read_pa = read.transaction_pa;
            desc = desc_value;
            return true;
        }

        tlm::tlm_response_status replay_transaction(
            apollo_smmu_tbu&,
            const apollo_smmu_tbu::arch_endpoint_replay_transaction& transaction,
            sc_core::sc_time&) override
        {
            replay_called = true;
            last_replay_pa = transaction.pa;
            last_replay_len = transaction.len;
            last_replay_payload = transaction.payload;
            last_replay_write = transaction.write;
            return replay_status;
        }
    };
    FakeArchIoExecutor fake_executor;
    m_tbu.set_arch_io_executor_for_testing(&fake_executor);
    EXPECT_EQ(&fake_executor, m_tbu.arch_io_executor_for_testing());
    apollo::smmuv3::apollo_smmu_arch_core::descriptor_fetch fake_fetch {};
    fake_fetch.desc_pa = 0x0004018;
    auto fake_desc_read = m_tbu.m_arch_core.begin_descriptor_memory_read(
        fake_fetch, 0x0005018, {apollo_smmu_tbu::ARCH_GRANULE_4K, 0, 4,
                                apollo_smmu_tbu::ARCH_FAULT_STAGE_S1},
        false);
    uint64_t fake_desc = 0;
    EXPECT_TRUE(m_tbu.execute_descriptor_memory_read(fake_desc_read, fake_desc));
    EXPECT_TRUE(fake_executor.read_called);
    EXPECT_EQ(0x0005018u, fake_executor.last_read_pa);
    EXPECT_EQ(fake_executor.desc_value, fake_desc);
    sc_core::sc_time fake_delay = sc_core::SC_ZERO_TIME;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              m_tbu.execute_endpoint_replay_transaction(endpoint_transaction,
                                                        fake_delay));
    EXPECT_TRUE(fake_executor.replay_called);
    EXPECT_EQ(0x30000000u, fake_executor.last_replay_pa);
    EXPECT_EQ(0x40u, fake_executor.last_replay_len);
    EXPECT_EQ(endpoint_segment.payload, fake_executor.last_replay_payload);
    EXPECT_TRUE(fake_executor.last_replay_write);
    m_tbu.set_arch_io_executor_for_testing(nullptr);
    EXPECT_NE(&fake_executor, m_tbu.arch_io_executor_for_testing());
    EXPECT_TRUE(m_tbu.m_arch_core.complete_endpoint_replay_transaction(
        *endpoint_allocation.record, endpoint_segment, endpoint_transaction,
        tlm::TLM_OK_RESPONSE));
    EXPECT_TRUE(m_tbu.m_arch_core.finish_endpoint_replay_redrive(
        *endpoint_allocation.record));
    EXPECT_TRUE(endpoint_allocation.record->redriven);
    EXPECT_EQ(0x40u, endpoint_allocation.record->replay_len);
    EXPECT_EQ(0x30000000u, endpoint_allocation.record->replay_pa);
    EXPECT_TRUE(m_tbu.m_arch_core.retire_endpoint_replay(
        *endpoint_allocation.record, true, true));
    EXPECT_EQ(nullptr, m_tbu.m_arch_core.find_pending_endpoint_replay(0x42, 0x66));
    ASSERT_NE(nullptr, m_tbu.m_arch_core.find_endpoint_replay(0x42, 0x66));
    EXPECT_EQ(0x40u, m_tbu.m_arch_core.find_endpoint_replay(0x42, 0x66)->len);
    EXPECT_EQ(0u, m_tbu.m_arch_endpoint_replay_pending);
    EXPECT_EQ(1u, m_tbu.m_arch_endpoint_replay_retried);
    EXPECT_EQ(4u, m_tbu.m_arch_endpoint_replay_succeeded);
    EXPECT_EQ(1u, m_tbu.m_arch_endpoint_replay_redriven);
    m_tbu.m_arch_core.reset_endpoint_replay_records();
    EXPECT_EQ(nullptr, m_tbu.m_arch_core.find_endpoint_replay(0x42, 0x66));
    m_tbu.m_arch_core.reset_fault_replay_state();
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_NONE, m_tbu.m_arch_fault_reason);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_NONE, m_tbu.m_arch_fault_stage);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_IN, m_tbu.m_arch_fault_event_class);
    EXPECT_FALSE(m_tbu.m_arch_fault_gpcf);
    EXPECT_FALSE(m_tbu.m_arch_fault_record_suppressed);
    EXPECT_EQ(0u, m_tbu.m_arch_last_fault_detail);
    EXPECT_EQ(0u, m_tbu.m_arch_stall_pending);
    EXPECT_EQ(0u, m_tbu.m_arch_endpoint_replay_succeeded);
    EXPECT_EQ(0u, m_tbu.m_arch_early_retry_discarded);
    EXPECT_EQ(1u, m_tbu.m_arch_next_fault_replay_id);
    EXPECT_EQ(1u, m_tbu.m_arch_next_stag);
    EXPECT_EQ(1u, m_tbu.m_arch_next_prg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedRegisterMmioSurface)
{
    const uint32_t idr0 = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR0));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR0, idr0);
    EXPECT_NE(0u, idr0 & apollo_smmu_tbu::ARCH_IDR0_S1P);
    EXPECT_NE(0u, idr0 & apollo_smmu_tbu::ARCH_IDR0_S2P);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR0_TTF_AARCH64,
              idr0 & apollo_smmu_tbu::ARCH_IDR0_TTF_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR0_HTTU_ACCESS_DIRTY_TABLE,
              idr0 & apollo_smmu_tbu::ARCH_IDR0_HTTU_MASK);
    EXPECT_NE(0u, idr0 & apollo_smmu_tbu::ARCH_IDR0_ASID16);
    EXPECT_NE(0u, idr0 & apollo_smmu_tbu::ARCH_IDR0_VMID16);
    EXPECT_NE(0u, idr0 & apollo_smmu_tbu::ARCH_IDR0_CD2L);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR0_ST_LEVEL_2LVL,
              idr0 & apollo_smmu_tbu::ARCH_IDR0_ST_LEVEL_MASK);
    const uint32_t idr1 = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR1));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR1, idr1);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR1_SIDSIZE,
              (idr1 & apollo_smmu_tbu::ARCH_IDR1_SIDSIZE_MASK) >>
                  apollo_smmu_tbu::ARCH_IDR1_SIDSIZE_SHIFT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR1_SSIDSIZE,
              (idr1 & apollo_smmu_tbu::ARCH_IDR1_SSIDSIZE_MASK) >>
                  apollo_smmu_tbu::ARCH_IDR1_SSIDSIZE_SHIFT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR1_QUEUE_LOG2_MAX,
              (idr1 & apollo_smmu_tbu::ARCH_IDR1_CMDQS_MASK) >>
                  apollo_smmu_tbu::ARCH_IDR1_CMDQS_SHIFT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR1_QUEUE_LOG2_MAX,
              (idr1 & apollo_smmu_tbu::ARCH_IDR1_EVENTQS_MASK) >>
                  apollo_smmu_tbu::ARCH_IDR1_EVENTQS_SHIFT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR1_QUEUE_LOG2_MAX,
              (idr1 & apollo_smmu_tbu::ARCH_IDR1_PRIQS_MASK) >>
                  apollo_smmu_tbu::ARCH_IDR1_PRIQS_SHIFT);
    EXPECT_NE(0u, idr1 & apollo_smmu_tbu::ARCH_IDR1_ATTR_PERMS_OVR);
    EXPECT_NE(0u, idr1 & apollo_smmu_tbu::ARCH_IDR1_ATTR_TYPES_OVR);
    EXPECT_EQ(0u, idr1 & apollo_smmu_tbu::ARCH_IDR1_ECMDQ);
    const uint32_t idr3 = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR3));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR3, idr3);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_HAD);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_XNX);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_MPAM);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_FWB);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_STT);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_RIL);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR3_BBML_LEVEL_2,
              idr3 & apollo_smmu_tbu::ARCH_IDR3_BBML_MASK);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_E0PD);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_PTWNNC);
    EXPECT_EQ(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_DPT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR4, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR4)));
    const uint32_t idr5 = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR5));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR5, idr5);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR5_OAS_48,
              idr5 & apollo_smmu_tbu::ARCH_IDR5_OAS_MASK);
    EXPECT_NE(0u, idr5 & apollo_smmu_tbu::ARCH_IDR5_GRAN4K);
    EXPECT_NE(0u, idr5 & apollo_smmu_tbu::ARCH_IDR5_GRAN16K);
    EXPECT_NE(0u, idr5 & apollo_smmu_tbu::ARCH_IDR5_GRAN64K);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IIDR, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IIDR)));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_AIDR, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_AIDR)));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_AIDR_SMMUV3_3,
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_AIDR)) & 0xffu);
    EXPECT_NE(apollo_smmu_tbu::ARCH_AIDR, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IIDR)));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_READY | apollo_smmu_tbu::ARCH_STATUS_QUEUE_MODEL,
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_STATUS)));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), 0x8000001f);
    EXPECT_EQ(0x8000001fu, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0ACK)));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL), 0x0f);
    EXPECT_EQ(0x0fu, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRLACK)));

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO, apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                0x12345020);
    EXPECT_EQ(0x12345020u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO)));
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI)));

    EXPECT_NE(0u, m_tbu.features() & apollo_smmu_tbu::FEATURE_ARCH_REG_QUEUE_SURFACE);
    EXPECT_NE(0u, m_tbu.features() & apollo_smmu_tbu::FEATURE_ARCH_CR0_QUEUE_GATES);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedIdr0AdvertisesAtsPri)
{
    const uint32_t idr0 = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR0));

    EXPECT_NE(0u, idr0 & apollo_smmu_tbu::ARCH_IDR0_ATS);
    EXPECT_NE(0u, idr0 & apollo_smmu_tbu::ARCH_IDR0_PRI);
    EXPECT_TRUE(apollo_smmu_tbu::arch_ats_supported());
    EXPECT_TRUE(apollo_smmu_tbu::arch_pri_supported());
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureRegisterBankConfiguresStrtabCmdqAndEventq)
{
    constexpr uint64_t secure_strtab_base = 0x12345020;
    constexpr uint64_t secure_cmdq_base = 0x12346020;
    constexpr uint64_t secure_eventq_base = 0x12347020;
    constexpr uint32_t strtab_cfg = 0x5;

    const uint32_t s_idr0 = reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IDR0));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_S_IDR0, s_idr0);
    EXPECT_NE(0u, s_idr0 & apollo_smmu_tbu::ARCH_S_IDR0_MSI);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_S_IDR0_STALL_MODEL_TERMINATE_ONLY,
              s_idr0 & apollo_smmu_tbu::ARCH_S_IDR0_STALL_MODEL_MASK);
    EXPECT_EQ(0u, s_idr0 & apollo_smmu_tbu::ARCH_S_IDR0_ECMDQ);
    EXPECT_EQ(0u, s_idr0 & ~(apollo_smmu_tbu::ARCH_S_IDR0_MSI |
                             apollo_smmu_tbu::ARCH_S_IDR0_STALL_MODEL_MASK |
                             apollo_smmu_tbu::ARCH_S_IDR0_ECMDQ));
    EXPECT_NE(reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR0)), s_idr0);

    const uint32_t s_idr1 = reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IDR1));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_S_IDR1, s_idr1);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_S_IDR1_S_SIDSIZE,
              s_idr1 & apollo_smmu_tbu::ARCH_S_IDR1_S_SIDSIZE_MASK);
    EXPECT_EQ(0u, s_idr1 &
                      (apollo_smmu_tbu::ARCH_IDR1_SSIDSIZE_MASK |
                       apollo_smmu_tbu::ARCH_IDR1_PRIQS_MASK |
                       apollo_smmu_tbu::ARCH_IDR1_EVENTQS_MASK |
                       apollo_smmu_tbu::ARCH_IDR1_CMDQS_MASK |
                       apollo_smmu_tbu::ARCH_IDR1_ATTR_PERMS_OVR |
                       apollo_smmu_tbu::ARCH_IDR1_ATTR_TYPES_OVR));
    const uint32_t s_idr3 = reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IDR3));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_S_IDR3, s_idr3);
    EXPECT_EQ(0u, s_idr3 & (apollo_smmu_tbu::ARCH_IDR3_MPAM |
                            apollo_smmu_tbu::ARCH_IDR3_RIL |
                            apollo_smmu_tbu::ARCH_IDR3_DPT |
                            apollo_smmu_tbu::ARCH_S_IDR3_SAMS));
    EXPECT_NE(reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR3)), s_idr3);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_S_IDR4,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IDR4)));

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR2), 0xffffffffu);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL), 0xffffffffu);
    EXPECT_EQ(ARCH_CR0_ALL_QUEUES,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0ACK)));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CR2_WRITABLE_MASK,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR2)));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_CTRL_WRITABLE_MASK,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRLACK)));

    reg_s_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, secure_strtab_base);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), strtab_cfg);
    EXPECT_EQ(static_cast<uint32_t>(secure_strtab_base),
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO)));
    EXPECT_EQ(strtab_cfg,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG)));

    const auto& strtab_bank = m_tbu.arch_security_strtab_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    EXPECT_TRUE(strtab_bank.configured);
    EXPECT_EQ(secure_strtab_base, strtab_bank.base);
    EXPECT_EQ(strtab_cfg, strtab_bank.cfg);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO)));

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 3);
    EXPECT_EQ(static_cast<uint32_t>(secure_cmdq_base | 2),
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO)));
    EXPECT_EQ(3u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD)));

    reg_s_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, secure_eventq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_PROD), 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS), 1);
    EXPECT_EQ(static_cast<uint32_t>(secure_eventq_base | 2),
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO)));
    EXPECT_EQ(2u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_PROD)));
    EXPECT_EQ(1u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS)));
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO)));

    const auto& eventq_bank = m_tbu.arch_security_eventq_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    EXPECT_TRUE(eventq_bank.queue_configured);
    EXPECT_EQ(secure_eventq_base | 2, eventq_bank.queue.base);
    EXPECT_EQ(2u, eventq_bank.queue.prod);
    EXPECT_EQ(1u, eventq_bank.queue.cons);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureCmdqProducerConsumesMemoryBackedCommands)
{
    constexpr uint64_t secure_cmdq_base = 0x6000;
    constexpr uint64_t iova_page = 0x12345000;
    constexpr uint32_t stream_id = 0x31;

    m_tbu.ats_fill(stream_id, iova_page);
    ASSERT_TRUE(m_tbu.ats_lookup(stream_id, iova_page));

    store_u64(secure_cmdq_base, apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(secure_cmdq_base + sizeof(uint64_t), 0);
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_ATC_INV |
                  (static_cast<uint64_t>(stream_id) << 32));
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              iova_page);

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);
    EXPECT_EQ(0u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, iova_page));

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(2u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, iova_page));
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_atc_invs);
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_syncs);
    EXPECT_EQ(stream_id, m_tbu.m_arch_last_cmd_stream_id);
    EXPECT_EQ(iova_page, m_tbu.m_arch_last_cmd_iova);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureCmdqSsecCfgiTargetsSelectedSecurityState)
{
    constexpr uint64_t secure_cmdq_base = 0x7900;
    constexpr uint32_t stream_id = 0x3a;
    constexpr uint64_t stale_ste0 =
        apollo_smmu_tbu::ARCH_STE_VALID |
        (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_S1_TRANS)
         << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT);
    constexpr uint64_t request_ste0 =
        stale_ste0 | (1ULL << apollo_smmu_tbu::ARCH_START_LEVEL_SHIFT);

    m_tbu.clear_config_cache();
    m_tbu.config_cache_fill(stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE,
                            stale_ste0, 0x4000, 0);
    m_tbu.config_cache_fill(stream_id, apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
                            stale_ste0, 0x5000, 0);
    EXPECT_TRUE(m_tbu.config_cache_conflict_present(
        stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE, request_ste0, 0x4000,
        0));
    EXPECT_TRUE(m_tbu.config_cache_conflict_present(
        stream_id, apollo_smmu_tbu::ARCH_SECURITY_NONSECURE, request_ste0,
        0x5000, 0));

    store_u64(secure_cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_CFGI_STE |
                  apollo_smmu_tbu::ARCH_CMDQ_SSEC |
                  (static_cast<uint64_t>(stream_id) << 32));
    store_u64(secure_cmdq_base + sizeof(uint64_t), 0);
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_CFGI_STE |
                  (static_cast<uint64_t>(stream_id) << 32));
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              0);

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);
    EXPECT_EQ(1u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_FALSE(m_tbu.config_cache_conflict_present(
        stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE, request_ste0, 0x4000,
        0));
    EXPECT_TRUE(m_tbu.config_cache_conflict_present(
        stream_id, apollo_smmu_tbu::ARCH_SECURITY_NONSECURE, request_ste0,
        0x5000, 0));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_SECURE,
              m_tbu.m_arch_last_cmd_security_state);
    EXPECT_TRUE(m_tbu.m_arch_last_cmd_ssec);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);
    EXPECT_EQ(2u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_FALSE(m_tbu.config_cache_conflict_present(
        stream_id, apollo_smmu_tbu::ARCH_SECURITY_NONSECURE, request_ste0,
        0x5000, 0));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
              m_tbu.m_arch_last_cmd_security_state);
    EXPECT_FALSE(m_tbu.m_arch_last_cmd_ssec);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureCmdqSsecTlbiAtcTargetsSelectedSecurityState)
{
    constexpr uint64_t secure_cmdq_base = 0x7b00;
    constexpr uint64_t page = 0x12345000;
    constexpr uint32_t stream_id = 0x3c;
    constexpr uint16_t asid = 0x42;
    constexpr uint16_t vmid = 0x24;

    auto tlbi_word0 = [](uint16_t asid_value, uint16_t vmid_value,
                         bool ssec) {
        return static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VA) |
               (ssec ? apollo_smmu_tbu::ARCH_CMDQ_SSEC : 0) |
               (static_cast<uint64_t>(vmid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT) |
               (static_cast<uint64_t>(asid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_ASID_SHIFT);
    };
    auto atc_word0 = [](uint32_t sid, bool ssec) {
        return static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_ATC_INV) |
               (ssec ? apollo_smmu_tbu::ARCH_CMDQ_SSEC : 0) |
               (static_cast<uint64_t>(sid) << 32);
    };

    m_tbu.clear_ats_cache();
    m_tbu.ats_fill(stream_id, page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    m_tbu.ats_fill(stream_id, page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE);
    ASSERT_TRUE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    ASSERT_TRUE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));

    store_u64(secure_cmdq_base, tlbi_word0(asid, vmid, true));
    store_u64(secure_cmdq_base + sizeof(uint64_t), page);
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              atc_word0(stream_id, false));
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              page);

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                  apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_SECURE,
              m_tbu.m_arch_last_cmd_security_state);
    EXPECT_TRUE(m_tbu.m_arch_last_cmd_ssec);
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VA,
              m_tbu.m_arch_last_cmd_opcode);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                  apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
              m_tbu.m_arch_last_cmd_security_state);
    EXPECT_FALSE(m_tbu.m_arch_last_cmd_ssec);
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_atc_invs);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureOnlyTlbiOpcodesRequireSecureCmdqAndTargetSecureState)
{
    constexpr uint64_t cmdq_base = 0x7d00;
    constexpr uint64_t secure_cmdq_base = 0x7e00;
    constexpr uint64_t page = 0x12347000;
    constexpr uint32_t stream_id = 0x3e;
    constexpr uint16_t asid = 0x34;
    constexpr uint16_t other_asid = 0x35;
    constexpr uint16_t vmid = 0x45;

    auto tlbi_word0 = [](uint32_t opcode, uint16_t asid_value,
                         uint16_t vmid_value, uint32_t pages = 1) {
        return static_cast<uint64_t>(opcode) |
               (pages > 1 ?
                    (static_cast<uint64_t>(pages - 1)
                     << apollo_smmu_tbu::ARCH_CMDQ_RANGE_NUM_SHIFT) :
                    0) |
               (static_cast<uint64_t>(vmid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT) |
               (static_cast<uint64_t>(asid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_ASID_SHIFT);
    };
    auto range_word1 = [](uint64_t target_page) {
        return target_page |
               (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_4K)
                << apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_SHIFT) |
               apollo_smmu_tbu::ARCH_CMDQ_LEAF;
    };

    m_tbu.clear_ats_cache();
    m_tbu.ats_fill(stream_id, page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    m_tbu.ats_fill(stream_id, page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE);

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_TLBI_SNH_ALL);
    store_u64(cmdq_base + sizeof(uint64_t), 0);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_tlbis);

    m_tbu.ats_fill(stream_id, page, other_asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    store_u64(secure_cmdq_base,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_S_EL2_ASID,
                         asid, vmid));
    store_u64(secure_cmdq_base + sizeof(uint64_t), 0);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                  secure_cmdq_base | 3);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0),
                ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                  apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, page, other_asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_SECURE,
              m_tbu.m_arch_last_cmd_security_state);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_S_EL2_ASID,
              m_tbu.m_arch_last_cmd_opcode);

    m_tbu.ats_fill(stream_id, page + apollo_smmu_tbu::PAGE_SIZE, asid, vmid,
                   false, 0, apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    m_tbu.ats_fill(stream_id, page + 2 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid, false, 0, apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    m_tbu.ats_fill(stream_id, page + apollo_smmu_tbu::PAGE_SIZE, asid, vmid,
                   false, 0, apollo_smmu_tbu::ARCH_SECURITY_NONSECURE);
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_S_S2_IPA, 0, vmid,
                         2));
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              range_word1(page + apollo_smmu_tbu::PAGE_SIZE));
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, page + apollo_smmu_tbu::PAGE_SIZE,
                                  asid, vmid, false, 0,
                                  apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id,
                                  page + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                  asid, vmid, false, 0,
                                  apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, page + apollo_smmu_tbu::PAGE_SIZE,
                                 asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_range_bytes /
                      apollo_smmu_tbu::PAGE_SIZE);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_S_S2_IPA,
              m_tbu.m_arch_last_cmd_opcode);

    m_tbu.ats_fill(stream_id, page + 3 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid, false, 0, apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    m_tbu.ats_fill(stream_id, page + 3 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid, false, 0, apollo_smmu_tbu::ARCH_SECURITY_NONSECURE);
    store_u64(secure_cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_TLBI_SNH_ALL);
    store_u64(secure_cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              0);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 3);
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id,
                                  page + 3 * apollo_smmu_tbu::PAGE_SIZE,
                                  asid, vmid, false, 0,
                                  apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id,
                                 page + 3 * apollo_smmu_tbu::PAGE_SIZE,
                                 asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_SNH_ALL,
              m_tbu.m_arch_last_cmd_opcode);
}

TEST_BENCH(ApolloSmmuTbuTestBench, NonSecureCmdqSsecCfgiIsIllegal)
{
    constexpr uint64_t cmdq_base = 0x7a00;
    constexpr uint32_t stream_id = 0x3b;
    constexpr uint64_t stale_ste0 =
        apollo_smmu_tbu::ARCH_STE_VALID |
        (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_S1_TRANS)
         << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT);
    constexpr uint64_t request_ste0 =
        stale_ste0 | (1ULL << apollo_smmu_tbu::ARCH_START_LEVEL_SHIFT);

    m_tbu.clear_config_cache();
    m_tbu.config_cache_fill(stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE,
                            stale_ste0, 0x4000, 0);

    store_u64(cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_CFGI_STE |
                  apollo_smmu_tbu::ARCH_CMDQ_SSEC |
                  (static_cast<uint64_t>(stream_id) << 32));
    store_u64(cmdq_base + sizeof(uint64_t), 0);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_TRUE(m_tbu.config_cache_conflict_present(
        stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE, request_ste0, 0x4000,
        0));
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_cfgis);
    EXPECT_TRUE(m_tbu.m_arch_last_cmd_ssec);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
              m_tbu.m_arch_last_cmd_security_state);
}

TEST_BENCH(ApolloSmmuTbuTestBench, NonSecureCmdqSsecTlbiAtcAreIllegal)
{
    constexpr uint64_t cmdq_base = 0x7c00;
    constexpr uint64_t page = 0x12346000;
    constexpr uint32_t stream_id = 0x3d;
    constexpr uint16_t asid = 0x33;
    constexpr uint16_t vmid = 0x44;

    auto tlbi_word0 = [](uint16_t asid_value, uint16_t vmid_value) {
        return static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VA) |
               apollo_smmu_tbu::ARCH_CMDQ_SSEC |
               (static_cast<uint64_t>(vmid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT) |
               (static_cast<uint64_t>(asid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_ASID_SHIFT);
    };
    auto atc_word0 = [](uint32_t sid) {
        return static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_ATC_INV) |
               apollo_smmu_tbu::ARCH_CMDQ_SSEC |
               (static_cast<uint64_t>(sid) << 32);
    };

    m_tbu.clear_ats_cache();
    m_tbu.ats_fill(stream_id, page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_SECURE);

    store_u64(cmdq_base, tlbi_word0(asid, vmid));
    store_u64(cmdq_base + sizeof(uint64_t), page);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              atc_word0(stream_id));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              page);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_tlbis);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS), 1);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(1u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_atc_invs);
    EXPECT_TRUE(m_tbu.m_arch_last_cmd_ssec);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
              m_tbu.m_arch_last_cmd_security_state);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureCmdqAtcInvSyncCerrorPausesAndRecovers)
{
    constexpr uint64_t secure_cmdq_base = 0x7000;
    constexpr uint64_t iova0 = 0x12345000;
    constexpr uint64_t iova1 = 0x12346000;

    m_tbu.m_arch_atc_inv_sync_force_fail_count = 2;

    store_u64(secure_cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_ATC_INV | (1ULL << 32));
    store_u64(secure_cmdq_base + sizeof(uint64_t), iova0);
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_ATC_INV | (2ULL << 32));
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              iova1);
    store_u64(secure_cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(secure_cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              0);
    store_u64(secure_cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(secure_cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              0);

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);

    uint32_t cons = reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(2u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ATC_INV_SYNC,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(2u, m_tbu.m_arch_cmd_atc_invs);
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_syncs);
    EXPECT_EQ(1u, m_tbu.m_arch_atc_inv_sync_errors);
    EXPECT_EQ(0u, m_tbu.m_arch_atc_inv_sync_pending_count);
    EXPECT_EQ(0u, m_tbu.m_arch_atc_inv_sync_force_fail_count);
    EXPECT_NE(0u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERROR)) &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);
    cons = reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(2u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_syncs);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS), 3);
    EXPECT_EQ(3u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);
    EXPECT_EQ(4u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_syncs);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureCmdSyncMsiWriteAndAbortAreReported)
{
    constexpr uint64_t secure_cmdq_base = 0x7800;
    constexpr uint64_t cmdq_msi_addr = 0x1010;
    constexpr uint64_t bad_msi_addr = MEM_SIZE + 0x2000;
    constexpr uint32_t cmdq_msi_data = 0xc001d00du;
    constexpr uint32_t bad_msi_data = 0xbad0bad0u;

    store_u64(secure_cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_SYNC |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_SYNC_CS_IRQ)
                   << apollo_smmu_tbu::ARCH_CMD_SYNC_CS_SHIFT) |
                  (static_cast<uint64_t>(cmdq_msi_data)
                   << apollo_smmu_tbu::ARCH_CMD_SYNC_MSIDATA_SHIFT));
    store_u64(secure_cmdq_base + sizeof(uint64_t), cmdq_msi_addr);
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_SYNC |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_SYNC_CS_IRQ)
                   << apollo_smmu_tbu::ARCH_CMD_SYNC_CS_SHIFT) |
                  (static_cast<uint64_t>(bad_msi_data)
                   << apollo_smmu_tbu::ARCH_CMD_SYNC_MSIDATA_SHIFT));
    store_u64(secure_cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              bad_msi_addr);

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(2u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(cmdq_msi_data, load_u32(cmdq_msi_addr));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC, m_tbu.m_arch_last_msi_source);
    EXPECT_EQ(1u, m_tbu.m_arch_msi_writes);
    EXPECT_EQ(1u, m_tbu.m_arch_msi_aborts);
    EXPECT_EQ(2u, m_tbu.m_arch_cmd_syncs);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
    EXPECT_NE(0u, reg_read_secure_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_MSI_CMDQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_CMDQ_ABORT);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_MSI_CMDQ_ABORT);

    EXPECT_EQ(0u, reg_read_secure_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_CMDQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureCmdSyncIrqUsesSecureCtrlBank)
{
    constexpr uint64_t secure_cmdq_base = 0x7880;

    store_u64(secure_cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_SYNC |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_SYNC_CS_NONE)
                   << apollo_smmu_tbu::ARCH_CMD_SYNC_CS_SHIFT));
    store_u64(secure_cmdq_base + sizeof(uint64_t), 0);

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, secure_cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_SECURE,
              m_tbu.m_arch_cmdq_sync_irq_security_state);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_lines & apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
    EXPECT_NE(0u, m_tbu.m_arch_irq_lines & apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRLACK)) &
                  apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
}

TEST_BENCH(ApolloSmmuTbuTestBench, MpamDiscoveryAdvertisesVmsPrerequisites)
{
    const uint32_t idr3 = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR3));
    const uint32_t mpamidr = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_MPAMIDR));

    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_HAD);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_XNX);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_MPAM);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_FWB);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_STT);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_RIL);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IDR3_BBML_LEVEL_2,
              idr3 & apollo_smmu_tbu::ARCH_IDR3_BBML_MASK);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_E0PD);
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_PTWNNC);
    EXPECT_EQ(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_DPT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAMIDR_PARTID_MAX, mpamidr & 0xffffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAMIDR_PMG_MAX, (mpamidr >> 16) & 0xffu);
    EXPECT_NE(0u, m_tbu.features() & apollo_smmu_tbu::FEATURE_ARCH_VMS_FETCH);
    EXPECT_NE(0u, m_tbu.features() & apollo_smmu_tbu::FEATURE_ARCH_CFGI_VMS_PIDM);
}

TEST_BENCH(ApolloSmmuTbuTestBench, IrqCtrlReservedBitsAreMaskedInCtrlAck)
{
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL), 0xffffffffu);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_CTRL_WRITABLE_MASK,
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL)));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_CTRL_WRITABLE_MASK,
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRLACK)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, MsiIrqCfgRegistersAreMaskedAndGuarded)
{
    EXPECT_NE(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR0)) &
                      apollo_smmu_tbu::ARCH_IDR0_MSI);

    reg_write64(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0,
                apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0_HI,
                0xffffffffffffffffULL);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG1), 0xfeedbeefu);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG2), 0xffffffffu);

    EXPECT_EQ(static_cast<uint32_t>(apollo_smmu_tbu::ARCH_MSI_CFG0_ADDR_MASK),
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0)));
    EXPECT_EQ(static_cast<uint32_t>(apollo_smmu_tbu::ARCH_MSI_CFG0_ADDR_MASK >> 32),
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0_HI)));
    EXPECT_EQ(0xfeedbeefu, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG1)));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MSI_CFG2_WRITABLE_MASK,
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG2)));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_GERROR);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG1), 0x12345678u);
    EXPECT_EQ(0xfeedbeefu, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG1)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, MsiWritesAndAbortGerrorBitsFollowIrqSources)
{
    constexpr uint64_t event_msi_addr = 0x1000;
    constexpr uint64_t pri_msi_addr = 0x1004;
    constexpr uint64_t bad_msi_addr = MEM_SIZE + 0x1000;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0,
                apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0_HI, event_msi_addr);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG1), 0xa5a50001u);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG0,
                apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG0_HI, pri_msi_addr);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG1), 0xa5a50002u);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_EVENTQ | apollo_smmu_tbu::ARCH_IRQ_PRIQ |
                    apollo_smmu_tbu::ARCH_IRQ_GERROR);

    m_tbu.m_eventq.base = 0x3000 | 2;
    m_tbu.push_event_record("msi-event", 0x101000, 0x1000);
    EXPECT_EQ(0xa5a50001u, load_u32(event_msi_addr));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_EVENTQ, m_tbu.m_arch_last_msi_source);

    m_tbu.m_priq.base = 0x4000 | 2;
    m_tbu.push_pri_record(0x102000, 0x202000, 0x1000);
    EXPECT_EQ(0xa5a50002u, load_u32(pri_msi_addr));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_PRIQ, m_tbu.m_arch_last_msi_source);
    EXPECT_EQ(2u, m_tbu.m_arch_msi_writes);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS), m_tbu.m_eventq.prod);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_GERROR);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0,
                apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0_HI, bad_msi_addr);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_EVENTQ | apollo_smmu_tbu::ARCH_IRQ_GERROR);

    m_tbu.push_event_record("msi-abort", 0x103000, 0x1000);
    EXPECT_EQ(1u, m_tbu.m_arch_msi_aborts);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_MSI_EVENTQ_ABORT);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdSyncMsiWriteAndAbortAreReported)
{
    constexpr uint64_t cmdq_base = 0x5000;
    constexpr uint64_t cmdq_msi_addr = 0x1010;
    constexpr uint64_t bad_msi_addr = MEM_SIZE + 0x2000;
    constexpr uint32_t cmdq_msi_data = 0xc001d00du;

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_SYNC |
                            (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_SYNC_CS_IRQ)
                             << apollo_smmu_tbu::ARCH_CMD_SYNC_CS_SHIFT) |
                            (static_cast<uint64_t>(cmdq_msi_data)
                             << apollo_smmu_tbu::ARCH_CMD_SYNC_MSIDATA_SHIFT));
    store_u64(cmdq_base + sizeof(uint64_t), cmdq_msi_addr);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_SYNC |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_SYNC_CS_IRQ)
                   << apollo_smmu_tbu::ARCH_CMD_SYNC_CS_SHIFT) |
                  (0xbad0bad0ULL << apollo_smmu_tbu::ARCH_CMD_SYNC_MSIDATA_SHIFT));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t),
              bad_msi_addr);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(2u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(cmdq_msi_data, load_u32(cmdq_msi_addr));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_SYNC_CS_IRQ,
              m_tbu.m_arch_last_cmd_sync_signal);
    EXPECT_EQ(1u, m_tbu.m_arch_msi_aborts);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_MSI_CMDQ_ABORT);
}

TEST_BENCH(ApolloSmmuTbuTestBench, GerrorRegisterExposesRawToggleStateAndGerrornAcknowledgesActiveBits)
{
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_QUEUE_OVERFLOW);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN)) &
                      apollo_smmu_tbu::ARCH_GERROR_QUEUE_OVERFLOW);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_NE(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR)) &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_NE(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN)) &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS), 1);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR)) &
                     apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
}

TEST_BENCH(ApolloSmmuTbuTestBench, QueueIndexWrapAndBaseDecode)
{
    apollo_smmu_tbu::arch_queue queue {};

    queue.base = 0x2000 | 2;
    EXPECT_EQ(4u, apollo_smmu_tbu::queue_entries(queue));
    EXPECT_EQ(0x2000u, apollo_smmu_tbu::queue_base_addr(queue));
    EXPECT_EQ(0u, apollo_smmu_tbu::queue_index(4, 4));
    EXPECT_EQ(1u, apollo_smmu_tbu::queue_advance(0, 4));
    EXPECT_EQ(4u, apollo_smmu_tbu::queue_advance(3, 4));
    EXPECT_EQ(5u, apollo_smmu_tbu::queue_advance(4, 4));
}

TEST_BENCH(ApolloSmmuTbuTestBench, EventAndPriQueuesWriteMemoryBackedRecords)
{
    m_tbu.m_eventq.base = 0x3000 | 2;
    m_tbu.m_priq.base = 0x4000 | 2;

    m_tbu.push_event_record("unit-test", 0x101000, 0x2000);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ((1ULL << 32) | 0x1u, load_u64(0x3000));
    EXPECT_EQ(0x101000u, load_u64(0x3008));
    EXPECT_EQ(0x2000u, load_u64(0x3010));
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_EVENTQ);

    m_tbu.push_pri_record(0x102000, 0x202000, 0x3000);
    EXPECT_EQ(1u, m_tbu.m_priq.prod);
    EXPECT_EQ((1ULL << 32) | 0x2u, load_u64(0x4000));
    EXPECT_EQ(0x102000u, load_u64(0x4008));
    EXPECT_EQ(0x202000u, load_u64(0x4010));
    EXPECT_EQ(0x3000u, load_u64(0x4018));
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_PRIQ);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Cr0QueueEnableGatesCmdEventAndPriQueues)
{
    constexpr uint64_t cmdq_base = 0x5000;

    m_tbu.m_eventq.base = 0x3000 | 2;
    m_tbu.m_priq.base = 0x4000 | 2;
    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(cmdq_base + 8, 0);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), 0);
    m_tbu.push_event_record("eventq-disabled", 0x101000, 0x1000);
    m_tbu.push_pri_record(0x102000, 0x202000, 0x1000);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, m_tbu.m_priq.prod);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status &
                     (apollo_smmu_tbu::ARCH_IRQ_EVENTQ |
                      apollo_smmu_tbu::ARCH_IRQ_PRIQ |
                      apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    m_tbu.push_event_record("eventq-enabled", 0x101000, 0x1000);
    m_tbu.push_pri_record(0x102000, 0x202000, 0x1000);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(1u, m_tbu.m_priq.prod);
    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_EVENTQ);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_PRIQ);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqProducerWriteConsumesMemoryBackedCommands)
{
    constexpr uint64_t cmdq_base = 0x5000;

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(cmdq_base + 8, 0);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + 8, 0);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(2u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
}

TEST_BENCH(ApolloSmmuTbuTestBench, DptiCommandsSetGerrorWhenDptUnsupported)
{
    constexpr uint64_t cmdq_base = 0x5000;
    constexpr uint64_t dpti_pa = 0x12345000;

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_DPTI_ALL);
    store_u64(cmdq_base + 8, 0);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_DPTI_PA);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + 8, dpti_pa);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t idr3 = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR3));
    EXPECT_NE(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_MPAM);
    EXPECT_EQ(0u, idr3 & apollo_smmu_tbu::ARCH_IDR3_DPT);
    uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_dptis);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_DPTI_ALL,
              reg_read32(apollo_smmu_tbu::REG_ARCH_CMD_STATUS) >> 24);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS), 1);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);
    cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));

    EXPECT_EQ(1u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(2u, m_tbu.m_arch_cmd_dptis);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_DPTI_PA,
              reg_read32(apollo_smmu_tbu::REG_ARCH_CMD_STATUS) >> 24);
    EXPECT_EQ(dpti_pa, m_tbu.m_arch_last_cmd_iova);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);

    EXPECT_EQ(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS), 2);
    EXPECT_EQ(2u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqUnconfiguredQueueSetsCerrorAbt)
{
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ABT,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS), 1);

    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdSyncAfterFailedAtcInvSetsCerrorAtcInvSync)
{
    constexpr uint64_t cmdq_base = 0x6000;
    constexpr uint64_t iova = 0x12345000;

    m_tbu.m_arch_atc_inv_sync_force_fail = true;

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_ATC_INV | (1ULL << 32));
    store_u64(cmdq_base + sizeof(uint64_t), iova);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), 0);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    const uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(1u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ATC_INV_SYNC,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_atc_invs);
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_syncs);
    EXPECT_EQ(1u, m_tbu.m_arch_atc_inv_sync_errors);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS), 2);

    EXPECT_EQ(2u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdSyncCoalescesOutstandingAtcInvFailuresAndPauses)
{
    constexpr uint64_t cmdq_base = 0x6800;
    constexpr uint64_t iova0 = 0x12345000;
    constexpr uint64_t iova1 = 0x12346000;

    m_tbu.m_arch_atc_inv_sync_force_fail_count = 2;

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_ATC_INV | (1ULL << 32));
    store_u64(cmdq_base + sizeof(uint64_t), iova0);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_ATC_INV | (2ULL << 32));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), iova1);
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), 0);
    store_u64(cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), 0);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);

    uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(2u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ATC_INV_SYNC,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(2u, m_tbu.m_arch_cmd_atc_invs);
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_syncs);
    EXPECT_EQ(1u, m_tbu.m_arch_atc_inv_sync_errors);
    EXPECT_EQ(0u, m_tbu.m_arch_atc_inv_sync_pending_count);
    EXPECT_EQ(0u, m_tbu.m_arch_atc_inv_sync_force_fail_count);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);
    cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(2u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_syncs);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS), 3);
    EXPECT_EQ(3u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);
    EXPECT_EQ(4u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_syncs);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_CMDQ_SYNC);
}

TEST_BENCH(ApolloSmmuTbuTestBench, EventQueueOverflowSetsAndAcksGerror)
{
    m_tbu.m_eventq.base = 0x7000 | 1;
    m_tbu.m_eventq.prod = 1;
    m_tbu.m_eventq.cons = 0;

    m_tbu.push_event_record("overflow", 0x1000, 0x1000);

    EXPECT_TRUE(m_tbu.m_eventq.overflow);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_NE(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_PROD)) &
                      apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS)) &
                     apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_QUEUE_OVERFLOW);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_QUEUE_OVERFLOW);

    EXPECT_EQ(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_QUEUE_OVERFLOW);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS),
                reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS)) |
                    apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_FALSE(m_tbu.m_eventq.overflow);
    EXPECT_NE(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS)) &
                      apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
}

TEST_BENCH(ApolloSmmuTbuTestBench, EventAndPriQueueOverflowFlagsToggleAndAck)
{
    m_tbu.m_eventq.base = 0x7000 | 1;
    m_tbu.m_eventq.prod = 1;
    m_tbu.m_eventq.cons = 0;

    m_tbu.push_event_record("event-overflow", 0x1000, 0x1000);

    const uint32_t eventq_prod = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_PROD));
    EXPECT_EQ(1u, eventq_prod & ~apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_NE(0u, eventq_prod & apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS)) &
                     apollo_smmu_tbu::ARCH_QUEUE_OVFLG);

    m_tbu.push_event_record("event-overflow-coalesce", 0x2000, 0x1000);
    EXPECT_EQ(eventq_prod, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_PROD)));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS),
                apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_FALSE(m_tbu.m_eventq.overflow);

    m_tbu.m_priq.base = 0x8000 | 1;
    m_tbu.m_priq.prod = 1;
    m_tbu.m_priq.cons = 0;

    m_tbu.push_pri_record(0x3000, 0x4000, 0x1000);

    const uint32_t priq_prod = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_PROD));
    EXPECT_EQ(1u, priq_prod & ~apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_NE(0u, priq_prod & apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_CONS)) &
                     apollo_smmu_tbu::ARCH_QUEUE_OVFLG);

    m_tbu.push_pri_record(0x5000, 0x6000, 0x1000);
    EXPECT_EQ(priq_prod, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_PROD)));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_CONS),
                apollo_smmu_tbu::ARCH_QUEUE_OVFLG);
    EXPECT_FALSE(m_tbu.m_priq.overflow);
}

TEST_BENCH(ApolloSmmuTbuTestBench, EventAndPriQueueWriteAbortUseArchitectedGerrorBits)
{
    m_tbu.m_eventq.base = (MEM_SIZE + 0x1000) | 1;
    m_tbu.m_eventq.prod = 0;
    m_tbu.m_eventq.cons = 0;

    m_tbu.push_event_record("event-abort", 0x1000, 0x1000);

    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_PRIQ_ABORT);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    m_tbu.m_priq.base = (MEM_SIZE + 0x2000) | 1;
    m_tbu.m_priq.prod = 0;
    m_tbu.m_priq.cons = 0;

    m_tbu.push_pri_record(0x2000, 0x3000, 0x1000);

    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_PRIQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
    EXPECT_EQ(0u, m_tbu.m_priq.prod);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_PRIQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_PRIQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedLinearStreamTableUsesSelectedStreamId)
{
    constexpr uint64_t strtab_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t ttbr = 0x4000;
    constexpr uint64_t output_base = 0x8000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t stream_id = 3;
    const uint64_t ste = arch_s1_ste(cd_base);
    const uint64_t cd = arch_valid_cd();

    stage_translation_tables(ttbr, iova, output_base);
    store_u64(strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE, ste);
    store_u64(cd_base, cd);
    store_u64(cd_base + sizeof(cd), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO, apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), 3);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(ste, m_tbu.m_arch_last_ste);
    EXPECT_EQ(cd, m_tbu.m_arch_last_cd);
    EXPECT_EQ((output_base & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova)));

    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, 8);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureStreamTableBankSelectsSecureSte)
{
    constexpr uint64_t nonsecure_strtab_base = 0x2000;
    constexpr uint64_t secure_strtab_base = 0x5000;
    constexpr uint64_t nonsecure_cd_base = 0x8000;
    constexpr uint64_t secure_cd_base = 0x9000;
    constexpr uint64_t ttbr = 0x10000;
    constexpr uint64_t output_base = 0x18000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t stream_id = 3;
    constexpr uint32_t strtab_cfg = 3;
    const uint64_t nonsecure_ste = arch_s1_ste(nonsecure_cd_base);
    const uint64_t secure_ste = arch_s1_ste(secure_cd_base);
    const uint64_t nonsecure_cd = arch_valid_cd() |
                                  (0x21ULL << apollo_smmu_tbu::ARCH_CD_ASID_SHIFT);
    const uint64_t secure_cd = arch_valid_cd() |
                               (0x22ULL << apollo_smmu_tbu::ARCH_CD_ASID_SHIFT);

    stage_translation_tables(ttbr, iova, output_base);
    store_u64(nonsecure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE,
              nonsecure_ste);
    store_u64(secure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE,
              secure_ste);
    store_u64(nonsecure_cd_base, nonsecure_cd);
    store_u64(nonsecure_cd_base + sizeof(nonsecure_cd),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(secure_cd_base, secure_cd);
    store_u64(secure_cd_base + sizeof(secure_cd),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, nonsecure_strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), strtab_cfg);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, secure_strtab_base);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), strtab_cfg);
    EXPECT_NE(0u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IDR1)) &
                      apollo_smmu_tbu::ARCH_S_IDR1_SECURE_IMPL);
    EXPECT_EQ(static_cast<uint32_t>(secure_strtab_base),
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO)));
    EXPECT_EQ(strtab_cfg,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG)));
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);

    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_SECURE;
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    const auto& secure_bank = m_tbu.arch_security_strtab_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(secure_ste, m_tbu.m_arch_last_ste);
    EXPECT_EQ(secure_cd, m_tbu.m_arch_last_cd);
    EXPECT_TRUE(secure_bank.configured);
    EXPECT_TRUE(secure_bank.valid);
    EXPECT_EQ(1u, secure_bank.lookups);
    EXPECT_EQ(stream_id, secure_bank.last_stream_id);
    EXPECT_EQ(secure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE,
              secure_bank.last_ste_pa);

    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_NONSECURE;
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(nonsecure_ste, m_tbu.m_arch_last_ste);
    EXPECT_EQ(nonsecure_cd, m_tbu.m_arch_last_cd);
    EXPECT_EQ(1u, secure_bank.lookups);
}


TEST_BENCH(ApolloSmmuTbuTestBench, SecureStage2OnlyUsesSS2TtbWhenNscfgSecure)
{
    constexpr uint64_t secure_strtab_base = 0x5000;
    constexpr uint64_t ns_s2ttb = 0xa0000;
    constexpr uint64_t secure_s2ttb = 0xb0000;
    constexpr uint64_t ns_pa = 0xc0000;
    constexpr uint64_t secure_pa = 0xd0000;
    constexpr uint64_t iova = 0x2345;
    constexpr uint32_t stream_id = 4;
    constexpr uint32_t strtab_cfg = 4;
    const uint64_t ste_pa =
        secure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(ns_s2ttb, iova, ns_pa);
    stage_translation_tables(secure_s2ttb, iova, secure_pa);
    store_u64(ste_pa, arch_s2_ste(0));
    store_u64(ste_pa + sizeof(uint64_t),
              static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_NSCFG_SECURE)
                  << apollo_smmu_tbu::ARCH_STE_NSCFG_SHIFT);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              ns_s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_S_S2TTB_WORD_OFFSET,
              secure_s2ttb & apollo_smmu_tbu::ARCH_STE_S_S2TTB_MASK);

    m_tbu.configure_arch_security_strtab_bank(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE, secure_strtab_base, strtab_cfg);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);

    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_SECURE;
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_TRUE(m_tbu.m_arch_last_s2_secure_ipa);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_NSCFG_SECURE, m_tbu.m_arch_last_nscfg);
    EXPECT_EQ(secure_s2ttb, m_tbu.m_arch_last_s_s2ttb);
    EXPECT_EQ(secure_s2ttb, m_tbu.m_arch_s2ttb);
    EXPECT_EQ((secure_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);

    store_u64(ste_pa + sizeof(uint64_t),
              static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_NSCFG_NONSECURE)
                  << apollo_smmu_tbu::ARCH_STE_NSCFG_SHIFT);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_FALSE(m_tbu.m_arch_last_s2_secure_ipa);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_NSCFG_NONSECURE, m_tbu.m_arch_last_nscfg);
    EXPECT_EQ(0u, m_tbu.m_arch_last_s_s2ttb);
    EXPECT_EQ(ns_s2ttb, m_tbu.m_arch_s2ttb);
    EXPECT_EQ((ns_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureNestedStage1OutputNsSelectsS2Ttb)
{
    constexpr uint64_t secure_strtab_base = 0x5000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t cd_pa = 0xe0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t ns_s2ttb = 0xa0000;
    constexpr uint64_t secure_s2ttb = 0xb0000;
    constexpr uint64_t ipa = 0x23000;
    constexpr uint64_t ns_pa = 0xc0000;
    constexpr uint64_t secure_pa = 0xd0000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint32_t stream_id = 5;
    constexpr uint32_t strtab_cfg = 4;
    const uint64_t ste_pa =
        secure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t expected_secure_pa =
        (secure_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));
    const uint64_t expected_ns_pa =
        (ns_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));

    stage_translation_tables(ns_s2ttb, cd_ipa, cd_pa);
    stage_translation_tables(ns_s2ttb, s1ttb, s1ttb);
    stage_translation_tables(ns_s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(ns_s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(ns_s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(secure_s2ttb, s1ttb, s1ttb);
    stage_translation_tables(secure_s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(secure_s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(secure_s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(ns_s2ttb, ipa, ns_pa);
    stage_translation_tables(secure_s2ttb, ipa, secure_pa);
    stage_translation_tables(s1ttb, iova, ipa);

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0 |
                  apollo_smmu_tbu::ARCH_STE_S2S);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              ns_s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_S_S2TTB_WORD_OFFSET,
              secure_s2ttb & apollo_smmu_tbu::ARCH_STE_S_S2TTB_MASK);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    m_tbu.configure_arch_security_strtab_bank(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE, secure_strtab_base, strtab_cfg);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);

    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_SECURE;
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_FALSE(m_tbu.m_arch_last_cd_nscfg0);
    EXPECT_FALSE(m_tbu.m_arch_last_s1_table_walk_nonsecure);
    EXPECT_FALSE(m_tbu.m_arch_last_s1_output_nonsecure_ipa);
    EXPECT_TRUE(m_tbu.m_arch_last_s1_tt_fetch_secure_ipa);
    EXPECT_EQ(secure_s2ttb, m_tbu.m_arch_last_s1_tt_fetch_s2ttb);
    EXPECT_TRUE(m_tbu.m_arch_last_s2_secure_ipa);
    EXPECT_EQ(secure_s2ttb, m_tbu.m_arch_last_s_s2ttb);
    EXPECT_EQ(secure_s2ttb, m_tbu.m_arch_s2ttb);
    EXPECT_EQ(expected_secure_pa, m_tbu.m_arch_last_pa);

    stage_translation_tables_cfg(s1ttb, iova, ipa, apollo_smmu_tbu::ARCH_GRANULE_4K,
                                 0, false, apollo_smmu_tbu::ARCH_DESC_NS);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_FALSE(m_tbu.m_arch_last_cd_nscfg0);
    EXPECT_TRUE(m_tbu.m_arch_last_s1_tt_fetch_secure_ipa);
    EXPECT_EQ(secure_s2ttb, m_tbu.m_arch_last_s1_tt_fetch_s2ttb);
    EXPECT_TRUE(m_tbu.m_arch_last_s1_output_nonsecure_ipa);
    EXPECT_FALSE(m_tbu.m_arch_last_s2_secure_ipa);
    EXPECT_EQ(0u, m_tbu.m_arch_last_s_s2ttb);
    EXPECT_EQ(ns_s2ttb, m_tbu.m_arch_s2ttb);
    EXPECT_EQ(expected_ns_pa, m_tbu.m_arch_last_pa);

    stage_translation_tables_cfg(s1ttb, iova, ipa, apollo_smmu_tbu::ARCH_GRANULE_4K,
                                 0);
    store_u64(cd_pa + sizeof(uint64_t),
              (s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK) |
                  apollo_smmu_tbu::ARCH_CD_NSCFG0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_TRUE(m_tbu.m_arch_last_cd_nscfg0);
    EXPECT_TRUE(m_tbu.m_arch_last_s1_table_walk_nonsecure);
    EXPECT_FALSE(m_tbu.m_arch_last_s1_tt_fetch_secure_ipa);
    EXPECT_EQ(ns_s2ttb, m_tbu.m_arch_last_s1_tt_fetch_s2ttb);
    EXPECT_TRUE(m_tbu.m_arch_last_s1_output_nonsecure_ipa);
    EXPECT_FALSE(m_tbu.m_arch_last_s2_secure_ipa);
    EXPECT_EQ(ns_s2ttb, m_tbu.m_arch_s2ttb);
    EXPECT_EQ(expected_ns_pa, m_tbu.m_arch_last_pa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedTwoLevelStreamTableSelectsL2Ste)
{
    constexpr uint64_t strtab_base = 0x9000;
    constexpr uint64_t l2_base = 0xa000;
    constexpr uint64_t cd_base = 0xb000;
    constexpr uint64_t ttbr = 0xc000;
    constexpr uint64_t output_base = 0x1000;
    constexpr uint64_t iova = 0x456;
    constexpr uint32_t stream_id = 0x15;
    constexpr uint32_t split = 2;
    constexpr uint32_t log2size = 6;
    const uint32_t l1_index = stream_id >> split;
    const uint32_t l2_index = stream_id & ((1u << split) - 1);
    const uint32_t strtab_cfg = (apollo_smmu_tbu::ARCH_STRTAB_FMT_2LVL
                                 << apollo_smmu_tbu::ARCH_STRTAB_CFG_FMT_SHIFT) |
                                (split << apollo_smmu_tbu::ARCH_STRTAB_CFG_SPLIT_SHIFT) |
                                log2size;
    const uint64_t ste = arch_s1_ste(cd_base);
    const uint64_t cd = arch_valid_cd();

    stage_translation_tables(ttbr, iova, output_base);
    store_u64(strtab_base + l1_index * sizeof(uint64_t), l2_base | (split + 1));
    store_u64(l2_base + l2_index * apollo_smmu_tbu::ARCH_STE_SIZE, ste);
    store_u64(cd_base, cd);
    store_u64(cd_base + sizeof(cd), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO, apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), strtab_cfg);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(ste, m_tbu.m_arch_last_ste);
    EXPECT_EQ(cd, m_tbu.m_arch_last_cd);
    EXPECT_EQ((output_base & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedContextDescriptorTableIndexesSelectedSsid)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x8000;
    constexpr uint64_t ttbr0 = 0x20000;
    constexpr uint64_t ttbr3 = 0x30000;
    constexpr uint64_t cd_l1_base = 0x40000;
    constexpr uint64_t cd_l2_base = 0x50000;
    constexpr uint64_t ttbr_l2 = 0x60000;
    constexpr uint64_t out0 = 0x70000;
    constexpr uint64_t out3 = 0x80000;
    constexpr uint64_t out_l2 = 0x90000;
    constexpr uint64_t iova = 0x1234;
    constexpr uint64_t iova_l2 = 0x2234;
    constexpr uint32_t stream_id = 4;
    constexpr uint32_t ssid = 3;
    constexpr uint32_t ssid_l2 = apollo_smmu_tbu::ARCH_CD_L2_ENTRIES + 1;
    constexpr uint32_t s1cdmax_linear = 4;
    constexpr uint32_t s1cdmax_l2 = 11;
    constexpr uint16_t asid3 = 0x33;
    constexpr uint16_t asid_l2 = 0x44;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t linear_ste = arch_s1_ste(cd_base) |
                                (static_cast<uint64_t>(s1cdmax_linear)
                                 << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT);
    const uint64_t linear_cd0 = arch_valid_cd();
    const uint64_t linear_cd3 = arch_valid_cd() |
                                (static_cast<uint64_t>(asid3)
                                 << apollo_smmu_tbu::ARCH_CD_ASID_SHIFT);
    const uint64_t l2_ste = arch_s1_ste(cd_l1_base) |
                            (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_S1FMT_64K_L2)
                             << apollo_smmu_tbu::ARCH_STE_S1FMT_SHIFT) |
                            (static_cast<uint64_t>(s1cdmax_l2)
                             << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT);
    const uint64_t l2_cd = arch_valid_cd() |
                           (static_cast<uint64_t>(asid_l2)
                            << apollo_smmu_tbu::ARCH_CD_ASID_SHIFT);

    stage_translation_tables(ttbr0, iova, out0);
    stage_translation_tables(ttbr3, iova, out3);
    stage_translation_tables(ttbr_l2, iova_l2, out_l2);
    store_u64(ste_pa, linear_ste);
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(cd_base, linear_cd0);
    store_u64(cd_base + sizeof(uint64_t), ttbr0 & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(cd_base + ssid * apollo_smmu_tbu::ARCH_CD_SIZE, linear_cd3);
    store_u64(cd_base + ssid * apollo_smmu_tbu::ARCH_CD_SIZE + sizeof(uint64_t),
              ttbr3 & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, (1u << 31) | ssid);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(linear_cd3, m_tbu.m_arch_last_cd);
    EXPECT_EQ(ssid, m_tbu.m_arch_last_cd_ssid);
    EXPECT_EQ(s1cdmax_linear, m_tbu.m_arch_last_s1cdmax);
    EXPECT_FALSE(m_tbu.m_arch_last_cd_l2);
    EXPECT_EQ((out3 & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova), asid3, 0,
                                 true, ssid));
    uint32_t detail = reg_read32(apollo_smmu_tbu::REG_ARCH_CD_DETAIL);
    EXPECT_EQ(ssid, detail & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(s1cdmax_linear,
              (detail >> 22) & apollo_smmu_tbu::ARCH_STE_S1CDMAX_MASK);
    EXPECT_NE(0u, detail & (1u << 30));

    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(linear_cd0, m_tbu.m_arch_last_cd);
    EXPECT_EQ(0u, m_tbu.m_arch_last_cd_ssid);
    EXPECT_FALSE(m_tbu.m_arch_current_ssid_valid);
    EXPECT_EQ((out0 & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);

    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_TERMINATE);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STREAM_DISABLED,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_S1DSS_TERMINATE, m_tbu.m_arch_last_s1dss);

    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_BYPASS);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(iova, m_tbu.m_arch_last_pa);
    EXPECT_EQ(0u, m_tbu.m_arch_last_cd);
    EXPECT_TRUE(m_tbu.m_arch_last_cd_bypass);
    detail = reg_read32(apollo_smmu_tbu::REG_ARCH_CD_DETAIL);
    EXPECT_NE(0u, detail & (1u << 31));

    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, (1u << 31) | (1u << s1cdmax_linear));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_SUBSTREAMID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));

    store_u64(ste_pa, l2_ste);
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(cd_l1_base + sizeof(uint64_t),
              apollo_smmu_tbu::ARCH_CD_L1_DESC_VALID | cd_l2_base);
    store_u64(cd_l2_base + sizeof(uint64_t) * 8, 0);
    store_u64(cd_l2_base + (ssid_l2 % apollo_smmu_tbu::ARCH_CD_L2_ENTRIES) *
                  apollo_smmu_tbu::ARCH_CD_SIZE,
              l2_cd);
    store_u64(cd_l2_base + (ssid_l2 % apollo_smmu_tbu::ARCH_CD_L2_ENTRIES) *
                  apollo_smmu_tbu::ARCH_CD_SIZE + sizeof(uint64_t),
              ttbr_l2 & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI,
                iova_l2);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, (1u << 31) | ssid_l2);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(l2_cd, m_tbu.m_arch_last_cd);
    EXPECT_EQ(ssid_l2, m_tbu.m_arch_last_cd_ssid);
    EXPECT_TRUE(m_tbu.m_arch_last_cd_l2);
    EXPECT_EQ((out_l2 & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova_l2 & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova_l2), asid_l2, 0,
                                 true, ssid_l2));
    detail = reg_read32(apollo_smmu_tbu::REG_ARCH_CD_DETAIL);
    EXPECT_EQ(ssid_l2, detail & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_S1FMT_64K_L2,
              (detail >> 27) & apollo_smmu_tbu::ARCH_STE_S1FMT_MASK);
    EXPECT_NE(0u, detail & (1u << 29));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedSteCdReservedEncodingFaults)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x8000;
    constexpr uint64_t ttbr = 0x20000;
    constexpr uint64_t cd_l1_base = 0x40000;
    constexpr uint64_t cd_l2_base = 0x50000;
    constexpr uint64_t out = 0x70000;
    constexpr uint64_t iova = 0x1234;
    constexpr uint32_t stream_id = 4;
    constexpr uint32_t ssid_l2 = apollo_smmu_tbu::ARCH_CD_L2_ENTRIES;
    constexpr uint32_t s1cdmax = 11;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t valid_ste = arch_s1_ste(cd_base) |
                               (1ULL << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT);
    const uint64_t valid_cd = arch_valid_cd();
    const auto expect_fault = [this](uint32_t reason, uint32_t stage) {
        EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
                  reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
        EXPECT_EQ(reason, reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
        EXPECT_EQ(stage, m_tbu.m_arch_fault_stage);
    };

    stage_translation_tables(ttbr, iova, out);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, 0);

    store_u64(ste_pa, apollo_smmu_tbu::ARCH_STE_VALID |
                          (0x3ULL << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT) |
                          (cd_base & apollo_smmu_tbu::ARCH_STE_S1CTXPTR_MASK));
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(cd_base, valid_cd);
    store_u64(cd_base + sizeof(uint64_t), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);
    expect_fault(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
                 apollo_smmu_tbu::ARCH_FAULT_STAGE_S1);

    store_u64(ste_pa, valid_ste | (1ULL << 58));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);
    expect_fault(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
                 apollo_smmu_tbu::ARCH_FAULT_STAGE_S1);

    store_u64(ste_pa, valid_ste);
    store_u64(ste_pa + sizeof(uint64_t), 0x3);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);
    expect_fault(apollo_smmu_tbu::ARCH_FAULT_CD_INVALID,
                 apollo_smmu_tbu::ARCH_FAULT_STAGE_S1);

    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(cd_base, valid_cd | (1ULL << 32));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);
    expect_fault(apollo_smmu_tbu::ARCH_FAULT_CD_INVALID,
                 apollo_smmu_tbu::ARCH_FAULT_STAGE_S1);

    store_u64(cd_base, valid_cd);
    store_u64(cd_base + sizeof(uint64_t),
              (ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK) | (1ULL << 3));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);
    expect_fault(apollo_smmu_tbu::ARCH_FAULT_CD_INVALID,
                 apollo_smmu_tbu::ARCH_FAULT_STAGE_S1);

    store_u64(ste_pa, arch_s1_ste(cd_l1_base) |
                          (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_S1FMT_64K_L2)
                           << apollo_smmu_tbu::ARCH_STE_S1FMT_SHIFT) |
                          (static_cast<uint64_t>(s1cdmax)
                           << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT));
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(cd_l1_base + sizeof(uint64_t),
              apollo_smmu_tbu::ARCH_CD_L1_DESC_VALID | cd_l2_base | (1ULL << 63));
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, (1u << 31) | ssid_l2);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);
    expect_fault(apollo_smmu_tbu::ARCH_FAULT_CD_INVALID,
                 apollo_smmu_tbu::ARCH_FAULT_STAGE_S1);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedWalkerGranuleBlockAndFaultMatrix)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr16 = 0x40000;
    constexpr uint64_t ttbr64 = 0xc0000;
    constexpr uint64_t ttbr_block = 0x60000;
    constexpr uint64_t ttbr_fault = 0x80000;
    constexpr uint64_t iova16 = 0x12345;
    constexpr uint64_t iova64 = 0x23456;
    constexpr uint64_t iova_block = 0x1a3456;
    constexpr uint64_t iova_fault = 0x234;
    constexpr uint64_t out16 = 0x140000;
    constexpr uint64_t out64 = 0x100000;
    constexpr uint64_t out_block = 0x400000;
    constexpr uint64_t out_fault = 0x180000;
    constexpr uint32_t stream_id = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base);

    store_u64(ste_pa, ste);
    store_u64(cd_base, arch_valid_cd(apollo_smmu_tbu::ARCH_GRANULE_16K, 1));
    store_u64(cd_base + sizeof(uint64_t), ttbr16 & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    stage_translation_tables_cfg(ttbr16, iova16, out16, apollo_smmu_tbu::ARCH_GRANULE_16K, 1);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova16);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(3u, reg_read32(apollo_smmu_tbu::REG_ARCH_LEVELS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, m_tbu.m_arch_last_stage);
    EXPECT_EQ((out16 & apollo_smmu_tbu::arch_output_mask(apollo_smmu_tbu::ARCH_GRANULE_16K)) |
                  (iova16 & apollo_smmu_tbu::arch_granule_mask(apollo_smmu_tbu::ARCH_GRANULE_16K)),
              m_tbu.m_arch_last_pa);

    store_u64(cd_base, arch_valid_cd(apollo_smmu_tbu::ARCH_GRANULE_64K, 1));
    store_u64(cd_base + sizeof(uint64_t), ttbr64 & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    stage_translation_tables_cfg(ttbr64, iova64, out64, apollo_smmu_tbu::ARCH_GRANULE_64K, 1);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova64);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(2u, reg_read32(apollo_smmu_tbu::REG_ARCH_LEVELS));
    EXPECT_EQ((out64 & apollo_smmu_tbu::arch_output_mask(apollo_smmu_tbu::ARCH_GRANULE_64K)) |
                  (iova64 & apollo_smmu_tbu::arch_granule_mask(apollo_smmu_tbu::ARCH_GRANULE_64K)),
              m_tbu.m_arch_last_pa);

    store_u64(cd_base, arch_valid_cd(apollo_smmu_tbu::ARCH_GRANULE_4K));
    store_u64(cd_base + sizeof(uint64_t), ttbr_block & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    stage_translation_tables_cfg(ttbr_block, iova_block, out_block,
                                 apollo_smmu_tbu::ARCH_GRANULE_4K, 0, true);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI,
                iova_block);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ((out_block & ~((1ULL << 21) - 1)) | (iova_block & ((1ULL << 21) - 1)),
              m_tbu.m_arch_last_pa);

    stage_translation_tables(ttbr_fault, iova_fault, out_fault);
    const uint64_t l3_table = ttbr_fault + 3 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l3_index = apollo_smmu_tbu::arch_level_index(iova_fault, 3);
    store_u64(cd_base + sizeof(uint64_t), ttbr_fault & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(l3_table + l3_index * sizeof(uint64_t),
              (out_fault & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  apollo_smmu_tbu::ARCH_DESC_PAGE);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI,
                iova_fault);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_ACCESS,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));

    store_u64(l3_table + l3_index * sizeof(uint64_t),
              (out_fault & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  apollo_smmu_tbu::ARCH_DESC_AF |
                  apollo_smmu_tbu::ARCH_DESC_AP_RO |
                  apollo_smmu_tbu::ARCH_DESC_PAGE);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE_WRITE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) &
                      (apollo_smmu_tbu::ARCH_FAULT_ATTR_WRITE << 20));
}

TEST_BENCH(ApolloSmmuTbuTestBench, CdE0pdBlocksUnprivilegedTtb0Access)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x40000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint64_t output_base = 0x90000;
    constexpr uint32_t stream_id = 0x61;
    constexpr uint32_t payload = 0xe0ad1234u;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              (ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK) |
                  apollo_smmu_tbu::ARCH_CD_E0PD0);
    stage_translation_tables(ttbr, iova, output_base);
    store_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)), payload);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed));
    EXPECT_TRUE(m_tbu.m_arch_last_e0pd_fault);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PAGE_INVALID,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, m_tbu.m_arch_fault_stage);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_access(stream_id, iova, true, observed));
    EXPECT_FALSE(m_tbu.m_arch_last_e0pd_fault);
    EXPECT_EQ(payload, observed);
}

TEST_BENCH(ApolloSmmuTbuTestBench, PtwnncNormalizesNestedStage1FetchDeviceMemory)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t cd_pa = 0xe0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t ipa = 0x23000;
    constexpr uint64_t output_base = 0xc0000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint32_t stream_id = 0x62;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    /*
     * stage_translation_tables() leaves stage-2 MemAttr[3:0] at zero,
     * modelling Device-nGnRnE for the stage-1 CD/TT fetch mappings.  With
     * IDR3.PTWNNC advertised, the TBU must not reject those table walks; it
     * records the architected Normal Non-cacheable normalization instead.
     */
    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    stage_translation_tables(s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, ipa, output_base);
    stage_translation_tables(s1ttb, iova, ipa);

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_TRUE(m_tbu.m_arch_last_ptwnnc_device_fetch);
    EXPECT_TRUE(m_tbu.m_arch_last_ptwnnc_normalized);
    EXPECT_EQ((output_base & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, S2ptwBlocksNestedCdFetchDeviceMemory)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t cd_pa = 0xe0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint32_t stream_id = 0x6a;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              (s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK) |
                  apollo_smmu_tbu::ARCH_STE_S2PTW);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_TRUE(m_tbu.m_arch_last_ptwnnc_device_fetch);
    EXPECT_TRUE(m_tbu.m_arch_last_s2ptw_fault);
    EXPECT_FALSE(m_tbu.m_arch_last_ptwnnc_normalized);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2,
              m_tbu.m_arch_fault_stage);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_CD,
              m_tbu.m_arch_fault_event_class);
    EXPECT_EQ(cd_ipa, m_tbu.m_arch_last_ipa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, S2ptwBlocksNestedTtFetchDeviceMemory)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t cd_pa = 0xe0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t ipa = 0x23000;
    constexpr uint64_t output_base = 0xc0000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint32_t stream_id = 0x6b;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t first_s1_desc_ipa =
        s1ttb + apollo_smmu_tbu::arch_level_index(iova, 0) * sizeof(uint64_t);
    const uint64_t cd_s2_l3_desc_pa =
        s2ttb + 3 * apollo_smmu_tbu::PAGE_SIZE +
        apollo_smmu_tbu::arch_level_index(cd_ipa, 3) * sizeof(uint64_t);
    const uint64_t s2_normal_nc =
        static_cast<uint64_t>(apollo_smmu_tbu::ARCH_DESC_S2_MEMATTR_NORMAL_NC)
        << apollo_smmu_tbu::ARCH_DESC_S2_MEMATTR_SHIFT;

    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    store_u64(cd_s2_l3_desc_pa, load_u64(cd_s2_l3_desc_pa) | s2_normal_nc);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    stage_translation_tables(s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, ipa, output_base);
    stage_translation_tables(s1ttb, iova, ipa);

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              (s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK) |
                  apollo_smmu_tbu::ARCH_STE_S2PTW);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_TRUE(m_tbu.m_arch_last_ptwnnc_device_fetch);
    EXPECT_TRUE(m_tbu.m_arch_last_s2ptw_fault);
    EXPECT_FALSE(m_tbu.m_arch_last_ptwnnc_normalized);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2,
              m_tbu.m_arch_fault_stage);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_TT,
              m_tbu.m_arch_fault_event_class);
    EXPECT_EQ(first_s1_desc_ipa, m_tbu.m_arch_last_ipa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CdHadDisablesHierarchicalStage1Attrs)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x40000;
    constexpr uint64_t iova = 0x4567;
    constexpr uint64_t output_base = 0x91000;
    constexpr uint32_t stream_id = 0x63;
    constexpr uint32_t payload = 0x0ad01234u;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t l0_index = apollo_smmu_tbu::arch_level_index(iova, 0);
    const uint64_t l0_desc_pa = ttbr + l0_index * sizeof(uint64_t);
    uint32_t observed = 0;

    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    stage_translation_tables(ttbr, iova, output_base);
    store_u64(l0_desc_pa,
              load_u64(l0_desc_pa) |
                  apollo_smmu_tbu::ARCH_DESC_APTABLE_NO_UNPRIV);
    store_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)), payload);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed));
    EXPECT_TRUE(m_tbu.m_arch_last_hier_attrs_applied);
    EXPECT_FALSE(m_tbu.m_arch_last_had_disabled_hier_attrs);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S1,
              m_tbu.m_arch_fault_stage);

    store_u64(cd_base + sizeof(uint64_t),
              (ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK) |
                  apollo_smmu_tbu::ARCH_CD_HAD0);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed));
    EXPECT_TRUE(m_tbu.m_arch_last_had0);
    EXPECT_TRUE(m_tbu.m_arch_last_had_disabled_hier_attrs);
    EXPECT_FALSE(m_tbu.m_arch_last_hier_attrs_applied);
    EXPECT_EQ(payload, observed);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Idr3XnxBlocksUnprivilegedStage2Execute)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t s2ttb = 0x50000;
    constexpr uint64_t iova = 0x5678;
    constexpr uint64_t output_base = 0x92000;
    constexpr uint32_t stream_id = 0x64;
    constexpr uint32_t payload = 0x1d3f0123u;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t l3_table = s2ttb + 3 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l3_index = apollo_smmu_tbu::arch_level_index(iova, 3);
    const uint64_t l3_desc_pa = l3_table + l3_index * sizeof(uint64_t);
    uint32_t observed = 0;

    store_u64(ste_pa, arch_s2_ste(s2ttb));
    store_u64(ste_pa + sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    stage_translation_tables(s2ttb, iova, output_base);
    store_u64(l3_desc_pa,
              load_u64(l3_desc_pa) | apollo_smmu_tbu::ARCH_DESC_UXN);
    store_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)), payload);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed, true));
    EXPECT_TRUE(m_tbu.m_arch_last_xnx_fault);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2,
              m_tbu.m_arch_fault_stage);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed, false));
    EXPECT_FALSE(m_tbu.m_arch_last_xnx_fault);
    EXPECT_EQ(payload, observed);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Idr3Bbml2IgnoresBlockNt)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x60000;
    constexpr uint64_t iova = 0x1a3456;
    constexpr uint64_t output_base = 0x0;
    constexpr uint64_t block_mask = (1ULL << 21) - 1;
    constexpr uint32_t stream_id = 0x65;
    constexpr uint32_t payload = 0xbb120123u;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    stage_translation_tables_cfg(ttbr, iova, output_base,
                                 apollo_smmu_tbu::ARCH_GRANULE_4K, 0,
                                 true, apollo_smmu_tbu::ARCH_DESC_NT);
    store_u64((output_base & ~block_mask) | (iova & block_mask), payload);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed));
    EXPECT_TRUE(m_tbu.m_arch_last_bbml2_nt_ignored);
    EXPECT_NE(0u, m_tbu.m_arch_last_bbml2_nt_desc &
                     apollo_smmu_tbu::ARCH_DESC_NT);
    EXPECT_NE(apollo_smmu_tbu::ARCH_FAULT_TLB_CONFLICT,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ((output_base & ~block_mask) | (iova & block_mask),
              m_tbu.m_arch_last_pa);
    EXPECT_EQ(payload, observed);
}

TEST_BENCH(ApolloSmmuTbuTestBench, HttuStage1AccessAndDirtyUpdatesLeaf)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x68000;
    constexpr uint64_t iova = 0x6789;
    constexpr uint64_t output_base = 0x93000;
    constexpr uint32_t stream_id = 0x66;
    constexpr uint32_t read_payload = 0xa11d0123u;
    constexpr uint32_t write_payload = 0xd1720123u;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t l3_table = ttbr + 3 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l3_index = apollo_smmu_tbu::arch_level_index(iova, 3);
    const uint64_t l3_desc_pa = l3_table + l3_index * sizeof(uint64_t);
    uint32_t observed = 0;

    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(cd_base,
              arch_valid_cd() | apollo_smmu_tbu::ARCH_CD_HA |
                  apollo_smmu_tbu::ARCH_CD_HD);
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    stage_translation_tables(ttbr, iova, output_base);
    store_u64(l3_desc_pa, load_u64(l3_desc_pa) & ~apollo_smmu_tbu::ARCH_DESC_AF);
    store_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              read_payload);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed));
    EXPECT_TRUE(m_tbu.m_arch_last_httu_af_update);
    EXPECT_FALSE(m_tbu.m_arch_last_httu_dirty_update);
    EXPECT_NE(0u, load_u64(l3_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_EQ(read_payload, observed);

    store_u64(l3_desc_pa,
              load_u64(l3_desc_pa) | apollo_smmu_tbu::ARCH_DESC_AP_RO |
                  apollo_smmu_tbu::ARCH_DESC_DBM);
    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_write32(stream_id, iova, write_payload));
    EXPECT_TRUE(m_tbu.m_arch_last_httu_dirty_update);
    EXPECT_EQ(0u, load_u64(l3_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AP_RO);
    EXPECT_NE(0u, load_u64(l3_desc_pa) & apollo_smmu_tbu::ARCH_DESC_DBM);
    EXPECT_EQ(write_payload,
              load_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1))) &
                  0xffffffffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, HttuStage2AccessAndDirtyUpdatesLeaf)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t s2ttb = 0x78000;
    constexpr uint64_t iova = 0x789a;
    constexpr uint64_t output_base = 0x94000;
    constexpr uint32_t stream_id = 0x67;
    constexpr uint32_t read_payload = 0xa22d0123u;
    constexpr uint32_t write_payload = 0xd2720123u;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t l3_table = s2ttb + 3 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l3_index = apollo_smmu_tbu::arch_level_index(iova, 3);
    const uint64_t l3_desc_pa = l3_table + l3_index * sizeof(uint64_t);
    uint32_t observed = 0;

    store_u64(ste_pa, arch_s2_ste(s2ttb));
    store_u64(ste_pa + sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              (s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK) |
                  apollo_smmu_tbu::ARCH_STE_S2HA |
                  apollo_smmu_tbu::ARCH_STE_S2HD);
    stage_translation_tables(s2ttb, iova, output_base);
    store_u64(l3_desc_pa, load_u64(l3_desc_pa) & ~apollo_smmu_tbu::ARCH_DESC_AF);
    store_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              read_payload);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed));
    EXPECT_TRUE(m_tbu.m_arch_last_httu_af_update);
    EXPECT_FALSE(m_tbu.m_arch_last_httu_dirty_update);
    EXPECT_NE(0u, load_u64(l3_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_EQ(read_payload, observed);

    store_u64(l3_desc_pa,
              load_u64(l3_desc_pa) | apollo_smmu_tbu::ARCH_DESC_AP_RO |
                  apollo_smmu_tbu::ARCH_DESC_DBM);
    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_write32(stream_id, iova, write_payload));
    EXPECT_TRUE(m_tbu.m_arch_last_httu_dirty_update);
    EXPECT_EQ(0u, load_u64(l3_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AP_RO);
    EXPECT_NE(0u, load_u64(l3_desc_pa) & apollo_smmu_tbu::ARCH_DESC_DBM);
    EXPECT_EQ(write_payload,
              load_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1))) &
                  0xffffffffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, HttuHaftStage1UpdatesTableAccessFlag)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x88000;
    constexpr uint64_t iova = 0x89ab;
    constexpr uint64_t output_base = 0x98000;
    constexpr uint32_t stream_id = 0x68;
    constexpr uint32_t payload = 0xaf710123u;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t l1_table = ttbr + apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l2_table = ttbr + 2 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l0_desc_pa =
        ttbr + apollo_smmu_tbu::arch_level_index(iova, 0) * sizeof(uint64_t);
    const uint64_t l1_desc_pa =
        l1_table + apollo_smmu_tbu::arch_level_index(iova, 1) * sizeof(uint64_t);
    const uint64_t l2_desc_pa =
        l2_table + apollo_smmu_tbu::arch_level_index(iova, 2) * sizeof(uint64_t);
    uint32_t observed = 0;

    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(cd_base, arch_valid_cd() | apollo_smmu_tbu::ARCH_CD_HA);
    store_u64(cd_base + sizeof(uint64_t),
              (ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK) |
                  apollo_smmu_tbu::ARCH_CD_HAFT);
    stage_translation_tables(ttbr, iova, output_base);
    store_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              payload);

    EXPECT_EQ(0u, load_u64(l0_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_EQ(0u, load_u64(l1_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_EQ(0u, load_u64(l2_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed));
    EXPECT_TRUE(m_tbu.m_arch_last_httu_af_update);
    EXPECT_TRUE(m_tbu.m_arch_last_httu_table_af_update);
    EXPECT_FALSE(m_tbu.m_arch_last_httu_dirty_update);
    EXPECT_NE(0u, load_u64(l0_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_NE(0u, load_u64(l1_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_NE(0u, load_u64(l2_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_NE(0u, m_tbu.m_arch_last_httu_desc_after &
                     apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_EQ(payload, observed);
}

TEST_BENCH(ApolloSmmuTbuTestBench, HttuHaftStage2UpdatesTableAccessFlag)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t s2ttb = 0x90000;
    constexpr uint64_t iova = 0x9abc;
    constexpr uint64_t output_base = 0xa0000;
    constexpr uint32_t stream_id = 0x69;
    constexpr uint32_t payload = 0xaf720123u;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t l1_table = s2ttb + apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l2_table = s2ttb + 2 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l0_desc_pa =
        s2ttb + apollo_smmu_tbu::arch_level_index(iova, 0) * sizeof(uint64_t);
    const uint64_t l1_desc_pa =
        l1_table + apollo_smmu_tbu::arch_level_index(iova, 1) * sizeof(uint64_t);
    const uint64_t l2_desc_pa =
        l2_table + apollo_smmu_tbu::arch_level_index(iova, 2) * sizeof(uint64_t);
    uint32_t observed = 0;

    store_u64(ste_pa, arch_s2_ste(s2ttb));
    store_u64(ste_pa + sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              (s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK) |
                  apollo_smmu_tbu::ARCH_STE_S2HA |
                  apollo_smmu_tbu::ARCH_STE_S2HAFT);
    stage_translation_tables(s2ttb, iova, output_base);
    store_u64(output_base + (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              payload);

    EXPECT_EQ(0u, load_u64(l0_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_EQ(0u, load_u64(l1_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_EQ(0u, load_u64(l2_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_access(stream_id, iova, false, observed));
    EXPECT_TRUE(m_tbu.m_arch_last_httu_af_update);
    EXPECT_TRUE(m_tbu.m_arch_last_httu_table_af_update);
    EXPECT_FALSE(m_tbu.m_arch_last_httu_dirty_update);
    EXPECT_NE(0u, load_u64(l0_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_NE(0u, load_u64(l1_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_NE(0u, load_u64(l2_desc_pa) & apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_NE(0u, m_tbu.m_arch_last_httu_desc_after &
                     apollo_smmu_tbu::ARCH_DESC_AF);
    EXPECT_EQ(payload, observed);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedWalkerStage2AndNestedMatrix)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t s1ttb = 0x40000;
    constexpr uint64_t s2ttb = 0x60000;
    constexpr uint64_t nested_s1ttb = 0x80000;
    constexpr uint64_t nested_s2ttb = 0xa0000;
    constexpr uint64_t nested_cd_pa = 0xe0000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint64_t ipa = 0x23000;
    constexpr uint64_t stage2_pa = 0x90000;
    constexpr uint64_t nested_pa = 0xb0000;
    constexpr uint32_t stream_id = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(s2ttb, iova, stage2_pa);
    store_u64(ste_pa, arch_s2_ste(s2ttb));
    store_u64(ste_pa + sizeof(uint64_t), s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_last_stage);
    EXPECT_EQ((stage2_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);

    stage_translation_tables(nested_s2ttb, cd_base, nested_cd_pa);
    stage_translation_tables(nested_s2ttb, nested_s1ttb, nested_s1ttb);
    stage_translation_tables(nested_s2ttb, nested_s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             nested_s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(nested_s2ttb, nested_s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             nested_s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(nested_s2ttb, nested_s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             nested_s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(nested_s1ttb, iova, ipa);
    stage_translation_tables(nested_s2ttb, ipa, nested_pa);
    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t), cd_base);
    store_u64(ste_pa + 2 * sizeof(uint64_t), nested_s2ttb);
    store_u64(nested_cd_pa, arch_valid_cd());
    store_u64(nested_cd_pa + sizeof(uint64_t),
              nested_s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ((ipa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              reg_read32(apollo_smmu_tbu::REG_ARCH_IPA_LO));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_last_stage);
    EXPECT_EQ((nested_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (m_tbu.m_arch_last_ipa & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);

    constexpr uint64_t bypass_s2ttb = 0xc0000;
    constexpr uint64_t bypass_iova = 0x4567;
    constexpr uint64_t bypass_pa = 0xd0000;
    stage_translation_tables(bypass_s2ttb, bypass_iova, bypass_pa);
    store_u64(ste_pa, arch_nested_ste() |
                        (1ULL << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT));
    store_u64(ste_pa + sizeof(uint64_t),
              cd_base | apollo_smmu_tbu::ARCH_STE_S1DSS_BYPASS);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              bypass_s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI,
                bypass_iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_TRUE(m_tbu.m_arch_last_cd_bypass);
    EXPECT_EQ(0u, m_tbu.m_arch_last_cd);
    EXPECT_EQ(bypass_iova, m_tbu.m_arch_last_ipa);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_last_stage);
    EXPECT_EQ((bypass_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (bypass_iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_CD_DETAIL) & (1u << 31));

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t), cd_base);
    store_u64(ste_pa + 2 * sizeof(uint64_t), nested_s2ttb);
    store_u64(nested_cd_pa, arch_valid_cd());
    store_u64(nested_cd_pa + sizeof(uint64_t),
              nested_s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, 0);

    const uint64_t nested_l3 = nested_s2ttb + 3 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t nested_l3_index = apollo_smmu_tbu::arch_level_index(ipa, 3);
    store_u64(nested_l3 + nested_l3_index * sizeof(uint64_t), 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PAGE_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_fault_stage);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SteBypassOutputAttributesPropagateOnContextBypass)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x30000;
    constexpr uint64_t iova = 0x15800;
    constexpr uint32_t stream_id = 0x51;
    constexpr uint32_t payload = 0x51a77a5u;
    constexpr uint8_t memattr = 0xf;
    constexpr uint8_t shcfg = 0x3;
    constexpr uint8_t alloccfg = 0xa;
    constexpr uint8_t instcfg = 0x2;
    constexpr uint8_t privcfg = 0x3;
    constexpr uint8_t nscfg = apollo_smmu_tbu::ARCH_STE_NSCFG_SECURE;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    bool observed_attrs_valid = false;
    bool observed_mtcfg = false;
    uint8_t observed_mem_type = 0;
    uint8_t observed_shareability = 0;
    uint8_t observed_alloc_hint = 0;
    uint8_t observed_inst_cfg = 0;
    uint8_t observed_priv_cfg = 0;
    uint8_t observed_ns_cfg = 0;
    uint64_t observed_addr = 0;
    uint32_t observed = 0;

    store_u64(iova, payload);
    store_u64(ste_pa,
              arch_s1_ste(cd_base) |
                  (1ULL << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT));
    store_u64(ste_pa + sizeof(uint64_t),
              apollo_smmu_tbu::ARCH_STE_S1DSS_BYPASS |
                  arch_ste_output_attrs(true, memattr, shcfg, alloccfg, instcfg,
                                        privcfg, nscfg));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == iova && ext != nullptr) {
            observed_addr = addr;
            observed_attrs_valid = ext->output_attrs_valid;
            observed_mtcfg = ext->output_mtcfg;
            observed_mem_type = ext->output_mem_type;
            observed_shareability = ext->output_shareability;
            observed_alloc_hint = ext->output_alloc_hint;
            observed_inst_cfg = ext->output_inst_cfg;
            observed_priv_cfg = ext->output_priv_cfg;
            observed_ns_cfg = ext->output_ns_cfg;
        }
        return mem_read(addr, data, len);
    });

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(m_tbu.m_arch_last_cd_bypass);
    EXPECT_TRUE(observed_attrs_valid);
    EXPECT_TRUE(observed_mtcfg);
    EXPECT_EQ(memattr, observed_mem_type);
    EXPECT_EQ(shcfg, observed_shareability);
    EXPECT_EQ(alloccfg, observed_alloc_hint);
    EXPECT_EQ(instcfg, observed_inst_cfg);
    EXPECT_EQ(privcfg, observed_priv_cfg);
    EXPECT_EQ(nscfg, observed_ns_cfg);

    observed_attrs_valid = false;
    observed_mtcfg = true;
    observed_mem_type = 0xff;
    store_u64(ste_pa + sizeof(uint64_t),
              apollo_smmu_tbu::ARCH_STE_S1DSS_BYPASS |
                  arch_ste_output_attrs(false, memattr, shcfg, alloccfg,
                                        instcfg, privcfg, nscfg));

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_TRUE(observed_attrs_valid);
    EXPECT_FALSE(observed_mtcfg);
    EXPECT_EQ(0u, observed_mem_type);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SteConfigBypassOutputAttributesPropagate)
{
    constexpr uint64_t ste_base = 0x24000;
    constexpr uint64_t iova = 0x16800;
    constexpr uint32_t stream_id = 0x52;
    constexpr uint32_t payload = 0x52b77b6u;
    constexpr uint8_t memattr = 0x0;
    constexpr uint8_t shcfg = 0x3;
    constexpr uint8_t alloccfg = 0x5;
    constexpr uint8_t instcfg = 0x3;
    constexpr uint8_t privcfg = 0x2;
    constexpr uint8_t nscfg = apollo_smmu_tbu::ARCH_STE_NSCFG_NONSECURE;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    bool observed_attrs_valid = false;
    bool observed_mtcfg = false;
    uint8_t observed_mem_type = 0xff;
    uint8_t observed_shareability = 0;
    uint8_t observed_alloc_hint = 0;
    uint8_t observed_inst_cfg = 0;
    uint8_t observed_priv_cfg = 0;
    uint8_t observed_ns_cfg = 0;
    uint64_t observed_addr = 0;
    uint32_t observed = 0;

    store_u64(iova, payload);
    store_u64(ste_pa,
              apollo_smmu_tbu::ARCH_STE_VALID |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_BYPASS)
                   << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_output_attrs(true, memattr, shcfg, alloccfg, instcfg,
                                    privcfg, nscfg));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == iova && ext != nullptr) {
            observed_addr = addr;
            observed_attrs_valid = ext->output_attrs_valid;
            observed_mtcfg = ext->output_mtcfg;
            observed_mem_type = ext->output_mem_type;
            observed_shareability = ext->output_shareability;
            observed_alloc_hint = ext->output_alloc_hint;
            observed_inst_cfg = ext->output_inst_cfg;
            observed_priv_cfg = ext->output_priv_cfg;
            observed_ns_cfg = ext->output_ns_cfg;
        }
        return mem_read(addr, data, len);
    });

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_EQ(iova, observed_addr);
    EXPECT_EQ(iova, m_tbu.m_arch_last_ipa);
    EXPECT_EQ(0u, m_tbu.m_arch_last_cd);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_NONE, m_tbu.m_arch_last_stage);
    EXPECT_TRUE(observed_attrs_valid);
    EXPECT_TRUE(observed_mtcfg);
    EXPECT_EQ(0u, observed_mem_type);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_SHCFG_OSH, observed_shareability);
    EXPECT_EQ(alloccfg, observed_alloc_hint);
    EXPECT_EQ(instcfg, observed_inst_cfg);
    EXPECT_EQ(privcfg, observed_priv_cfg);
    EXPECT_EQ(nscfg, observed_ns_cfg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedAtsPriProtocolMatrixAndPriResp)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t ttbr = 0x4000;
    constexpr uint64_t output_base = 0x8000;
    constexpr uint64_t priq_base = 0x9000;
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t stream_id = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base);
    const uint64_t cd = arch_valid_cd();

    stage_translation_tables(ttbr, iova, output_base);
    store_u64(ste_pa, ste);
    store_u64(cd_base, cd);
    store_u64(cd_base + sizeof(cd), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO, apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI,
                priq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 3);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_success);
    EXPECT_EQ(0u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(0u, m_tbu.m_arch_ats_ca);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(apollo_smmu_tbu::arch_priq_ppr_word0(stream_id, false, true,
                                                   false, true, false, false),
              load_u64(priq_base));
    EXPECT_EQ(iova, load_u64(priq_base + 8));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS, load_u64(priq_base + 16) >> 56);
    const uint16_t success_prg = m_tbu.m_arch_last_prg;
    EXPECT_EQ(success_prg, load_u64(priq_base + 24) >> 48);

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_PRI_RESP);
    store_u64(cmdq_base + sizeof(uint64_t), success_prg);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_rejected);

    store_u64(ste_pa, 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_UR,
              load_u64(priq_base + apollo_smmu_tbu::ARCH_PRIQ_ENTRY_BYTES + 16) >> 56);
    const uint16_t fault_prg = m_tbu.m_arch_last_prg;

    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t),
              fault_prg |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT) << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(2u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_rejected);

    store_u64(ste_pa, ste);
    store_u64(ttbr, 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_ca);
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_ATS_DETAIL));
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_PRI_DETAIL));
}

TEST_BENCH(ApolloSmmuTbuTestBench, PriProtocolPprFieldsStopMarkerAndNonLastDiscard)
{
    constexpr uint64_t priq_base = 0x9000;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t ssid = 0x345;
    constexpr uint64_t iova = 0x123000;
    constexpr uint64_t pa = 0x823000;
    constexpr uint16_t prg = 0x55;

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    m_tbu.m_priq.base = priq_base | 2;
    m_tbu.m_priq.prod = 0;
    m_tbu.m_priq.cons = 0;

    m_tbu.push_pri_protocol_record(stream_id, prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR, iova, pa,
                                   apollo_smmu_tbu::PAGE_SIZE, true, ssid, true,
                                   false, false, false, false);

    EXPECT_EQ(1u, m_tbu.m_arch_pri_stop_markers);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(0u, m_tbu.m_priq.prod);
    EXPECT_EQ(apollo_smmu_tbu::arch_priq_ppr_word0(stream_id, true, true,
                                                   false, false, false, false),
              m_tbu.m_arch_last_pri_ppr_word0);

    m_tbu.push_pri_protocol_record(stream_id, prg + 1,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS,
                                   iova + apollo_smmu_tbu::PAGE_SIZE,
                                   pa + apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE, true, ssid, true,
                                   true, false, false, true);

    EXPECT_EQ(1u, m_tbu.m_priq.prod);
    EXPECT_EQ(apollo_smmu_tbu::arch_priq_ppr_word0(stream_id, true, true,
                                                   false, true, false, true),
              load_u64(priq_base));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS,
              load_u64(priq_base + 16) >> 56);
    EXPECT_EQ(prg + 1, load_u64(priq_base + 24) >> 48);
    EXPECT_EQ(ssid, (load_u64(priq_base + 24) >>
                     apollo_smmu_tbu::ARCH_PRIQ_PPR_SSID_SHIFT) &
                        apollo_smmu_tbu::ARCH_PRIQ_PPR_SSID_MASK);

    m_tbu.m_priq.base = priq_base | 1;
    m_tbu.m_priq.prod = 1;
    m_tbu.m_priq.cons = 0;
    m_tbu.push_pri_protocol_record(stream_id, prg + 2,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                   pa + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE, false, 0, false,
                                   true, false, false, false);

    EXPECT_EQ(1u, m_tbu.m_arch_pri_discarded_nonlast);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_TRUE(m_tbu.m_priq.overflow);
}

TEST_BENCH(ApolloSmmuTbuTestBench, PriProtocolPprIgnoresAtschkAndSteEats)
{
    constexpr uint64_t priq_base = 0x9800;
    constexpr uint64_t ste_base = 0x28000;
    constexpr uint64_t cd_base = 0x30000;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t ssid = 0x456;
    constexpr uint64_t iova = 0x128000;
    constexpr uint64_t pa = 0x828000;
    const uint64_t ste_pa =
        ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    m_tbu.m_arch_ste_base = ste_base;
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    m_tbu.m_priq.base = priq_base | 3;
    m_tbu.m_priq.prod = 0;
    m_tbu.m_priq.cons = 0;
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                ARCH_CR0_ALL_QUEUES & ~apollo_smmu_tbu::ARCH_CR0_ATSCHK);

    const uint16_t atschk_clear_prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR, true, ssid);
    ASSERT_NE(0u, atschk_clear_prg);
    m_tbu.push_pri_protocol_record(stream_id, atschk_clear_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE,
                                   true, ssid, true, true, false, false,
                                   false);

    EXPECT_EQ(1u, m_tbu.m_priq.prod);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_ste_ppar_checks);
    EXPECT_EQ(atschk_clear_prg, load_u64(priq_base + 24) >> 48);
    EXPECT_EQ(ssid, (load_u64(priq_base + 24) >>
                     apollo_smmu_tbu::ARCH_PRIQ_PPR_SSID_SHIFT) &
                        apollo_smmu_tbu::ARCH_PRIQ_PPR_SSID_MASK);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                ARCH_CR0_ALL_QUEUES);
    const uint16_t eats_disabled_prg =
        m_tbu.allocate_prg(stream_id, iova + apollo_smmu_tbu::PAGE_SIZE,
                           apollo_smmu_tbu::ARCH_ATS_RESP_CA);
    ASSERT_NE(0u, eats_disabled_prg);
    m_tbu.push_pri_protocol_record(stream_id, eats_disabled_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_CA,
                                   iova + apollo_smmu_tbu::PAGE_SIZE,
                                   pa + apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE);

    EXPECT_EQ(2u, m_tbu.m_priq.prod);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_ste_ppar_checks);
    EXPECT_EQ(2u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(eats_disabled_prg,
              load_u64(priq_base + apollo_smmu_tbu::ARCH_PRIQ_ENTRY_BYTES +
                       24) >> 48);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_CA,
              load_u64(priq_base + apollo_smmu_tbu::ARCH_PRIQ_ENTRY_BYTES +
                       16) >> 56);
}

TEST_BENCH(ApolloSmmuTbuTestBench, PriProtocolPrgIndexIsNineBitsAndWraps)
{
    constexpr uint64_t priq_base = 0x9c00;
    constexpr uint64_t cmdq_base = 0x9d00;
    constexpr uint32_t stream_id = 1;
    constexpr uint64_t iova = 0x140000;
    constexpr uint64_t pa = 0x840000;

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    m_tbu.m_priq.base = priq_base | 3;
    m_tbu.m_priq.prod = 0;
    m_tbu.m_priq.cons = 0;
    m_tbu.m_arch_next_prg = apollo_smmu_tbu::ARCH_PRIQ_PPR_PRG_MASK;

    const uint16_t last_prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS);
    const uint16_t wrapped_prg =
        m_tbu.allocate_prg(stream_id, iova + apollo_smmu_tbu::PAGE_SIZE,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    ASSERT_EQ(apollo_smmu_tbu::ARCH_PRIQ_PPR_PRG_MASK, last_prg);
    ASSERT_EQ(1u, wrapped_prg);
    EXPECT_EQ(2u, m_tbu.arch_pri_pending_count());

    m_tbu.push_pri_protocol_record(stream_id, last_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    EXPECT_EQ(last_prg, (load_u64(priq_base + 24) >> 48) &
                            apollo_smmu_tbu::ARCH_PRIQ_PPR_PRG_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
                            (static_cast<uint64_t>(stream_id) << 32));
    store_u64(cmdq_base + sizeof(uint64_t),
              (last_prg | (1u << 9)) |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT)
                   << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_EQ(last_prg, m_tbu.m_arch_last_prg);
    EXPECT_EQ(stream_id,
              m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_TRUE(m_tbu.pri_prg_pending(wrapped_prg));
}

TEST_BENCH(ApolloSmmuTbuTestBench, PriProtocolAutoRespondsOnOverflowDisabledAndAbort)
{
    constexpr uint64_t priq_base = 0x9000;
    constexpr uint32_t stream_id = 1;
    constexpr uint64_t iova = 0x123000;
    constexpr uint64_t pa = 0x823000;

    m_tbu.m_priq.base = priq_base | 1;
    m_tbu.m_priq.prod = 1;
    m_tbu.m_priq.cons = 0;

    const uint16_t overflow_prg =
        m_tbu.allocate_prg(stream_id, iova, apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    ASSERT_NE(0u, overflow_prg);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());

    m_tbu.push_pri_protocol_record(stream_id, overflow_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE);

    EXPECT_EQ(1u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_failures);
    EXPECT_EQ(overflow_prg, m_tbu.m_arch_last_auto_prg);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT,
              m_tbu.m_arch_last_auto_response);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_priq.prod);
    EXPECT_TRUE(m_tbu.m_priq.overflow);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_QUEUE_OVERFLOW);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN |
                    apollo_smmu_tbu::ARCH_CR0_CMDQEN |
                    apollo_smmu_tbu::ARCH_CR0_ATSCHK);
    m_tbu.m_priq.base = priq_base | 2;
    m_tbu.m_priq.prod = 0;
    m_tbu.m_priq.cons = 0;
    const uint16_t disabled_prg =
        m_tbu.allocate_prg(stream_id, iova + apollo_smmu_tbu::PAGE_SIZE,
                           apollo_smmu_tbu::ARCH_ATS_RESP_CA);

    m_tbu.push_pri_protocol_record(stream_id, disabled_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_CA,
                                   iova + apollo_smmu_tbu::PAGE_SIZE,
                                   pa + apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE);

    EXPECT_EQ(2u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_auto_failures);
    EXPECT_EQ(disabled_prg, m_tbu.m_arch_last_auto_prg);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(0u, m_tbu.m_priq.prod);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    m_tbu.set_arch_gerror(apollo_smmu_tbu::ARCH_GERROR_PRIQ_ABORT);
    const uint16_t abort_prg =
        m_tbu.allocate_prg(stream_id, iova + 2 * apollo_smmu_tbu::PAGE_SIZE,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);

    m_tbu.push_pri_protocol_record(stream_id, abort_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                   pa + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE);

    EXPECT_EQ(3u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(2u, m_tbu.m_arch_pri_auto_failures);
    EXPECT_EQ(abort_prg, m_tbu.m_arch_last_auto_prg);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_FAILURE,
              m_tbu.m_arch_last_auto_response);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(0u, m_tbu.m_priq.prod);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_PRIQ_ABORT);
}

TEST_BENCH(ApolloSmmuTbuTestBench, PriProtocolOverflowUsesStePparForPasidAutoResponse)
{
    constexpr uint64_t priq_base = 0xa000;
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x30000;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t ssid = 0x12345;
    constexpr uint64_t iova = 0x456000;
    constexpr uint64_t pa = 0x856000;
    constexpr uint64_t ste_pa =
        ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste0 = arch_s1_ste(cd_base);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    m_tbu.m_arch_ste_base = ste_base;
    store_u64(ste_pa, ste0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    auto force_priq_overflow = [&]() {
        m_tbu.m_priq.base = priq_base | 1;
        m_tbu.m_priq.prod = 1;
        m_tbu.m_priq.cons = 0;
        m_tbu.m_priq.overflow = false;
    };

    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_PPAR);
    force_priq_overflow();
    const uint16_t ppar_prg =
        m_tbu.allocate_prg(stream_id, iova, apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    m_tbu.push_pri_protocol_record(stream_id, ppar_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE,
                                   true, ssid);

    EXPECT_EQ(1u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_failures);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_auto_ste_ppar_checks);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_ste_ppar_failures);
    EXPECT_TRUE(m_tbu.m_arch_last_pri_ppar);
    EXPECT_FALSE(m_tbu.m_arch_last_pri_pps);
    EXPECT_TRUE(m_tbu.m_arch_last_auto_response_ssv);
    EXPECT_EQ(ssid, m_tbu.m_arch_last_auto_response_ssid);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT,
              m_tbu.m_arch_last_auto_response);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());

    store_u64(ste_pa + sizeof(uint64_t), 0);
    force_priq_overflow();
    const uint16_t no_ppar_prg =
        m_tbu.allocate_prg(stream_id, iova + apollo_smmu_tbu::PAGE_SIZE,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    m_tbu.push_pri_protocol_record(stream_id, no_ppar_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova + apollo_smmu_tbu::PAGE_SIZE,
                                   pa + apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE, true, ssid);

    EXPECT_EQ(2u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_auto_failures);
    EXPECT_EQ(2u, m_tbu.m_arch_pri_auto_ste_ppar_checks);
    EXPECT_FALSE(m_tbu.m_arch_last_pri_ppar);
    EXPECT_FALSE(m_tbu.m_arch_last_auto_response_ssv);
    EXPECT_EQ(0u, m_tbu.m_arch_last_auto_response_ssid);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT,
              m_tbu.m_arch_last_auto_response);

    store_u64(ste_pa, 0);
    force_priq_overflow();
    const uint16_t invalid_ste_prg =
        m_tbu.allocate_prg(stream_id, iova + 2 * apollo_smmu_tbu::PAGE_SIZE,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    m_tbu.push_pri_protocol_record(stream_id, invalid_ste_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                   pa + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE, true, ssid);

    EXPECT_EQ(3u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_auto_failures);
    EXPECT_EQ(3u, m_tbu.m_arch_pri_auto_ste_ppar_checks);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_auto_ste_ppar_failures);
    EXPECT_FALSE(m_tbu.m_arch_last_auto_response_ssv);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_FAILURE,
              m_tbu.m_arch_last_auto_response);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
}

TEST_BENCH(ApolloSmmuTbuTestBench, PriProtocolSecureStreamAutoFailsWithoutQueueing)
{
    constexpr uint64_t secure_priq_base = 0xa800;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t ssid = 0x223;
    constexpr uint64_t iova = 0x789000;
    constexpr uint64_t pa = 0x889000;

    reg_s_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI,
                  secure_priq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_SECURE;

    const uint16_t prg =
        m_tbu.allocate_prg(stream_id, iova, apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    ASSERT_NE(0u, prg);
    m_tbu.push_pri_protocol_record(stream_id, prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE,
                                   true, ssid);

    EXPECT_EQ(1u, m_tbu.m_arch_pri_auto_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_auto_failures);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_secure_auto_failures);
    EXPECT_EQ(prg, m_tbu.m_arch_last_auto_prg);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_FAILURE,
              m_tbu.m_arch_last_auto_response);
    EXPECT_FALSE(m_tbu.m_arch_last_auto_response_ssv);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(0u, m_tbu.m_arch_secure_priq.prod);
    EXPECT_EQ(0u, load_u64(secure_priq_base));
}

TEST_BENCH(ApolloSmmuTbuTestBench, PriProtocolPparLookupFaultHonorsRecCfgAts)
{
    constexpr uint64_t priq_base = 0xb000;
    constexpr uint64_t eventq_base = 0xb800;
    constexpr uint64_t ste_base = 0x21000;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t ssid = 0x334;
    constexpr uint64_t iova = 0x990000;
    constexpr uint64_t pa = 0x990000;
    constexpr uint64_t ste_pa =
        ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                eventq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);
    m_tbu.m_arch_ste_base = ste_base;
    store_u64(ste_pa, 0);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    auto force_priq_overflow = [&]() {
        m_tbu.m_priq.base = priq_base | 1;
        m_tbu.m_priq.prod = 1;
        m_tbu.m_priq.cons = 0;
        m_tbu.m_priq.overflow = false;
    };

    force_priq_overflow();
    const uint16_t recorded_prg =
        m_tbu.allocate_prg(stream_id, iova, apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    m_tbu.push_pri_protocol_record(stream_id, recorded_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE,
                                   true, ssid);

    EXPECT_EQ(1u, m_tbu.m_arch_pri_ppar_lookup_fault_records);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_FAILURE,
              m_tbu.m_arch_last_auto_response);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2), 0);
    m_tbu.m_eventq.prod = 0;
    m_tbu.m_eventq.cons = 0;
    m_tbu.m_eventq.overflow = false;
    force_priq_overflow();
    const uint16_t suppressed_prg =
        m_tbu.allocate_prg(stream_id, iova + apollo_smmu_tbu::PAGE_SIZE,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    m_tbu.push_pri_protocol_record(stream_id, suppressed_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova + apollo_smmu_tbu::PAGE_SIZE,
                                   pa + apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE, true, ssid);

    EXPECT_EQ(1u, m_tbu.m_arch_pri_ppar_lookup_fault_records);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_TRUE(m_tbu.m_arch_fault_record_suppressed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_FAILURE,
              m_tbu.m_arch_last_auto_response);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
}

TEST_BENCH(ApolloSmmuTbuTestBench, PriProtocolPparBadStreamIdHonorsRecInvsid)
{
    constexpr uint64_t priq_base = 0xc000;
    constexpr uint64_t eventq_base = 0xc800;
    constexpr uint64_t strtab_base = 0xd000;
    constexpr uint32_t bad_stream_id = 4;
    constexpr uint32_t ssid = 0x445;
    constexpr uint32_t one_bit_strtab = 1;
    constexpr uint64_t iova = 0xaa0000;
    constexpr uint64_t pa = 0xaa0000;

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                one_bit_strtab);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    auto force_priq_overflow = [&]() {
        m_tbu.m_priq.base = priq_base | 1;
        m_tbu.m_priq.prod = 1;
        m_tbu.m_priq.cons = 0;
        m_tbu.m_priq.overflow = false;
    };

    force_priq_overflow();
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);
    const uint16_t suppressed_prg =
        m_tbu.allocate_prg(bad_stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    m_tbu.push_pri_protocol_record(bad_stream_id, suppressed_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE,
                                   true, ssid);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_ppar_lookup_fault_records);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_TRUE(m_tbu.m_arch_fault_record_suppressed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_FAILURE,
              m_tbu.m_arch_last_auto_response);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());

    store_u64(eventq_base, 0);
    store_u64(eventq_base + sizeof(uint64_t), 0);
    store_u64(eventq_base + 2 * sizeof(uint64_t), 0);
    store_u64(eventq_base + 3 * sizeof(uint64_t), 0);
    m_tbu.m_arch_fault_record_suppressed = false;
    m_tbu.m_eventq.prod = 0;
    m_tbu.m_eventq.cons = 0;
    m_tbu.m_eventq.overflow = false;
    force_priq_overflow();
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS |
                    apollo_smmu_tbu::ARCH_CR2_RECINVSID);
    const uint16_t recorded_prg =
        m_tbu.allocate_prg(bad_stream_id,
                           iova + apollo_smmu_tbu::PAGE_SIZE,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    m_tbu.push_pri_protocol_record(bad_stream_id, recorded_prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_UR,
                                   iova + apollo_smmu_tbu::PAGE_SIZE,
                                   pa + apollo_smmu_tbu::PAGE_SIZE,
                                   apollo_smmu_tbu::PAGE_SIZE, true, ssid);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_ppar_lookup_fault_records);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_FALSE(m_tbu.m_arch_fault_record_suppressed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STREAMID,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(bad_stream_id,
              static_cast<uint32_t>(load_u64(eventq_base) >> 32));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_FAILURE,
              m_tbu.m_arch_last_auto_response);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslationRequestHonorsCr0AtschkAndSteEats)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t ttbr = 0x4000;
    constexpr uint64_t output_base = 0x8000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t priq_base = 0xa000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t stream_id = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base);
    const uint64_t cd = arch_valid_cd();
    const uint32_t cr0_without_atschk =
        apollo_smmu_tbu::ARCH_CR0_SMMUEN |
        apollo_smmu_tbu::ARCH_CR0_PRIQEN |
        apollo_smmu_tbu::ARCH_CR0_EVENTQEN |
        apollo_smmu_tbu::ARCH_CR0_CMDQEN;

    stage_translation_tables(ttbr, iova, output_base);
    store_u64(ste_pa, ste);
    store_u64(cd_base, cd);
    store_u64(cd_base + sizeof(cd), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO, apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO, apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI,
                priq_base | 3);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), cr0_without_atschk);
    store_u64(ste_pa + sizeof(uint64_t), arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_ATS_TREQ,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_DISABLED, m_tbu.m_arch_last_eats);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_BAD_ATS_TREQ, load_u64(eventq_base) & 0xffu);
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova)));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    store_u64(ste_pa + sizeof(uint64_t), arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_DISABLED));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_ATS_TREQ,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(2u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_BAD_ATS_TREQ,
              load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES) & 0xffu);

    store_u64(ste_pa + sizeof(uint64_t), arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_DPT));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_ATS_TREQ,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, apollo_smmu_tbu::ARCH_IDR3 & apollo_smmu_tbu::ARCH_IDR3_DPT);
    EXPECT_EQ(3u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_DISABLED, m_tbu.m_arch_last_eats);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_BAD_ATS_TREQ,
              load_u64(eventq_base + 2 * apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES) & 0xffu);

    store_u64(ste_pa + sizeof(uint64_t), arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_success);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_FULL, m_tbu.m_arch_last_eats);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova)));
    EXPECT_EQ((output_base & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslationRequestTranslationFaultReturnsSuccessNoEvent)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t ttbr = 0x4000;
    constexpr uint64_t output_base = 0x8000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t priq_base = 0xa000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t stream_id = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base);
    const uint64_t cd = arch_valid_cd();
    const uint64_t l3_table = ttbr + 3 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l3_index = apollo_smmu_tbu::arch_level_index(iova, 3);
    const uint64_t leaf_pa = l3_table + l3_index * sizeof(uint64_t);

    stage_translation_tables(ttbr, iova, output_base);
    store_u64(leaf_pa, load_u64(leaf_pa) & ~apollo_smmu_tbu::ARCH_DESC_AF);
    store_u64(ste_pa, ste);
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL));
    store_u64(cd_base, cd);
    store_u64(cd_base + sizeof(cd), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI, priq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_ACCESS,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_success);
    EXPECT_EQ(0u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(0u, m_tbu.m_arch_ats_ca);
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base));
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova)));
    EXPECT_EQ(0u, m_tbu.m_arch_last_pa);
    EXPECT_TRUE(m_tbu.m_arch_fault_record_suppressed);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslationRequestWriteIntentUpdatesHttuDirty)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t ttbr = 0x4000;
    constexpr uint64_t output_base = 0x8000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t priq_base = 0xa000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t stream_id = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t l3_table = ttbr + 3 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l3_index = apollo_smmu_tbu::arch_level_index(iova, 3);
    const uint64_t leaf_pa = l3_table + l3_index * sizeof(uint64_t);

    stage_translation_tables(ttbr, iova, output_base);
    store_u64(leaf_pa,
              load_u64(leaf_pa) |
                  apollo_smmu_tbu::ARCH_DESC_AP_RO |
                  apollo_smmu_tbu::ARCH_DESC_DBM);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL));
    store_u64(cd_base,
              arch_valid_cd() |
                  apollo_smmu_tbu::ARCH_CD_HA |
                  apollo_smmu_tbu::ARCH_CD_HD);
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI, priq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST_WRITE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_TRUE(m_tbu.m_arch_last_ats_treq_write);
    EXPECT_TRUE(m_tbu.m_arch_last_httu_dirty_update);
    EXPECT_EQ(0u, load_u64(leaf_pa) & apollo_smmu_tbu::ARCH_DESC_AP_RO);
    EXPECT_NE(0u, load_u64(leaf_pa) & apollo_smmu_tbu::ARCH_DESC_DBM);
    EXPECT_EQ(1u, m_tbu.m_arch_ats_success);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS, load_u64(priq_base + 16) >> 56);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova)));

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL, apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    store_u64(leaf_pa,
              load_u64(leaf_pa) |
                  apollo_smmu_tbu::ARCH_DESC_AP_RO |
                  apollo_smmu_tbu::ARCH_DESC_DBM);
    store_u64(cd_base, arch_valid_cd() | apollo_smmu_tbu::ARCH_CD_HA);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST_WRITE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_TRUE(m_tbu.m_arch_last_ats_treq_write);
    EXPECT_FALSE(m_tbu.m_arch_last_httu_dirty_update);
    EXPECT_NE(0u, load_u64(leaf_pa) & apollo_smmu_tbu::ARCH_DESC_AP_RO);
    EXPECT_EQ(2u, m_tbu.m_arch_ats_success);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_TRUE(m_tbu.m_arch_fault_record_suppressed);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslationRequestConfigFaultsUseArchitectedResponses)
{
    constexpr uint64_t strtab_base = 0x2000;
    constexpr uint64_t ste_base = 0x3000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t priq_base = 0xa000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t bad_stream_id = 4;
    constexpr uint32_t abort_stream_id = 1;
    constexpr uint32_t one_bit_strtab = 1;
    const uint64_t abort_ste_pa =
        ste_base + abort_stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI, priq_base | 3);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS |
                    apollo_smmu_tbu::ARCH_CR2_RECINVSID);

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                one_bit_strtab);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, bad_stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_ca);
    EXPECT_EQ(0u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_CA, load_u64(priq_base + 16) >> 56);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STREAMID, load_u64(eventq_base) & 0xffu);

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL, apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, abort_stream_id);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    store_u64(abort_ste_pa,
              apollo_smmu_tbu::ARCH_STE_VALID |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_ABORT)
                   << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT));
    store_u64(abort_ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL));
    store_u64(abort_ste_pa + 2 * sizeof(uint64_t), 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_ATS_TREQ,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_ca);
    EXPECT_EQ(1u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_UR,
              load_u64(priq_base + apollo_smmu_tbu::ARCH_PRIQ_ENTRY_BYTES + 16) >> 56);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_BAD_ATS_TREQ,
              load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslationRequestSubstreamConfigFaultsReturnCa)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t priq_base = 0xa000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t s1cdmax = 2;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base) |
                         (static_cast<uint64_t>(s1cdmax)
                          << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT);

    store_u64(ste_pa, ste);
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL) |
                  apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI, priq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);

    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID,
                (1u << 31) | (1u << s1cdmax));
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_SUBSTREAMID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_arch_ats_ca);
    EXPECT_EQ(0u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_CA, load_u64(priq_base + 16) >> 56);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_SUBSTREAMID,
              load_u64(eventq_base) & 0xffu);

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL, apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, 1u << 31);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STREAM_DISABLED,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(2u, m_tbu.m_arch_ats_ca);
    EXPECT_EQ(0u, m_tbu.m_arch_ats_ur);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_CA,
              load_u64(priq_base + apollo_smmu_tbu::ARCH_PRIQ_ENTRY_BYTES + 16) >> 56);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_STREAM_DISABLED,
              load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedTransactionForbiddenRecordsEvent)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t iova = 0x12000;
    constexpr uint64_t pa = 0x82000;
    constexpr uint32_t stream_id = 0x2a;
    constexpr uint32_t value = 0xa55a1234;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base);
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, value);
    store_u64(ste_pa, ste);
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_DISABLED));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO, apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSL_FORBIDDEN,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(load_u64(eventq_base) >> 32));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);

    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL));
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(value, observed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_FULL, m_tbu.m_arch_last_eats);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedAtschkDisabledUsesGbpmpamAttributes)
{
    constexpr uint64_t iova = 0x12800;
    constexpr uint64_t pa = 0x82800;
    constexpr uint32_t stream_id = 0x57;
    constexpr uint16_t partid = 0x1d;
    constexpr uint8_t pmg = 0x4;
    constexpr uint32_t payload = 0x0badf00d;
    constexpr uint32_t gbpmpam_value =
        (static_cast<uint32_t>(pmg) << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        partid;
    bool observed_mpam_valid = false;
    bool observed_mpam_remapped = true;
    bool observed_mpam_unknown = true;
    uint16_t observed_partid = 0;
    uint8_t observed_pmg = 0;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, payload);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | gbpmpam_value);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == pa && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_mpam_remapped = ext->mpam_remapped;
            observed_mpam_unknown = ext->mpam_unknown;
            observed_partid = ext->mpam_partid;
            observed_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_FALSE(observed_mpam_remapped);
    EXPECT_FALSE(observed_mpam_unknown);
    EXPECT_EQ(partid, observed_partid);
    EXPECT_EQ(pmg, observed_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedSteMpamAttributesPropagateWhenAtschkEnabled)
{
    constexpr uint64_t ste_base = 0x22000;
    constexpr uint64_t cd_base = 0x26000;
    constexpr uint64_t iova = 0x13800;
    constexpr uint64_t pa = 0x83800;
    constexpr uint32_t stream_id = 0x58;
    constexpr uint16_t partid = 0x1e;
    constexpr uint8_t pmg = 0x5;
    constexpr uint32_t payload = 0x13572468;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    bool observed_mpam_valid = false;
    bool observed_mpam_remapped = true;
    bool observed_mpam_unknown = true;
    uint16_t observed_partid = 0;
    uint8_t observed_pmg = 0;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, payload);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(partid) << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              static_cast<uint64_t>(pmg) << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_ATSCHK);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == pa && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_mpam_remapped = ext->mpam_remapped;
            observed_mpam_unknown = ext->mpam_unknown;
            observed_partid = ext->mpam_partid;
            observed_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_FALSE(observed_mpam_remapped);
    EXPECT_FALSE(observed_mpam_unknown);
    EXPECT_EQ(partid, observed_partid);
    EXPECT_EQ(pmg, observed_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench,
           AtsTranslatedSteOutputAttributesPropagateWhenAtschkEnabled)
{
    constexpr uint64_t ste_base = 0x28000;
    constexpr uint64_t cd_base = 0x2c000;
    constexpr uint64_t iova = 0x17800;
    constexpr uint64_t pa = 0x87800;
    constexpr uint32_t stream_id = 0x5a;
    constexpr uint32_t payload = 0x5aa77aa5;
    constexpr uint8_t memattr = 0xf;
    constexpr uint8_t shcfg = 0x3;
    constexpr uint8_t alloccfg = 0x6;
    constexpr uint8_t instcfg = 0x2;
    constexpr uint8_t privcfg = 0x3;
    constexpr uint8_t nscfg = apollo_smmu_tbu::ARCH_STE_NSCFG_SECURE;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    bool observed_attrs_valid = false;
    bool observed_mtcfg = false;
    uint8_t observed_mem_type = 0;
    uint8_t observed_shareability = 0;
    uint8_t observed_alloc_hint = 0;
    uint8_t observed_inst_cfg = 0;
    uint8_t observed_priv_cfg = 0;
    uint8_t observed_ns_cfg = 0;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, payload);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL) |
                  arch_ste_output_attrs(true, memattr, shcfg, alloccfg,
                                        instcfg, privcfg, nscfg));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_ATSCHK);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == pa && ext != nullptr) {
            observed_attrs_valid = ext->output_attrs_valid;
            observed_mtcfg = ext->output_mtcfg;
            observed_mem_type = ext->output_mem_type;
            observed_shareability = ext->output_shareability;
            observed_alloc_hint = ext->output_alloc_hint;
            observed_inst_cfg = ext->output_inst_cfg;
            observed_priv_cfg = ext->output_priv_cfg;
            observed_ns_cfg = ext->output_ns_cfg;
        }
        return mem_read(addr, data, len);
    });

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_attrs_valid);
    EXPECT_TRUE(observed_mtcfg);
    EXPECT_EQ(memattr, observed_mem_type);
    EXPECT_EQ(shcfg, observed_shareability);
    EXPECT_EQ(alloccfg, observed_alloc_hint);
    EXPECT_EQ(instcfg, observed_inst_cfg);
    EXPECT_EQ(privcfg, observed_priv_cfg);
    EXPECT_EQ(nscfg, observed_ns_cfg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, GatosParReportsSteOutputAttributes)
{
    constexpr uint64_t ste_base = 0x30000;
    constexpr uint64_t cd_base = 0x34000;
    constexpr uint64_t ttbr = 0x38000;
    constexpr uint64_t iova = 0x18800;
    constexpr uint64_t pa = 0x98800;
    constexpr uint32_t stream_id = 0x5b;
    constexpr uint8_t memattr = 0xf;
    constexpr uint8_t shcfg = 0x3;
    constexpr uint8_t alloccfg = 0x5;
    constexpr uint8_t instcfg = 0x2;
    constexpr uint8_t privcfg = 0x3;
    constexpr uint8_t nscfg = apollo_smmu_tbu::ARCH_STE_NSCFG_SECURE;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(ttbr, iova, pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_output_attrs(true, memattr, shcfg, alloccfg,
                                    instcfg, privcfg, nscfg));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_GATOS_TRANSLATE);

    const uint64_t par =
        static_cast<uint64_t>(reg_read32(apollo_smmu_tbu::REG_ARCH_PAR_LO)) |
        (static_cast<uint64_t>(reg_read32(apollo_smmu_tbu::REG_ARCH_PAR_HI)) << 32);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_EQ(memattr, (par >> 56) & 0xffu);
    EXPECT_EQ(shcfg, (par >> 8) & 0x3u);
    EXPECT_EQ(par, m_tbu.m_arch_last_par);
    EXPECT_TRUE(m_tbu.m_arch_last_output_attrs_valid);
    EXPECT_EQ(memattr, m_tbu.m_arch_last_output_mem_type);
    EXPECT_EQ(shcfg, m_tbu.m_arch_last_output_shareability);
}

TEST_BENCH(ApolloSmmuTbuTestBench, GatosParFaultCodeForUnmappedPage)
{
    constexpr uint64_t ste_base = 0x3c000;
    constexpr uint64_t cd_base = 0x40000;
    constexpr uint64_t ttbr = 0x44000;
    constexpr uint64_t iova = 0x19800;
    constexpr uint32_t stream_id = 0x5c;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_GATOS_TRANSLATE);

    const uint64_t par =
        static_cast<uint64_t>(reg_read32(apollo_smmu_tbu::REG_ARCH_PAR_LO)) |
        (static_cast<uint64_t>(reg_read32(apollo_smmu_tbu::REG_ARCH_PAR_HI)) << 32);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(1u, par & 0x1u);
    EXPECT_EQ(0x10u, (par >> 4) & 0xffu);
    EXPECT_EQ(0u, (par >> 1) & 0x3u);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Smmuv3GatosRegistersRunAndClear)
{
    constexpr uint64_t ste_base = 0x48000;
    constexpr uint64_t cd_base = 0x4c000;
    constexpr uint64_t ttbr = 0x50000;
    constexpr uint64_t iova = 0x1a000;
    constexpr uint64_t pa = 0x9a800;
    constexpr uint32_t stream_id = 0x5d;
    const uint64_t atos = atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1);
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(ttbr, iova, pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI, atos);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);

    const uint64_t par =
        static_cast<uint64_t>(reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);

    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL)) &
                     apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_EQ(stream_id, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO)));
    EXPECT_EQ(atos, m_tbu.m_arch_gatos_addr);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Smmuv3GatosAddrAccessFieldsIgnoreSteOverrides)
{
    constexpr uint64_t ste_base = 0x52000;
    constexpr uint64_t cd_base = 0x56000;
    constexpr uint64_t ttbr = 0x5a000;
    constexpr uint64_t iova = 0x1a800;
    constexpr uint64_t pa = 0x9ac00;
    constexpr uint32_t stream_id = 0x5f;
    constexpr uint8_t memattr = 0x7;
    constexpr uint8_t shcfg = 0x1;
    constexpr uint8_t alloccfg = 0x6;
    constexpr uint8_t instcfg = 0x1;
    constexpr uint8_t privcfg = 0x2;
    constexpr uint8_t nscfg = apollo_smmu_tbu::ARCH_STE_NSCFG_NONSECURE;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t atos =
        atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1, true,
                  true, true);

    stage_translation_tables(ttbr, iova, pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_output_attrs(true, memattr, shcfg, alloccfg,
                                    instcfg, privcfg, nscfg));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI, atos);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);

    const uint64_t par =
        static_cast<uint64_t>(reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);

    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_EQ(0xffu, (par >> 56) & 0xffu);
    EXPECT_EQ(0x3u, (par >> 8) & 0x3u);
    EXPECT_TRUE(m_tbu.m_arch_last_output_attrs_valid);
    EXPECT_EQ(memattr, m_tbu.m_arch_last_output_mem_type);
    EXPECT_EQ(shcfg, m_tbu.m_arch_last_output_shareability);
    EXPECT_TRUE(m_tbu.m_arch_last_atos_privileged);
    EXPECT_TRUE(m_tbu.m_arch_last_atos_instruction);
    EXPECT_TRUE(m_tbu.m_arch_last_atos_read);
    EXPECT_TRUE(m_tbu.m_arch_last_atos_ste_attrs_ignored);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Smmuv3GatosFaultDoesNotRecordEvent)
{
    constexpr uint64_t ste_base = 0x54000;
    constexpr uint64_t cd_base = 0x58000;
    constexpr uint64_t ttbr = 0x5c000;
    constexpr uint64_t iova = 0x1b000;
    constexpr uint32_t stream_id = 0x5e;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);

    const uint64_t par =
        static_cast<uint64_t>(reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);

    EXPECT_EQ(1u, par & 0x1u);
    EXPECT_EQ(0x10u, (par >> 4) & 0xffu);
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureSmmuv3GatosRegistersUseSecureBank)
{
    constexpr uint64_t nonsecure_strtab_base = 0x60000;
    constexpr uint64_t secure_strtab_base = 0x64000;
    constexpr uint64_t nonsecure_cd_base = 0x68000;
    constexpr uint64_t secure_cd_base = 0x6c000;
    constexpr uint64_t nonsecure_ttbr = 0x70000;
    constexpr uint64_t secure_ttbr = 0x76000;
    constexpr uint64_t iova = 0x1c000;
    constexpr uint64_t nonsecure_pa = 0xba800;
    constexpr uint64_t secure_pa = 0xaa800;
    constexpr uint32_t stream_id = 3;
    constexpr uint32_t strtab_cfg = 3;
    const uint64_t atos = atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1);
    const uint64_t secure_sid =
        stream_id | apollo_smmu_tbu::ARCH_ATOS_SID_SECURE_STREAM;
    const uint64_t nonsecure_ste = arch_s1_ste(nonsecure_cd_base);
    const uint64_t secure_ste = arch_s1_ste(secure_cd_base);

    stage_translation_tables(nonsecure_ttbr, iova, nonsecure_pa);
    stage_translation_tables(secure_ttbr, iova, secure_pa);
    store_u64(nonsecure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE,
              nonsecure_ste);
    store_u64(secure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE,
              secure_ste);
    store_u64(nonsecure_cd_base, arch_valid_cd());
    store_u64(nonsecure_cd_base + sizeof(uint64_t),
              nonsecure_ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(secure_cd_base, arch_valid_cd());
    store_u64(secure_cd_base + sizeof(uint64_t),
              secure_ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                nonsecure_strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), strtab_cfg);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                  secure_strtab_base);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                strtab_cfg);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                  apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, secure_sid);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                  apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI, atos);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);

    const uint64_t par =
        static_cast<uint64_t>(
            reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    const auto& secure_bank = m_tbu.arch_security_strtab_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE);

    EXPECT_EQ(0u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL)) &
                     apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(secure_pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_NE(nonsecure_pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_EQ(secure_ste, m_tbu.m_arch_last_ste);
    EXPECT_EQ(atos, m_tbu.m_arch_secure_gatos_addr);
    EXPECT_TRUE(secure_bank.configured);
    EXPECT_TRUE(secure_bank.valid);
    EXPECT_EQ(1u, secure_bank.lookups);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureSmmuv3GatosSsecSelectsNonsecureStream)
{
    constexpr uint64_t nonsecure_strtab_base = 0x6e000;
    constexpr uint64_t secure_strtab_base = 0x72000;
    constexpr uint64_t nonsecure_cd_base = 0x78000;
    constexpr uint64_t secure_cd_base = 0x7c000;
    constexpr uint64_t nonsecure_ttbr = 0x80000;
    constexpr uint64_t secure_ttbr = 0x86000;
    constexpr uint64_t iova = 0x1c000;
    constexpr uint64_t nonsecure_pa = 0xbe800;
    constexpr uint64_t secure_pa = 0xde800;
    constexpr uint32_t stream_id = 3;
    constexpr uint32_t strtab_cfg = 3;
    const uint64_t atos = atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1);
    const uint64_t nonsecure_ste = arch_s1_ste(nonsecure_cd_base);
    const uint64_t secure_ste = arch_s1_ste(secure_cd_base);

    stage_translation_tables(nonsecure_ttbr, iova, nonsecure_pa);
    stage_translation_tables(secure_ttbr, iova, secure_pa);
    store_u64(nonsecure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE,
              nonsecure_ste);
    store_u64(secure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE,
              secure_ste);
    store_u64(nonsecure_cd_base, arch_valid_cd());
    store_u64(nonsecure_cd_base + sizeof(uint64_t),
              nonsecure_ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(secure_cd_base, arch_valid_cd());
    store_u64(secure_cd_base + sizeof(uint64_t),
              secure_ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                nonsecure_strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), strtab_cfg);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                  secure_strtab_base);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                strtab_cfg);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), 0);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                  apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, stream_id);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                  apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI, atos);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    uint64_t par =
        static_cast<uint64_t>(
            reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    EXPECT_EQ(1u, par & 0x1u);
    EXPECT_EQ(0xfdu, (par >> 4) & 0xffu);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    par = static_cast<uint64_t>(
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
          (static_cast<uint64_t>(
               reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    const auto& secure_bank = m_tbu.arch_security_strtab_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE);

    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(nonsecure_pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_NE(secure_pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_EQ(nonsecure_ste, m_tbu.m_arch_last_ste);
    EXPECT_TRUE(secure_bank.configured);
    EXPECT_EQ(0u, secure_bank.lookups);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureSmmuv3GatosFaultDoesNotRecordEvent)
{
    constexpr uint64_t secure_strtab_base = 0x80000;
    constexpr uint64_t secure_cd_base = 0x84000;
    constexpr uint64_t secure_ttbr = 0x88000;
    constexpr uint64_t iova = 0x1d000;
    constexpr uint32_t stream_id = 4;
    constexpr uint32_t strtab_cfg = 3;
    const uint64_t secure_ste = arch_s1_ste(secure_cd_base);
    const uint64_t secure_sid =
        stream_id | apollo_smmu_tbu::ARCH_ATOS_SID_SECURE_STREAM;

    store_u64(secure_strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE,
              secure_ste);
    store_u64(secure_cd_base, arch_valid_cd());
    store_u64(secure_cd_base + sizeof(uint64_t),
              secure_ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    reg_s_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                  secure_strtab_base);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                strtab_cfg);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                  apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, secure_sid);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                  apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                  atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1));
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);

    const uint64_t par =
        static_cast<uint64_t>(
            reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    const auto& secure_bank = m_tbu.arch_security_eventq_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE);

    EXPECT_EQ(1u, par & 0x1u);
    EXPECT_EQ(0x10u, (par >> 4) & 0xffu);
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, secure_bank.queue.prod);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Smmuv3GatosAddrTypeReservedAndInvStage)
{
    constexpr uint64_t ste_base = 0x90000;
    constexpr uint64_t cd_base = 0x94000;
    constexpr uint64_t ttbr = 0x98000;
    constexpr uint64_t iova = 0x1e000;
    constexpr uint64_t pa = 0xae000;
    constexpr uint32_t stream_id = 5;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(ttbr, iova, pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, stream_id);

    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_RESERVED));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    uint64_t par =
        static_cast<uint64_t>(reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    EXPECT_EQ(1u, par & 0x1u);
    EXPECT_EQ(0xffu, (par >> 4) & 0xffu);

    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE2));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    par = static_cast<uint64_t>(
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
          (static_cast<uint64_t>(
               reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    EXPECT_EQ(1u, par & 0x1u);
    EXPECT_EQ(0xfeu, (par >> 4) & 0xffu);

    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    par = static_cast<uint64_t>(
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
          (static_cast<uint64_t>(
               reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Smmuv3GatosAddrTypeNestedStageSelection)
{
    constexpr uint64_t ste_base = 0x9c000;
    constexpr uint64_t cd_ipa = 0xa0000;
    constexpr uint64_t cd_pa = 0xa4000;
    constexpr uint64_t s1ttb = 0xb0000;
    constexpr uint64_t ipa = 0xc0000;
    constexpr uint64_t s2ttb = 0xd0000;
    constexpr uint64_t pa = 0xe0000;
    constexpr uint64_t iova = 0x1f000;
    constexpr uint32_t stream_id = 6;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    stage_translation_tables(s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s1ttb, iova, ipa);
    stage_translation_tables(s2ttb, ipa, pa);
    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa & apollo_smmu_tbu::ARCH_STE_S1CTXPTR_MASK);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, stream_id);

    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    uint64_t par =
        static_cast<uint64_t>(reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(ipa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1_STAGE2));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    par = static_cast<uint64_t>(
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
          (static_cast<uint64_t>(
               reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                atos_addr(ipa, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE2));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    par = static_cast<uint64_t>(
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
          (static_cast<uint64_t>(
               reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    EXPECT_EQ(0u, par & 0x1u);
    EXPECT_EQ(pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Smmuv3VatosStage1OnlyRegisterPath)
{
    constexpr uint64_t ste_base = 0x102000;
    constexpr uint64_t cd_ipa = 0x104000;
    constexpr uint64_t cd_pa = 0x106000;
    constexpr uint64_t s1ttb = 0x108000;
    constexpr uint64_t ipa = 0x118000;
    constexpr uint64_t s2ttb = 0x120000;
    constexpr uint64_t pa = 0x130000;
    constexpr uint64_t iova = 0x21000;
    constexpr uint32_t stream_id = 7;
    constexpr uint16_t vmid = 0x39;
    constexpr uint16_t foreign_vmid = 0x3a;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    stage_translation_tables(s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s1ttb, iova, ipa);
    stage_translation_tables(s2ttb, ipa, pa);
    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa & apollo_smmu_tbu::ARCH_STE_S1CTXPTR_MASK);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              (s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK) |
                  (static_cast<uint64_t>(vmid)
                   << apollo_smmu_tbu::ARCH_STE_S2VMID_SHIFT));
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET, 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET, 0);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_SID_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_SID_HI, stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_GATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_GATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1_STAGE2));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    const uint64_t gatos_par =
        static_cast<uint64_t>(reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32);
    ASSERT_EQ(0u, gatos_par & 0x1u);
    EXPECT_EQ(pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              gatos_par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_EQ(vmid, m_tbu.m_arch_last_vmid);

    m_tbu.m_arch_vatos_sel = vmid;
    reg_write64(apollo_smmu_tbu::SMMUV3_VATOS_PAGE +
                    apollo_smmu_tbu::SMMUV3_VATOS_SID_LO,
                apollo_smmu_tbu::SMMUV3_VATOS_PAGE +
                    apollo_smmu_tbu::SMMUV3_VATOS_SID_HI,
                stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_VATOS_PAGE +
                    apollo_smmu_tbu::SMMUV3_VATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_VATOS_PAGE +
                    apollo_smmu_tbu::SMMUV3_VATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1));
    reg_write32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    uint64_t vatos_par =
        static_cast<uint64_t>(
            reg_read32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_PAR_LO))) |
        (static_cast<uint64_t>(
             reg_read32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_PAR_HI))) << 32);

    EXPECT_EQ(0u, reg_read32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_CTRL)) &
                     apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    EXPECT_EQ(0u, vatos_par & 0x1u);
    EXPECT_EQ(ipa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              vatos_par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_NE(pa & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK,
              vatos_par & apollo_smmu_tbu::ARCH_GATOS_PAR_ADDR_MASK);
    EXPECT_EQ(vatos_par, m_tbu.m_arch_vatos_par);
    EXPECT_EQ(vmid, m_tbu.m_arch_last_vmid);
    EXPECT_TRUE(m_tbu.m_arch_last_atos_read);
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(gatos_par,
              static_cast<uint64_t>(
                  reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
                  (static_cast<uint64_t>(
                       reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32));

    reg_write64(apollo_smmu_tbu::SMMUV3_VATOS_PAGE +
                    apollo_smmu_tbu::SMMUV3_VATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_VATOS_PAGE +
                    apollo_smmu_tbu::SMMUV3_VATOS_ADDR_HI,
                atos_addr(iova,
                          apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1_STAGE2));
    reg_write32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    vatos_par = static_cast<uint64_t>(
                    reg_read32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_PAR_LO))) |
                (static_cast<uint64_t>(
                     reg_read32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_PAR_HI))) << 32);

    EXPECT_EQ(1u, vatos_par & 0x1u);
    EXPECT_EQ(0xffu, (vatos_par >> 4) & 0xffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_ATOS_INV_REQ,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(gatos_par,
              static_cast<uint64_t>(
                  reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_LO))) |
                  (static_cast<uint64_t>(
                       reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GATOS_PAR_HI))) << 32));

    m_tbu.m_arch_vatos_sel = foreign_vmid;
    reg_write64(apollo_smmu_tbu::SMMUV3_VATOS_PAGE +
                    apollo_smmu_tbu::SMMUV3_VATOS_ADDR_LO,
                apollo_smmu_tbu::SMMUV3_VATOS_PAGE +
                    apollo_smmu_tbu::SMMUV3_VATOS_ADDR_HI,
                atos_addr(iova, apollo_smmu_tbu::ARCH_ATOS_ADDR_TYPE_STAGE1));
    reg_write32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_CTRL),
                apollo_smmu_tbu::ARCH_GATOS_CTRL_RUN);
    vatos_par = static_cast<uint64_t>(
                    reg_read32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_PAR_LO))) |
                (static_cast<uint64_t>(
                     reg_read32(smmu_vatos_reg(apollo_smmu_tbu::SMMUV3_VATOS_PAR_HI))) << 32);
    EXPECT_EQ(1u, vatos_par & 0x1u);
    EXPECT_EQ(0xffu, (vatos_par >> 4) & 0xffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_ATOS_INV_REQ,
              m_tbu.m_arch_fault_reason);
    EXPECT_EQ(vmid, m_tbu.m_arch_last_vmid);
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR0)) &
                     apollo_smmu_tbu::ARCH_IDR0_VATOS);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_VATOS_SEL), 0x3ff);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_VATOS_SEL)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedCdMpamAttributesPropagateWhenS1MpamEnabled)
{
    constexpr uint64_t ste_base = 0x22000;
    constexpr uint64_t cd_base = 0x27000;
    constexpr uint64_t iova = 0x14800;
    constexpr uint64_t pa = 0x84800;
    constexpr uint32_t stream_id = 0x59;
    constexpr uint16_t ste_partid = 0x12;
    constexpr uint8_t ste_pmg = 0x2;
    constexpr uint16_t cd_partid = 0x1f;
    constexpr uint8_t cd_pmg = 0x7;
    constexpr uint32_t payload = 0x24681357;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    bool observed_cd_fetch_mpam_valid = false;
    bool observed_cd_fetch_mpam_remapped = true;
    uint16_t observed_cd_fetch_partid = 0;
    uint8_t observed_cd_fetch_pmg = 0;
    bool observed_payload_mpam_valid = false;
    bool observed_payload_mpam_remapped = true;
    bool observed_payload_mpam_unknown = true;
    uint16_t observed_payload_partid = 0;
    uint8_t observed_payload_pmg = 0;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, payload);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              apollo_smmu_tbu::ARCH_STE_S1MPAM |
                  arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(ste_partid)
                  << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              static_cast<uint64_t>(ste_pmg) << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT);
    store_u64(cd_base + apollo_smmu_tbu::ARCH_CD_MPAM_WORD_OFFSET,
              (static_cast<uint64_t>(cd_pmg) << apollo_smmu_tbu::ARCH_CD_PMG_SHIFT) |
                  (static_cast<uint64_t>(cd_partid)
                   << apollo_smmu_tbu::ARCH_CD_PARTID_SHIFT));
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_ATSCHK);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == cd_base + apollo_smmu_tbu::ARCH_CD_MPAM_WORD_OFFSET &&
            ext != nullptr) {
            observed_cd_fetch_mpam_valid = ext->mpam_valid;
            observed_cd_fetch_mpam_remapped = ext->mpam_remapped;
            observed_cd_fetch_partid = ext->mpam_partid;
            observed_cd_fetch_pmg = ext->mpam_pmg;
        }
        if (addr == pa && ext != nullptr) {
            observed_payload_mpam_valid = ext->mpam_valid;
            observed_payload_mpam_remapped = ext->mpam_remapped;
            observed_payload_mpam_unknown = ext->mpam_unknown;
            observed_payload_partid = ext->mpam_partid;
            observed_payload_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_cd_fetch_mpam_valid);
    EXPECT_FALSE(observed_cd_fetch_mpam_remapped);
    EXPECT_EQ(ste_partid, observed_cd_fetch_partid);
    EXPECT_EQ(ste_pmg, observed_cd_fetch_pmg);
    EXPECT_TRUE(observed_payload_mpam_valid);
    EXPECT_FALSE(observed_payload_mpam_remapped);
    EXPECT_FALSE(observed_payload_mpam_unknown);
    EXPECT_EQ(cd_partid, observed_payload_partid);
    EXPECT_EQ(cd_pmg, observed_payload_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedSplitStageUnsupportedRecordsForbidden)
{
    constexpr uint64_t ste_base = 0x2400;
    constexpr uint64_t cd_base = 0x3400;
    constexpr uint64_t eventq_base = 0x9a00;
    constexpr uint64_t iova = 0x13000;
    constexpr uint64_t pa = 0x83000;
    constexpr uint32_t stream_id = 0x2b;
    constexpr uint32_t value = 0xf17e5a7e;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, value);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT, m_tbu.m_arch_last_eats);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSL_FORBIDDEN,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(load_u64(eventq_base) >> 32));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedSplitStageStage2OnlyWalksIpa)
{
    constexpr uint64_t ste_base = 0x2c000;
    constexpr uint64_t s2ttb = 0x54000;
    constexpr uint64_t ipa = 0x13280;
    constexpr uint64_t pa_base = 0x84000;
    constexpr uint64_t pa = pa_base | (ipa & (apollo_smmu_tbu::PAGE_SIZE - 1));
    constexpr uint32_t stream_id = 0x2d;
    constexpr uint32_t value = 0x5a77c0de;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    stage_translation_tables(s2ttb, ipa, pa_base);
    store_u64(pa, value);
    store_u64(ste_pa, arch_s2_ste(0));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT));
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, ipa, observed));
    EXPECT_EQ(value, observed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT, m_tbu.m_arch_last_eats);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_last_stage);
    EXPECT_EQ(s2ttb, m_tbu.m_arch_s2ttb);
    EXPECT_EQ(pa, m_tbu.m_arch_last_pa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedSplitStageNestedWalksIpaWithStage2Only)
{
    constexpr uint64_t ste_base = 0x2d000;
    constexpr uint64_t s2ttb = 0x56000;
    constexpr uint64_t ipa = 0x15280;
    constexpr uint64_t pa_base = 0x8c000;
    constexpr uint64_t pa = pa_base | (ipa & (apollo_smmu_tbu::PAGE_SIZE - 1));
    constexpr uint32_t stream_id = 0x2e;
    constexpr uint32_t value = 0xc0015102;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    stage_translation_tables(s2ttb, ipa, pa_base);
    store_u64(pa, value);
    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT));
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, ipa, observed));
    EXPECT_EQ(value, observed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT, m_tbu.m_arch_last_eats);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_last_stage);
    EXPECT_EQ(ipa, m_tbu.m_arch_last_ipa);
    EXPECT_EQ(s2ttb, m_tbu.m_arch_s2ttb);
    EXPECT_EQ(pa, m_tbu.m_arch_last_pa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedSplitStageNestedAppliesAccessOverrides)
{
    constexpr uint64_t ste_base = 0x2e000;
    constexpr uint64_t s2ttb = 0x58000;
    constexpr uint64_t ipa = 0x16280;
    constexpr uint64_t pa_base = 0x8e000;
    constexpr uint64_t pa = pa_base | (ipa & (apollo_smmu_tbu::PAGE_SIZE - 1));
    constexpr uint32_t stream_id = 0x30;
    constexpr uint32_t value = 0xa75accc5;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;
    bool observed_attrs_valid = false;
    bool observed_privileged = false;
    bool observed_instruction = false;
    uint8_t observed_inst_cfg = 0;
    uint8_t observed_priv_cfg = 0;

    stage_translation_tables(s2ttb, ipa, pa_base);
    store_u64(pa, value);
    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT) |
                  arch_ste_output_attrs(false, 0, 0, 0, 3, 3,
                                        apollo_smmu_tbu::ARCH_STE_NSCFG_USE_INCOMING));
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == pa && ext != nullptr) {
            observed_attrs_valid = ext->output_attrs_valid;
            observed_privileged = ext->privileged;
            observed_instruction = ext->instruction;
            observed_inst_cfg = ext->output_inst_cfg;
            observed_priv_cfg = ext->output_priv_cfg;
        }
        return mem_read(addr, data, len);
    });

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, ipa, observed));
    EXPECT_EQ(value, observed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_TRUE(m_tbu.m_arch_translated_effective_access_valid);
    EXPECT_FALSE(m_tbu.m_arch_last_split_stage_in_privileged);
    EXPECT_FALSE(m_tbu.m_arch_last_split_stage_in_instruction);
    EXPECT_TRUE(m_tbu.m_arch_last_split_stage_effective_privileged);
    EXPECT_TRUE(m_tbu.m_arch_last_split_stage_effective_instruction);
    EXPECT_TRUE(observed_attrs_valid);
    EXPECT_TRUE(observed_privileged);
    EXPECT_TRUE(observed_instruction);
    EXPECT_EQ(3, observed_inst_cfg);
    EXPECT_EQ(3, observed_priv_cfg);
    EXPECT_EQ(ipa, m_tbu.m_arch_last_ipa);
    EXPECT_EQ(pa, m_tbu.m_arch_last_pa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslationRequestSplitStageNestedWalksIpaWithStage2Only)
{
    constexpr uint64_t ste_base = 0x2f000;
    constexpr uint64_t s2ttb = 0x5a000;
    constexpr uint64_t ipa = 0x17240;
    constexpr uint64_t pa_base = 0x8d000;
    constexpr uint64_t pa = pa_base | (ipa & (apollo_smmu_tbu::PAGE_SIZE - 1));
    constexpr uint32_t stream_id = 0x31;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    stage_translation_tables(s2ttb, ipa, pa_base);
    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT));
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, ipa);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_SPLIT, m_tbu.m_arch_last_eats);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_last_stage);
    EXPECT_EQ(ipa, m_tbu.m_arch_last_ipa);
    EXPECT_EQ(s2ttb, m_tbu.m_arch_s2ttb);
    EXPECT_EQ(pa, m_tbu.m_arch_last_pa);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(ipa)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedConfigAbortRecordsForbidden)
{
    constexpr uint64_t ste_base = 0x2600;
    constexpr uint64_t eventq_base = 0x9b00;
    constexpr uint64_t iova = 0x14000;
    constexpr uint64_t pa = 0x84000;
    constexpr uint32_t stream_id = 0x2c;
    constexpr uint32_t value = 0xab047000;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t abort_ste =
        apollo_smmu_tbu::ARCH_STE_VALID |
        (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_ABORT)
         << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT);
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, value);
    store_u64(ste_pa, abort_ste);
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_FULL));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(abort_ste, m_tbu.m_arch_last_ste);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSL_FORBIDDEN,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(load_u64(eventq_base) >> 32));
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedDptUnsupportedActsAsDisabled)
{
    constexpr uint64_t ste_base = 0x2800;
    constexpr uint64_t cd_base = 0x3800;
    constexpr uint64_t eventq_base = 0x9c00;
    constexpr uint64_t iova = 0x15000;
    constexpr uint64_t pa = 0x85000;
    constexpr uint32_t stream_id = 0x2d;
    constexpr uint32_t value = 0xd9700000;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, value);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_DPT));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(0u, apollo_smmu_tbu::ARCH_IDR3 & apollo_smmu_tbu::ARCH_IDR3_DPT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_EATS_DISABLED, m_tbu.m_arch_last_eats);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSL_FORBIDDEN,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(load_u64(eventq_base) >> 32));
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedPasidttDisabledClearsPrefixAttrs)
{
    constexpr uint64_t ste_base = 0x2a00;
    constexpr uint64_t cd_base = 0x3a00;
    constexpr uint64_t eventq_base = 0x9d80;
    constexpr uint64_t iova = 0x16000;
    constexpr uint64_t pa = 0x86000;
    constexpr uint32_t stream_id = 0x2e;
    constexpr uint32_t ssid = 0x54321;
    constexpr uint32_t value = 0x0badc0de;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    gs::ApolloSmmuStreamIdExtension stream_id_ext(stream_id, ssid, true,
                                                  true, true, true);
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, value);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_DISABLED));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(iova);
    trans.set_data_ptr(reinterpret_cast<uint8_t*>(&observed));
    trans.set_data_length(sizeof(observed));
    trans.set_streaming_width(sizeof(observed));
    trans.set_byte_enable_length(0);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    trans.set_extension(&stream_id_ext);
    m_tbu.b_transport(trans, delay);
    trans.clear_extension(&stream_id_ext);

    ASSERT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE, trans.get_response_status());
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    const uint64_t word0 = load_u64(eventq_base);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSL_FORBIDDEN, word0 & 0xffu);
    EXPECT_EQ(0u, word0 & (1ULL << 11));
    EXPECT_EQ(0u, (word0 >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedConfigFaultsHonorCr2RecCfgAts)
{
    constexpr uint64_t ste_base = 0x2200;
    constexpr uint64_t strtab_base = 0x4000;
    constexpr uint64_t eventq_base = 0x9900;
    constexpr uint64_t iova = 0x15000;
    constexpr uint32_t stream_id = 3;
    constexpr uint32_t bad_stream_id = 4;
    constexpr uint32_t one_bit_strtab = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN |
                    apollo_smmu_tbu::ARCH_CR0_ATSCHK);
    store_u64(ste_pa, 0);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base));

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STE,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(load_u64(eventq_base) >> 32));
    EXPECT_EQ(0u, load_u64(eventq_base + 8));
    EXPECT_EQ(0u, load_u64(eventq_base + 16));
    EXPECT_EQ(0u, load_u64(eventq_base + 24));

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                one_bit_strtab);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(bad_stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(2u, m_tbu.m_eventq.prod);
    const uint64_t bad_sid_record =
        eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STREAMID,
              load_u64(bad_sid_record) & 0xffu);
    EXPECT_EQ(bad_stream_id,
              static_cast<uint32_t>(load_u64(bad_sid_record) >> 32));
    EXPECT_EQ(0u, load_u64(bad_sid_record + 8));
    EXPECT_EQ(0u, load_u64(bad_sid_record + 16));
    EXPECT_EQ(0u, load_u64(bad_sid_record + 24));
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedTransactionsHonorSmmuenAndAtschk)
{
    constexpr uint64_t strtab_base = 0x4800;
    constexpr uint64_t eventq_base = 0x9d00;
    constexpr uint64_t iova = 0x19000;
    constexpr uint64_t pa = 0x8c000;
    constexpr uint32_t stream_id = 4;
    constexpr uint32_t value = 0xc0defeed;
    constexpr uint32_t one_bit_strtab = 1;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, value);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSL_FORBIDDEN,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(load_u64(eventq_base) >> 32));

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                one_bit_strtab);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);

    /*
     * ATS Translated traffic with ATSCHK==0 bypasses configuration lookup:
     * the out-of-range StreamID would be C_BAD_STREAMID if checked.
     */
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(value, observed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES));
    EXPECT_EQ(0u, m_tbu.m_arch_last_ste);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedAddressSizeAbortIsNoEvent)
{
    constexpr uint64_t eventq_base = 0x9f00;
    const uint64_t high_pa =
        apollo_smmu_tbu::arch_translated_addr_mask() + 1;
    constexpr uint32_t stream_id = 6;
    uint32_t observed = 0;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN);

    /*
     * ATS Translated address-size behavior is checked even when ATSCHK==0.
     * QBox chooses the spec-permitted no-event abort behavior.
     */
    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, high_pa, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_ADDR_SIZE,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base));
    EXPECT_EQ(0u, m_tbu.m_arch_last_ste);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedPriorityChecksConfigBeforeForbidden)
{
    constexpr uint64_t strtab_base = 0x4c00;
    constexpr uint64_t eventq_base = 0x9e00;
    constexpr uint64_t iova = 0x1a000;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t bad_stream_id = 4;
    constexpr uint32_t one_bit_strtab = 1;
    const uint64_t ste_pa =
        strtab_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                one_bit_strtab);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN |
                    apollo_smmu_tbu::ARCH_CR0_ATSCHK);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);

    // ATS Translated priority: C_BAD_STREAMID is checked before forbidden EATS.
    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(bad_stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STREAMID,
              load_u64(eventq_base) & 0xffu);

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    store_u64(ste_pa, 0);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    // ATS Translated priority: C_BAD_STE is checked before F_TRANSL_FORBIDDEN.
    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    const uint64_t bad_ste_record =
        eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STE,
              load_u64(bad_ste_record) & 0xffu);

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    store_u64(ste_pa, arch_s1_ste(0x7000));
    store_u64(ste_pa + sizeof(uint64_t),
              arch_ste_eats(apollo_smmu_tbu::ARCH_STE_EATS_DISABLED));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TRANSL_FORBIDDEN,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    const uint64_t forbidden_record =
        eventq_base + 2 * apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSL_FORBIDDEN,
              load_u64(forbidden_record) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedSteFetchRecordsBeforeSteDecode)
{
    constexpr uint64_t eventq_base = 0x9c00;
    constexpr uint64_t iova = 0x1b000;
    constexpr uint64_t pa = 0x8d000;
    constexpr uint32_t stream_id = 0;
    constexpr uint32_t value = 0x5eedc0de;
    constexpr uint64_t ste_pa = MEM_SIZE - sizeof(uint64_t);
    constexpr uint64_t ste_base = ste_pa;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(pa, value);
    store_u64(ste_pa, arch_s1_ste(0x7000));
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN |
                    apollo_smmu_tbu::ARCH_CR0_ATSCHK);

    // ATS Translated priority: F_STE_FETCH is checked before STE decode/EATS.
    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STE_FETCH,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base));

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STE_FETCH,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_STE_FETCH,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(0u, load_u64(eventq_base + 8));
    EXPECT_EQ(ste_pa & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK,
              load_u64(eventq_base + 24) &
                  apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslatedVmsFetchRecordsBeforeForbidden)
{
    constexpr uint64_t ste_base = 0x5400;
    constexpr uint64_t eventq_base = 0x9a80;
    constexpr uint64_t iova = 0x1c000;
    constexpr uint32_t stream_id = 2;
    constexpr uint64_t vms_ptr = MEM_SIZE;
    const uint64_t ste_pa =
        ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    uint32_t observed = 0;

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN |
                    apollo_smmu_tbu::ARCH_CR0_ATSCHK);

    store_u64(ste_pa, apollo_smmu_tbu::ARCH_STE_VALID |
                          (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_NESTED)
                           << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT));
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1MPAM);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              vms_ptr & apollo_smmu_tbu::ARCH_STE_VMSPTR_MASK);

    // ATS Translated priority: F_VMS_FETCH is checked before F_TRANSL_FORBIDDEN.
    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_VMS_FETCH,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base));

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              translated_stream_read32(stream_id, iova, observed));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_VMS_FETCH,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_VMS_FETCH,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(0u, load_u64(eventq_base + 8));
    EXPECT_EQ(vms_ptr & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK,
              load_u64(eventq_base + 24) &
                  apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, VmsFetchCachesFullPartidMap)
{
    constexpr uint64_t ste_pa = 0x5600;
    constexpr uint64_t vms_ptr = 0x17000;
    constexpr uint32_t stream_id = 3;
    constexpr uint64_t ste0 =
        apollo_smmu_tbu::ARCH_STE_VALID |
        (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_NESTED)
         << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT);
    constexpr uint64_t ste1 = apollo_smmu_tbu::ARCH_STE_S1MPAM;

    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              vms_ptr & apollo_smmu_tbu::ARCH_STE_VMSPTR_MASK);
    for (size_t i = 0; i < m_tbu.m_arch_last_vms_partid_map.size(); i++) {
        store_u64(vms_ptr + i * sizeof(uint64_t), 0xa110000000000000ULL | i);
    }

    ASSERT_TRUE(m_tbu.arch_fetch_vms_if_enabled(stream_id, ste_pa, ste0, ste1));
    EXPECT_EQ(vms_ptr, m_tbu.m_arch_last_vms_ptr);
    EXPECT_EQ(vms_ptr & apollo_smmu_tbu::ARCH_STE_VMSPTR_MASK,
              m_tbu.arch_ste_vmsptr(m_tbu.m_arch_last_ste5));
    for (size_t i = 0; i < m_tbu.m_arch_last_vms_partid_map.size(); i++) {
        EXPECT_EQ(0xa110000000000000ULL | i,
                  m_tbu.m_arch_last_vms_partid_map[i]);
    }
}

TEST_BENCH(ApolloSmmuTbuTestBench, VmsPartidMapRemapsCdPartidToMpamExtension)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t cd_pa = 0xe0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t vms_ptr = 0x18000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint64_t ipa = 0x23000;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x4d;
    constexpr uint16_t virtual_partid = 5;
    constexpr uint16_t physical_partid = 0x1a;
    constexpr uint8_t pmg = 0x7;
    constexpr uint32_t payload = 0x89abcdef;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    bool observed_mpam_valid = false;
    bool observed_mpam_remapped = false;
    bool observed_mpam_unknown = true;
    uint16_t observed_partid = 0;
    uint8_t observed_pmg = 0;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));

    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    stage_translation_tables(s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s1ttb, iova, ipa);
    stage_translation_tables(s2ttb, ipa, output_pa);

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1MPAM);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              vms_ptr & apollo_smmu_tbu::ARCH_STE_VMSPTR_MASK);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(cd_pa + apollo_smmu_tbu::ARCH_CD_MPAM_WORD_OFFSET,
              (static_cast<uint64_t>(pmg) << apollo_smmu_tbu::ARCH_CD_PMG_SHIFT) |
                  (static_cast<uint64_t>(virtual_partid)
                   << apollo_smmu_tbu::ARCH_CD_PARTID_SHIFT));
    store_u64(vms_ptr + (virtual_partid / 4) * sizeof(uint64_t),
              static_cast<uint64_t>(physical_partid)
                  << ((virtual_partid % 4) * 16));
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == output_addr && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_mpam_remapped = ext->mpam_remapped;
            observed_mpam_unknown = ext->mpam_unknown;
            observed_partid = ext->mpam_partid;
            observed_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_TRUE(observed_mpam_remapped);
    EXPECT_FALSE(observed_mpam_unknown);
    EXPECT_EQ(physical_partid, observed_partid);
    EXPECT_EQ(pmg, observed_pmg);
    EXPECT_EQ(physical_partid, m_tbu.m_arch_last_mpam_partid);
    EXPECT_EQ(virtual_partid, m_tbu.m_arch_last_mpam_virtual_partid);
    EXPECT_EQ(pmg, m_tbu.m_arch_last_mpam_pmg);
    EXPECT_EQ(physical_partid,
              reg_read32(apollo_smmu_tbu::REG_ARCH_MPAM_DETAIL));
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_MPAM_STATUS) & 0x1u);
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_MPAM_STATUS) & 0x2u);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SteMpamAttributesPropagateWhenS1MpamDisabled)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x80000;
    constexpr uint64_t iova = 0x4567;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x4e;
    constexpr uint16_t partid = 0x1b;
    constexpr uint8_t pmg = 0x6;
    constexpr uint32_t payload = 0x10293847;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));
    bool observed_mpam_valid = false;
    bool observed_mpam_remapped = true;
    uint16_t observed_partid = 0;
    uint8_t observed_pmg = 0;

    stage_translation_tables(ttbr, iova, output_pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(partid) << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              static_cast<uint64_t>(pmg) << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == output_addr && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_mpam_remapped = ext->mpam_remapped;
            observed_partid = ext->mpam_partid;
            observed_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_FALSE(observed_mpam_remapped);
    EXPECT_EQ(partid, observed_partid);
    EXPECT_EQ(pmg, observed_pmg);
    EXPECT_EQ(partid, m_tbu.m_arch_last_mpam_partid);
    EXPECT_EQ(pmg, m_tbu.m_arch_last_mpam_pmg);
    EXPECT_EQ(partid, reg_read32(apollo_smmu_tbu::REG_ARCH_MPAM_DETAIL));
}

TEST_BENCH(ApolloSmmuTbuTestBench, SteMpamAttributesPropagateOnCdFetches)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x80000;
    constexpr uint64_t iova = 0x4567;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x52;
    constexpr uint16_t partid = 0x16;
    constexpr uint8_t pmg = 0x5;
    constexpr uint32_t payload = 0x98765432;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));
    bool observed_cd_mpam_valid = false;
    bool observed_cd_mpam_remapped = true;
    bool observed_cd_mpam_unknown = true;
    uint16_t observed_cd_partid = 0;
    uint8_t observed_cd_pmg = 0;

    stage_translation_tables(ttbr, iova, output_pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(partid) << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              static_cast<uint64_t>(pmg) << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == cd_base && ext != nullptr) {
            observed_cd_mpam_valid = ext->mpam_valid;
            observed_cd_mpam_remapped = ext->mpam_remapped;
            observed_cd_mpam_unknown = ext->mpam_unknown;
            observed_cd_partid = ext->mpam_partid;
            observed_cd_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_cd_mpam_valid);
    EXPECT_FALSE(observed_cd_mpam_remapped);
    EXPECT_FALSE(observed_cd_mpam_unknown);
    EXPECT_EQ(partid, observed_cd_partid);
    EXPECT_EQ(pmg, observed_cd_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SteMpamAttributesPropagateOnS1MpamCdFetches)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x80000;
    constexpr uint64_t iova = 0x4567;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x55;
    constexpr uint16_t ste_partid = 0x11;
    constexpr uint8_t ste_pmg = 0x2;
    constexpr uint16_t cd_partid = 0x18;
    constexpr uint8_t cd_pmg = 0x6;
    constexpr uint32_t payload = 0x44332211;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));
    bool observed_cd_mpam_valid = false;
    bool observed_cd_mpam_remapped = true;
    bool observed_cd_mpam_unknown = true;
    uint16_t observed_cd_partid = 0;
    uint8_t observed_cd_pmg = 0;
    bool observed_output_mpam_valid = false;
    bool observed_output_mpam_remapped = true;
    bool observed_output_mpam_unknown = true;
    uint16_t observed_output_partid = 0;
    uint8_t observed_output_pmg = 0;

    stage_translation_tables(ttbr, iova, output_pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1MPAM);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(ste_partid)
                  << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              static_cast<uint64_t>(ste_pmg) << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(cd_base + apollo_smmu_tbu::ARCH_CD_MPAM_WORD_OFFSET,
              (static_cast<uint64_t>(cd_pmg) << apollo_smmu_tbu::ARCH_CD_PMG_SHIFT) |
                  (static_cast<uint64_t>(cd_partid)
                   << apollo_smmu_tbu::ARCH_CD_PARTID_SHIFT));
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == cd_base && ext != nullptr) {
            observed_cd_mpam_valid = ext->mpam_valid;
            observed_cd_mpam_remapped = ext->mpam_remapped;
            observed_cd_mpam_unknown = ext->mpam_unknown;
            observed_cd_partid = ext->mpam_partid;
            observed_cd_pmg = ext->mpam_pmg;
        }
        if (addr == output_addr && ext != nullptr) {
            observed_output_mpam_valid = ext->mpam_valid;
            observed_output_mpam_remapped = ext->mpam_remapped;
            observed_output_mpam_unknown = ext->mpam_unknown;
            observed_output_partid = ext->mpam_partid;
            observed_output_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_cd_mpam_valid);
    EXPECT_FALSE(observed_cd_mpam_remapped);
    EXPECT_FALSE(observed_cd_mpam_unknown);
    EXPECT_EQ(ste_partid, observed_cd_partid);
    EXPECT_EQ(ste_pmg, observed_cd_pmg);
    EXPECT_TRUE(observed_output_mpam_valid);
    EXPECT_FALSE(observed_output_mpam_remapped);
    EXPECT_FALSE(observed_output_mpam_unknown);
    EXPECT_EQ(cd_partid, observed_output_partid);
    EXPECT_EQ(cd_pmg, observed_output_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SteMpamAttributesPropagateOnS1TtFetches)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x80000;
    constexpr uint64_t iova = 0x4567;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x53;
    constexpr uint16_t partid = 0x15;
    constexpr uint8_t pmg = 0x4;
    constexpr uint32_t payload = 0x1234abcd;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));
    bool observed_tt_mpam_valid = false;
    bool observed_tt_mpam_remapped = true;
    bool observed_tt_mpam_unknown = true;
    uint16_t observed_tt_partid = 0;
    uint8_t observed_tt_pmg = 0;

    stage_translation_tables(ttbr, iova, output_pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(partid) << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              static_cast<uint64_t>(pmg) << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == ttbr && ext != nullptr) {
            observed_tt_mpam_valid = ext->mpam_valid;
            observed_tt_mpam_remapped = ext->mpam_remapped;
            observed_tt_mpam_unknown = ext->mpam_unknown;
            observed_tt_partid = ext->mpam_partid;
            observed_tt_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_tt_mpam_valid);
    EXPECT_FALSE(observed_tt_mpam_remapped);
    EXPECT_FALSE(observed_tt_mpam_unknown);
    EXPECT_EQ(partid, observed_tt_partid);
    EXPECT_EQ(pmg, observed_tt_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, NestedS1MpamAttributesPropagateOnStage2HelperWalks)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t cd_pa = 0xe0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t vms_ptr = 0x18000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint64_t ipa = 0x23000;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x56;
    constexpr uint16_t ste_partid = 0x13;
    constexpr uint8_t ste_pmg = 0x2;
    constexpr uint16_t virtual_partid = 6;
    constexpr uint16_t physical_partid = 0x1c;
    constexpr uint8_t cd_pmg = 0x7;
    constexpr uint32_t payload = 0xfeed1234;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));
    size_t s2_root_read_count = 0;
    bool observed_cd_s2_mpam_valid = false;
    bool observed_cd_s2_mpam_remapped = true;
    bool observed_cd_s2_mpam_unknown = true;
    uint16_t observed_cd_s2_partid = 0;
    uint8_t observed_cd_s2_pmg = 0;
    bool observed_remapped_s2_mpam_valid = false;
    bool observed_remapped_s2_mpam_unknown = true;
    uint16_t observed_remapped_s2_partid = 0;
    uint8_t observed_remapped_s2_pmg = 0;

    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    stage_translation_tables(s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s1ttb, iova, ipa);
    stage_translation_tables(s2ttb, ipa, output_pa);

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1MPAM);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(ste_partid)
                  << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              (vms_ptr & apollo_smmu_tbu::ARCH_STE_VMSPTR_MASK) |
                  (static_cast<uint64_t>(ste_pmg)
                   << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT));
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(cd_pa + apollo_smmu_tbu::ARCH_CD_MPAM_WORD_OFFSET,
              (static_cast<uint64_t>(cd_pmg) << apollo_smmu_tbu::ARCH_CD_PMG_SHIFT) |
                  (static_cast<uint64_t>(virtual_partid)
                   << apollo_smmu_tbu::ARCH_CD_PARTID_SHIFT));
    store_u64(vms_ptr + (virtual_partid / 4) * sizeof(uint64_t),
              static_cast<uint64_t>(physical_partid)
                  << ((virtual_partid % 4) * 16));
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == s2ttb) {
            s2_root_read_count++;
            if (s2_root_read_count == 1 && ext != nullptr) {
                observed_cd_s2_mpam_valid = ext->mpam_valid;
                observed_cd_s2_mpam_remapped = ext->mpam_remapped;
                observed_cd_s2_mpam_unknown = ext->mpam_unknown;
                observed_cd_s2_partid = ext->mpam_partid;
                observed_cd_s2_pmg = ext->mpam_pmg;
            }
            if (ext != nullptr && ext->mpam_remapped) {
                observed_remapped_s2_mpam_valid = ext->mpam_valid;
                observed_remapped_s2_mpam_unknown = ext->mpam_unknown;
                observed_remapped_s2_partid = ext->mpam_partid;
                observed_remapped_s2_pmg = ext->mpam_pmg;
            }
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_GE(s2_root_read_count, 2u);
    EXPECT_TRUE(observed_cd_s2_mpam_valid);
    EXPECT_FALSE(observed_cd_s2_mpam_remapped);
    EXPECT_FALSE(observed_cd_s2_mpam_unknown);
    EXPECT_EQ(ste_partid, observed_cd_s2_partid);
    EXPECT_EQ(ste_pmg, observed_cd_s2_pmg);
    EXPECT_TRUE(observed_remapped_s2_mpam_valid);
    EXPECT_FALSE(observed_remapped_s2_mpam_unknown);
    EXPECT_EQ(physical_partid, observed_remapped_s2_partid);
    EXPECT_EQ(cd_pmg, observed_remapped_s2_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SteMpamAttributesPropagateOnS2TtFetches)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t ipa = 0x6789;
    constexpr uint64_t output_pa = 0xc0000;
    constexpr uint32_t stream_id = 0x54;
    constexpr uint16_t partid = 0x14;
    constexpr uint8_t pmg = 0x3;
    constexpr uint32_t payload = 0x76543210;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (ipa & (apollo_smmu_tbu::PAGE_SIZE - 1));
    bool observed_tt_mpam_valid = false;
    bool observed_tt_mpam_remapped = true;
    bool observed_tt_mpam_unknown = true;
    uint16_t observed_tt_partid = 0;
    uint8_t observed_tt_pmg = 0;

    stage_translation_tables(s2ttb, ipa, output_pa);
    store_u64(ste_pa, arch_s2_ste(0));
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(partid) << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              static_cast<uint64_t>(pmg) << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT);
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == s2ttb && ext != nullptr) {
            observed_tt_mpam_valid = ext->mpam_valid;
            observed_tt_mpam_remapped = ext->mpam_remapped;
            observed_tt_mpam_unknown = ext->mpam_unknown;
            observed_tt_partid = ext->mpam_partid;
            observed_tt_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, ipa, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_tt_mpam_valid);
    EXPECT_FALSE(observed_tt_mpam_remapped);
    EXPECT_FALSE(observed_tt_mpam_unknown);
    EXPECT_EQ(partid, observed_tt_partid);
    EXPECT_EQ(pmg, observed_tt_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, MpamRangeOverflowMarksUnknownAttributes)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x80000;
    constexpr uint64_t iova = 0x5678;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x4f;
    constexpr uint16_t unsupported_partid =
        apollo_smmu_tbu::ARCH_MPAMIDR_PARTID_MAX + 1;
    constexpr uint8_t unsupported_pmg =
        apollo_smmu_tbu::ARCH_MPAMIDR_PMG_MAX + 1;
    constexpr uint32_t payload = 0x55667788;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));
    bool observed_mpam_valid = false;
    bool observed_mpam_unknown = false;
    uint16_t observed_partid = 0;
    uint8_t observed_pmg = 0;

    stage_translation_tables(ttbr, iova, output_pa);
    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_MPAM_WORD4_OFFSET,
              static_cast<uint64_t>(unsupported_partid)
                  << apollo_smmu_tbu::ARCH_STE_PARTID_SHIFT);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              static_cast<uint64_t>(unsupported_pmg)
                  << apollo_smmu_tbu::ARCH_STE_PMG_SHIFT);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t),
              ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == output_addr && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_mpam_unknown = ext->mpam_unknown;
            observed_partid = ext->mpam_partid;
            observed_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_TRUE(observed_mpam_unknown);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAM_UNKNOWN_PARTID, observed_partid);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAM_UNKNOWN_PMG, observed_pmg);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAM_UNKNOWN_PARTID,
              m_tbu.m_arch_last_mpam_partid);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAM_UNKNOWN_PMG,
              m_tbu.m_arch_last_mpam_pmg);
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_MPAM_STATUS) & 0x4u);
}

TEST_BENCH(ApolloSmmuTbuTestBench, GlobalBypassUsesGbpmpamAttributes)
{
    constexpr uint64_t iova = 0x10000;
    constexpr uint64_t output_pa = 0x11000;
    constexpr uint64_t map_size = apollo_smmu_tbu::PAGE_SIZE;
    constexpr uint32_t stream_id = 0x50;
    constexpr uint16_t partid = 0x1c;
    constexpr uint8_t pmg = 0x5;
    constexpr uint32_t payload = 0xa5a55a5a;
    constexpr uint32_t gbpmpam_value =
        (static_cast<uint32_t>(pmg) << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        partid;
    bool observed_mpam_valid = false;
    bool observed_mpam_remapped = true;
    bool observed_mpam_unknown = true;
    uint16_t observed_partid = 0;
    uint8_t observed_pmg = 0;

    std::memcpy(&m_memory_bytes[output_pa], &payload, sizeof(payload));
    add_map(stream_id, iova, output_pa, map_size);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | gbpmpam_value);
    EXPECT_EQ(gbpmpam_value, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPMPAM)));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == output_pa && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_mpam_remapped = ext->mpam_remapped;
            observed_mpam_unknown = ext->mpam_unknown;
            observed_partid = ext->mpam_partid;
            observed_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_FALSE(observed_mpam_remapped);
    EXPECT_FALSE(observed_mpam_unknown);
    EXPECT_EQ(partid, observed_partid);
    EXPECT_EQ(pmg, observed_pmg);
    EXPECT_EQ(partid, m_tbu.m_arch_last_mpam_partid);
    EXPECT_EQ(pmg, m_tbu.m_arch_last_mpam_pmg);
    EXPECT_EQ(partid, reg_read32(apollo_smmu_tbu::REG_ARCH_MPAM_DETAIL));
}

TEST_BENCH(ApolloSmmuTbuTestBench, GlobalBypassUsesGbpaOutputAttributes)
{
    constexpr uint64_t iova = 0x12000;
    constexpr uint64_t output_pa = 0x13000;
    constexpr uint64_t map_size = apollo_smmu_tbu::PAGE_SIZE;
    constexpr uint32_t stream_id = 0x51;
    constexpr uint32_t payload = 0x6bfa600d;
    constexpr uint32_t gbpa_value =
        apollo_smmu_tbu::ARCH_GBPA_MTCFG |
        (0xau << apollo_smmu_tbu::ARCH_GBPA_ALLOCCFG_SHIFT) |
        (0x3u << apollo_smmu_tbu::ARCH_GBPA_INSTCFG_SHIFT) |
        (0x2u << apollo_smmu_tbu::ARCH_GBPA_PRIVCFG_SHIFT);
    bool observed_attrs_valid = false;
    bool observed_mtcfg = false;
    uint8_t observed_mem_type = 0xff;
    uint8_t observed_shareability = 0;
    uint8_t observed_alloc_hint = 0;
    uint8_t observed_inst_cfg = 0;
    uint8_t observed_priv_cfg = 0;

    std::memcpy(&m_memory_bytes[output_pa], &payload, sizeof(payload));
    add_map(stream_id, iova, output_pa, map_size);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPA), gbpa_value);
    EXPECT_EQ(gbpa_value, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPA)));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == output_pa && ext != nullptr) {
            observed_attrs_valid = ext->output_attrs_valid;
            observed_mtcfg = ext->output_mtcfg;
            observed_mem_type = ext->output_mem_type;
            observed_shareability = ext->output_shareability;
            observed_alloc_hint = ext->output_alloc_hint;
            observed_inst_cfg = ext->output_inst_cfg;
            observed_priv_cfg = ext->output_priv_cfg;
        }
        return mem_read(addr, data, len);
    });

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_attrs_valid);
    EXPECT_TRUE(observed_mtcfg);
    EXPECT_EQ(0u, observed_mem_type);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_STE_SHCFG_OSH, observed_shareability);
    EXPECT_EQ(0xau, observed_alloc_hint);
    EXPECT_EQ(0x3u, observed_inst_cfg);
    EXPECT_EQ(0x2u, observed_priv_cfg);
    EXPECT_TRUE(m_tbu.m_arch_last_output_attrs_valid);
}

TEST_BENCH(ApolloSmmuTbuTestBench, GlobalBypassGbpaAbortSuppressesEvent)
{
    constexpr uint64_t iova = 0x14000;
    constexpr uint64_t output_pa = 0x15000;
    constexpr uint64_t map_size = apollo_smmu_tbu::PAGE_SIZE;
    constexpr uint32_t stream_id = 0x52;
    bool downstream_touched = false;

    add_map(stream_id, iova, output_pa, map_size);
    m_tbu.m_eventq.base = 0x76000 | 2;
    m_tbu.m_eventq.prod = 0;
    m_tbu.m_eventq.cons = 0;
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPA),
                apollo_smmu_tbu::ARCH_GBPA_ABORT);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_GBPA_ABORT,
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPA)));

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        downstream_touched = true;
        return mem_read(addr, data, len);
    });

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              stream_read32(stream_id, iova, observed));
    EXPECT_FALSE(downstream_touched);
    EXPECT_TRUE(m_tbu.m_arch_fault_record_suppressed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_NONE, m_tbu.m_arch_fault_reason);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AgbpaUnsupportedRegistersAreRes0)
{
    constexpr uint32_t agbpa_probe = 0xffffffffu;
    constexpr uint32_t gbpa_probe =
        apollo_smmu_tbu::ARCH_GBPA_ABORT |
        (0x3u << apollo_smmu_tbu::ARCH_GBPA_INSTCFG_SHIFT);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_AGBPA), agbpa_probe);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_AGBPA_UNSUPPORTED_RES0,
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_AGBPA)));

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_AGBPA), agbpa_probe);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_AGBPA_UNSUPPORTED_RES0,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_AGBPA)));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPA), gbpa_probe);
    EXPECT_EQ(gbpa_probe, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPA)));
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GBPA), gbpa_probe);
    EXPECT_EQ(gbpa_probe, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GBPA)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, DptUnsupportedRegistersAreRes0)
{
    constexpr uint32_t probe = 0xffffffffu;
    const std::array<uint64_t, 5> dpt_registers {{
        apollo_smmu_tbu::SMMUV3_DPT_BASE_LO,
        apollo_smmu_tbu::SMMUV3_DPT_BASE_HI,
        apollo_smmu_tbu::SMMUV3_DPT_BASE_CFG,
        apollo_smmu_tbu::SMMUV3_DPT_CFG_FAR_LO,
        apollo_smmu_tbu::SMMUV3_DPT_CFG_FAR_HI,
    }};

    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR3)) &
                     apollo_smmu_tbu::ARCH_IDR3_DPT);

    for (const uint64_t off : dpt_registers) {
        reg_write32(smmu_reg(off), probe);
        EXPECT_EQ(apollo_smmu_tbu::ARCH_DPT_UNSUPPORTED_RES0,
                  reg_read32(smmu_reg(off)));
        reg_write32(smmu_s_reg(off), probe);
        EXPECT_EQ(apollo_smmu_tbu::ARCH_DPT_UNSUPPORTED_RES0,
                  reg_read32(smmu_s_reg(off)));
    }
}

TEST_BENCH(ApolloSmmuTbuTestBench, EcmdqUnsupportedRegistersAreRes0)
{
    constexpr uint32_t probe = 0xffffffffu;
    const std::array<uint64_t, 4> ecmdq_registers {{
        apollo_smmu_tbu::SMMUV3_CMDQ_CONTROL_PAGE_BASE_LO,
        apollo_smmu_tbu::SMMUV3_CMDQ_CONTROL_PAGE_BASE_HI,
        apollo_smmu_tbu::SMMUV3_CMDQ_CONTROL_PAGE_CFG,
        apollo_smmu_tbu::SMMUV3_CMDQ_CONTROL_PAGE_STATUS,
    }};
    const std::array<uint64_t, 4> secure_ecmdq_registers {{
        apollo_smmu_tbu::SMMUV3_S_CMDQ_CONTROL_PAGE_BASE_LO,
        apollo_smmu_tbu::SMMUV3_S_CMDQ_CONTROL_PAGE_BASE_HI,
        apollo_smmu_tbu::SMMUV3_S_CMDQ_CONTROL_PAGE_CFG,
        apollo_smmu_tbu::SMMUV3_S_CMDQ_CONTROL_PAGE_STATUS,
    }};

    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR1)) &
                     apollo_smmu_tbu::ARCH_IDR1_ECMDQ);
    EXPECT_EQ(0u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IDR0)) &
                     apollo_smmu_tbu::ARCH_S_IDR0_ECMDQ);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR6), probe);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ECMDQ_UNSUPPORTED_RES0,
              reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_IDR6)));
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IDR6), probe);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ECMDQ_UNSUPPORTED_RES0,
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IDR6)));

    for (const uint64_t off : ecmdq_registers) {
        reg_write32(smmu_reg(off), probe);
        EXPECT_EQ(apollo_smmu_tbu::ARCH_ECMDQ_UNSUPPORTED_RES0,
                  reg_read32(smmu_reg(off)));
    }

    for (const uint64_t off : secure_ecmdq_registers) {
        reg_write32(smmu_reg(off), probe);
        EXPECT_EQ(apollo_smmu_tbu::ARCH_ECMDQ_UNSUPPORTED_RES0,
                  reg_read32(smmu_reg(off)));
    }
}

TEST_BENCH(ApolloSmmuTbuTestBench, MpamAttributesCarryNonSecurePartidSpace)
{
    constexpr uint64_t iova = 0x11000;
    constexpr uint64_t output_pa = 0x12000;
    constexpr uint32_t stream_id = 0x5a;
    constexpr uint16_t partid = 0x1a;
    constexpr uint8_t pmg = 0x3;
    constexpr uint32_t payload = 0xc001d00d;
    constexpr uint32_t gbpmpam_value =
        (static_cast<uint32_t>(pmg) << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        partid;
    bool observed_mpam_valid = false;
    uint8_t observed_partid_space = 0;
    uint32_t observed = 0;

    std::memcpy(&m_memory_bytes[output_pa], &payload, sizeof(payload));
    add_map(stream_id, iova, output_pa, apollo_smmu_tbu::PAGE_SIZE);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | gbpmpam_value);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == output_pa && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_partid_space = ext->mpam_partid_space;
        }
        return mem_read(addr, data, len);
    });

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAM_SPACE_NONSECURE, observed_partid_space);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAM_SPACE_NONSECURE,
              (reg_read32(apollo_smmu_tbu::REG_ARCH_MPAM_STATUS) >> 6) & 0x3u);
}

TEST_BENCH(ApolloSmmuTbuTestBench, MpamAttributesCarrySecurityPartidSpace)
{
    constexpr uint64_t iova = 0x13000;
    constexpr uint64_t output_pa = 0x14000;
    constexpr uint32_t stream_id = 0x5b;
    constexpr uint16_t partid = 0x1b;
    constexpr uint8_t pmg = 0x4;
    constexpr uint32_t payload = 0x51a7e5ec;
    constexpr uint32_t gbpmpam_value =
        (static_cast<uint32_t>(pmg) << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        partid;
    std::array<bool, 4> observed_mpam_valid {};
    std::array<uint8_t, 4> observed_partid_space {};
    std::array<std::pair<uint8_t, uint8_t>, 4> cases {{
        {apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
         apollo_smmu_tbu::ARCH_MPAM_SPACE_NONSECURE},
        {apollo_smmu_tbu::ARCH_SECURITY_SECURE,
         apollo_smmu_tbu::ARCH_MPAM_SPACE_SECURE},
        {apollo_smmu_tbu::ARCH_SECURITY_REALM,
         apollo_smmu_tbu::ARCH_MPAM_SPACE_REALM},
        {apollo_smmu_tbu::ARCH_SECURITY_ROOT,
         apollo_smmu_tbu::ARCH_MPAM_SPACE_ROOT},
    }};

    std::memcpy(&m_memory_bytes[output_pa], &payload, sizeof(payload));
    add_map(stream_id, iova, output_pa, apollo_smmu_tbu::PAGE_SIZE);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | gbpmpam_value);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == output_pa && ext != nullptr) {
            const uint8_t index =
                ext->security_state & apollo_smmu_tbu::ARCH_SECURITY_EVENTQ_STATE_MASK;
            observed_mpam_valid[index] = ext->mpam_valid;
            observed_partid_space[index] = ext->mpam_partid_space;
        }
        return mem_read(addr, data, len);
    });

    for (const auto& test_case : cases) {
        const uint8_t security_state = test_case.first;
        const uint8_t expected_partid_space = test_case.second;
        uint32_t observed = 0;

        EXPECT_EQ(tlm::TLM_OK_RESPONSE,
                  stream_read32_security(stream_id, security_state, iova, observed));
        EXPECT_EQ(payload, observed);
        EXPECT_TRUE(observed_mpam_valid[security_state]);
        EXPECT_EQ(expected_partid_space, observed_partid_space[security_state]);
        EXPECT_EQ(expected_partid_space,
                  (reg_read32(apollo_smmu_tbu::REG_ARCH_MPAM_STATUS) >> 6) & 0x3u);
    }
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureMpamRegisterBanksDriveAttributes)
{
    constexpr uint64_t iova = 0x15000;
    constexpr uint64_t output_pa = 0x16000;
    constexpr uint64_t fetch_pa = 0x17000;
    constexpr uint32_t stream_id = 0x5c;
    constexpr uint16_t ns_partid = 0x11;
    constexpr uint8_t ns_pmg = 0x2;
    constexpr uint16_t secure_partid = 0x1e;
    constexpr uint8_t secure_pmg = 0x6;
    constexpr uint32_t payload = 0x51ecb00c;
    constexpr uint64_t descriptor = 0x0123456789abcdefULL;
    const uint32_t ns_mpam =
        (static_cast<uint32_t>(ns_pmg) << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        ns_partid;
    const uint32_t secure_mpam =
        (static_cast<uint32_t>(secure_pmg)
         << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        secure_partid;
    bool observed_gbp = false;
    bool observed_gmp = false;
    uint16_t observed_gbp_partid = 0;
    uint8_t observed_gbp_pmg = 0;
    uint8_t observed_gbp_space = 0;
    uint16_t observed_gmp_partid = 0;
    uint8_t observed_gmp_pmg = 0;
    uint8_t observed_gmp_space = 0;

    std::memcpy(&m_memory_bytes[output_pa], &payload, sizeof(payload));
    store_u64(fetch_pa, descriptor);
    add_map(stream_id, iova, output_pa, apollo_smmu_tbu::PAGE_SIZE);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GBPMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | ns_mpam);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GBPMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | secure_mpam);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | ns_mpam);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | secure_mpam);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == output_pa && ext != nullptr) {
            observed_gbp = ext->mpam_valid;
            observed_gbp_partid = ext->mpam_partid;
            observed_gbp_pmg = ext->mpam_pmg;
            observed_gbp_space = ext->mpam_partid_space;
        } else if (addr == fetch_pa && ext != nullptr) {
            observed_gmp = ext->mpam_valid;
            observed_gmp_partid = ext->mpam_partid;
            observed_gmp_pmg = ext->mpam_pmg;
            observed_gmp_space = ext->mpam_partid_space;
        }
        return mem_read(addr, data, len);
    });

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_security(stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE,
                                     iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_gbp);
    EXPECT_EQ(secure_partid, observed_gbp_partid);
    EXPECT_EQ(secure_pmg, observed_gbp_pmg);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAM_SPACE_SECURE, observed_gbp_space);
    EXPECT_EQ(secure_partid, m_tbu.m_arch_last_mpam_partid);
    EXPECT_EQ(secure_pmg, m_tbu.m_arch_last_mpam_pmg);

    uint64_t fetched = 0;
    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_SECURE;
    EXPECT_TRUE(m_tbu.read_downstream_u64(fetch_pa, fetched, true, false));
    EXPECT_EQ(descriptor, fetched);
    EXPECT_TRUE(observed_gmp);
    EXPECT_EQ(secure_partid, observed_gmp_partid);
    EXPECT_EQ(secure_pmg, observed_gmp_pmg);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_MPAM_SPACE_SECURE, observed_gmp_space);
}

TEST_BENCH(ApolloSmmuTbuTestBench, GmpamAttributesPropagateOnEventqWrites)
{
    constexpr uint64_t eventq_base = 0x3000;
    constexpr uint16_t partid = 0x1d;
    constexpr uint8_t pmg = 0x4;
    constexpr uint32_t gmpam_value =
        (static_cast<uint32_t>(pmg) << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        partid;
    std::array<uint64_t, 4> words {0x11, 0x22, 0x33, 0x44};
    bool observed_mpam_valid = false;
    bool observed_mpam_unknown = true;
    uint16_t observed_partid = 0;
    uint8_t observed_pmg = 0;

    m_tbu.m_eventq.base = eventq_base | 2;
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | gmpam_value);
    EXPECT_EQ(gmpam_value, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_GMPAM)));

    m_memory.register_write_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == eventq_base && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_mpam_unknown = ext->mpam_unknown;
            observed_partid = ext->mpam_partid;
            observed_pmg = ext->mpam_pmg;
        }
        return mem_write(addr, data, len);
    });

    ASSERT_TRUE(m_tbu.push_arch_queue_record(m_tbu.m_eventq, "EVENTQ",
                                             apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES,
                                             words, words.size(),
                                             apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT));
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_FALSE(observed_mpam_unknown);
    EXPECT_EQ(partid, observed_partid);
    EXPECT_EQ(pmg, observed_pmg);
    EXPECT_EQ(words[0], load_u64(eventq_base));
}

TEST_BENCH(ApolloSmmuTbuTestBench, GmpamAttributesPropagateOnCmdqFetches)
{
    constexpr uint64_t cmdq_base = 0x5000;
    constexpr uint16_t partid = 0x19;
    constexpr uint8_t pmg = 0x3;
    constexpr uint32_t gmpam_value =
        (static_cast<uint32_t>(pmg) << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        partid;
    bool observed_mpam_valid = false;
    bool observed_mpam_unknown = true;
    uint16_t observed_partid = 0;
    uint8_t observed_pmg = 0;

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(cmdq_base + sizeof(uint64_t), 0);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | gmpam_value);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == cmdq_base && ext != nullptr) {
            observed_mpam_valid = ext->mpam_valid;
            observed_mpam_unknown = ext->mpam_unknown;
            observed_partid = ext->mpam_partid;
            observed_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_TRUE(observed_mpam_valid);
    EXPECT_FALSE(observed_mpam_unknown);
    EXPECT_EQ(partid, observed_partid);
    EXPECT_EQ(pmg, observed_pmg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, GmpamAttributesPropagateOnSteAndVmsFetches)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t cd_pa = 0xe0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t vms_ptr = 0x18000;
    constexpr uint64_t iova = 0x3456;
    constexpr uint64_t ipa = 0x23000;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x51;
    constexpr uint16_t partid = 0x18;
    constexpr uint8_t pmg = 0x2;
    constexpr uint16_t virtual_partid = 4;
    constexpr uint16_t physical_partid = 0x17;
    constexpr uint32_t payload = 0x01020304;
    constexpr uint32_t gmpam_value =
        (static_cast<uint32_t>(pmg) << apollo_smmu_tbu::ARCH_MPAM_REG_PMG_SHIFT) |
        partid;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t output_addr =
        (output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
        (iova & (apollo_smmu_tbu::PAGE_SIZE - 1));
    bool observed_ste_mpam_valid = false;
    bool observed_vms_mpam_valid = false;
    uint16_t observed_ste_partid = 0;
    uint16_t observed_vms_partid = 0;
    uint8_t observed_ste_pmg = 0;
    uint8_t observed_vms_pmg = 0;

    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    stage_translation_tables(s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s1ttb, iova, ipa);
    stage_translation_tables(s2ttb, ipa, output_pa);

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1MPAM);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    store_u64(ste_pa + apollo_smmu_tbu::ARCH_STE_VMSPTR_OFFSET,
              vms_ptr & apollo_smmu_tbu::ARCH_STE_VMSPTR_MASK);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t),
              s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(cd_pa + apollo_smmu_tbu::ARCH_CD_MPAM_WORD_OFFSET,
              static_cast<uint64_t>(virtual_partid)
                  << apollo_smmu_tbu::ARCH_CD_PARTID_SHIFT);
    store_u64(vms_ptr + (virtual_partid / 4) * sizeof(uint64_t),
              static_cast<uint64_t>(physical_partid)
                  << ((virtual_partid % 4) * 16));
    std::memcpy(&m_memory_bytes[output_addr], &payload, sizeof(payload));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GMPAM),
                apollo_smmu_tbu::ARCH_MPAM_UPDATE | gmpam_value);

    m_memory.register_read_cb([&](uint64_t addr, uint8_t* data, size_t len) {
        auto* ext =
            m_memory.get_cur_txn().get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (addr == ste_pa && ext != nullptr) {
            observed_ste_mpam_valid = ext->mpam_valid;
            observed_ste_partid = ext->mpam_partid;
            observed_ste_pmg = ext->mpam_pmg;
        }
        if (addr == vms_ptr && ext != nullptr) {
            observed_vms_mpam_valid = ext->mpam_valid;
            observed_vms_partid = ext->mpam_partid;
            observed_vms_pmg = ext->mpam_pmg;
        }
        return mem_read(addr, data, len);
    });

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    uint32_t observed = 0;
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_TRUE(observed_ste_mpam_valid);
    EXPECT_TRUE(observed_vms_mpam_valid);
    EXPECT_EQ(partid, observed_ste_partid);
    EXPECT_EQ(partid, observed_vms_partid);
    EXPECT_EQ(pmg, observed_ste_pmg);
    EXPECT_EQ(pmg, observed_vms_pmg);
    EXPECT_EQ(physical_partid, m_tbu.m_arch_last_mpam_partid);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsTranslationRequestHonorsCr2RecCfgAtsAndRecInvsid)
{
    constexpr uint64_t strtab_base = 0x2000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t priq_base = 0xa000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t bad_stream_id = 4;
    constexpr uint32_t one_bit_strtab = 1;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO, apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO, apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI,
                priq_base | 3);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2), 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_ATS_TREQ,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, m_tbu.m_fault_count);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);
    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL, apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_ATS_TREQ,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO, apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI,
                strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), one_bit_strtab);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, bad_stream_id);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_RECINVSID);
    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL, apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, m_tbu.m_fault_count);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_RECINVSID |
                    apollo_smmu_tbu::ARCH_CR2_REC_CFG_ATS);
    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL, apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STREAMID, load_u64(eventq_base) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, BadStreamIdHonorsCr2RecInvsidForEventRecording)
{
    constexpr uint64_t strtab_base = 0x2000;
    constexpr uint64_t eventq_base = 0x9600;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t bad_stream_id = 4;
    constexpr uint32_t one_bit_strtab = 1;

    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG),
                one_bit_strtab);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, bad_stream_id);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2), 0);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base));

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR2),
                apollo_smmu_tbu::ARCH_CR2_RECINVSID);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STREAMID,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(bad_stream_id,
              static_cast<uint32_t>(load_u64(eventq_base) >> 32));
    EXPECT_EQ(0u, load_u64(eventq_base + 8));
    EXPECT_EQ(0u, load_u64(eventq_base + 16));
    EXPECT_EQ(0u, load_u64(eventq_base + 24));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedConfigEventPayloadsAreRes0)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t eventq_base = 0x9800;
    constexpr uint64_t iova = 0x456000;
    constexpr uint32_t stream_id = 2;
    constexpr uint32_t s1cdmax = 4;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    const auto expect_res0_record =
        [this, eventq_base](uint32_t index, uint32_t event, uint32_t sid) {
            const uint64_t record_base =
                eventq_base + index * apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;

            EXPECT_EQ(event, load_u64(record_base) & 0xffu);
            EXPECT_EQ(sid, static_cast<uint32_t>(load_u64(record_base) >> 32));
            EXPECT_EQ(0u, load_u64(record_base + 8));
            EXPECT_EQ(0u, load_u64(record_base + 16));
            EXPECT_EQ(0u, load_u64(record_base + 24));
        };

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN);

    store_u64(ste_pa, 0);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    expect_res0_record(0, apollo_smmu_tbu::ARCH_EVENT_C_BAD_STE, stream_id);

    store_u64(ste_pa, arch_s1_ste(cd_base));
    store_u64(cd_base, 0);
    store_u64(cd_base + sizeof(uint64_t), 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_CD_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    expect_res0_record(1, apollo_smmu_tbu::ARCH_EVENT_C_BAD_CD, stream_id);

    store_u64(ste_pa, arch_s1_ste(cd_base) |
                      (static_cast<uint64_t>(s1cdmax)
                       << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT));
    store_u64(ste_pa + sizeof(uint64_t),
              apollo_smmu_tbu::ARCH_STE_S1DSS_TERMINATE);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STREAM_DISABLED,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    expect_res0_record(2, apollo_smmu_tbu::ARCH_EVENT_F_STREAM_DISABLED,
                       stream_id);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdPriRespUnknownPrgIsAccounted)
{
    constexpr uint64_t cmdq_base = 0xb000;

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_PRI_RESP);
    store_u64(cmdq_base + sizeof(uint64_t), 0x7777);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_unknown);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_EQ(0u, m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);
    EXPECT_EQ(0u, m_tbu.m_arch_fault_replay.last_pri_response_ats_status);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdPriRespReservedResponseSetsCerrorIll)
{
    constexpr uint64_t cmdq_base = 0xb400;
    constexpr uint32_t stream_id = 1;
    constexpr uint64_t iova = 0x1800;
    constexpr uint8_t reserved_response = 0x3;

    const uint16_t prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    ASSERT_NE(0u, prg);

    store_u64(cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
                  (static_cast<uint64_t>(stream_id) << 32));
    store_u64(cmdq_base + sizeof(uint64_t),
              prg | (static_cast<uint64_t>(reserved_response) << 16));

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(0u, m_tbu.m_arch_pri_responses);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureCmdPriRespReservedResponseSetsCerrorIll)
{
    constexpr uint64_t secure_cmdq_base = 0xcc00;
    constexpr uint32_t stream_id = 2;
    constexpr uint64_t iova = 0x2800;
    constexpr uint8_t reserved_response = 0x3;

    const uint16_t prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_CA);
    ASSERT_NE(0u, prg);

    store_u64(secure_cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
                  (static_cast<uint64_t>(stream_id) << 32));
    store_u64(secure_cmdq_base + sizeof(uint64_t),
              prg | (static_cast<uint64_t>(reserved_response) << 16));

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                  secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t cons =
        reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(0u, m_tbu.m_arch_pri_responses);
    EXPECT_NE(0u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERROR)) &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureCmdPriRespIgnoresSsecAndTargetsNonSecure)
{
    constexpr uint64_t secure_cmdq_base = 0xcd00;
    constexpr uint32_t stream_id = 3;
    constexpr uint64_t iova = 0x3800;

    const uint16_t prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS);
    ASSERT_NE(0u, prg);

    store_u64(secure_cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
                  apollo_smmu_tbu::ARCH_CMDQ_SSEC |
                  (static_cast<uint64_t>(stream_id) << 32));
    store_u64(secure_cmdq_base + sizeof(uint64_t),
              prg | (static_cast<uint64_t>(
                         apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT)
                     << 16));

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                  secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t cons =
        reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(1u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_NONE,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
              m_tbu.m_arch_last_cmd_security_state);
    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_EQ(stream_id,
              m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);
    EXPECT_EQ(0u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERROR)) &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdPriRespRequiresHeadPrgOrdering)
{
    constexpr uint64_t cmdq_base = 0xb800;
    constexpr uint32_t first_stream_id = 1;
    constexpr uint32_t second_stream_id = 2;
    constexpr uint64_t first_iova = 0x1000;
    constexpr uint64_t second_iova = 0x2000;

    const uint16_t first_prg =
        m_tbu.allocate_prg(first_stream_id, first_iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS);
    const uint16_t second_prg =
        m_tbu.allocate_prg(second_stream_id, second_iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    ASSERT_NE(0u, first_prg);
    ASSERT_NE(0u, second_prg);
    ASSERT_NE(first_prg, second_prg);
    EXPECT_EQ(2u, m_tbu.arch_pri_pending_count());

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_PRI_RESP);
    store_u64(cmdq_base + sizeof(uint64_t),
              second_prg |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT)
                   << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(2u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_rejected);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_unknown);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_order_mismatch);
    EXPECT_EQ(first_prg, m_tbu.m_arch_fault_replay.last_pri_response_head_prg);
    EXPECT_EQ(0u, m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);
    EXPECT_TRUE(m_tbu.pri_prg_pending(first_prg));
    EXPECT_TRUE(m_tbu.pri_prg_pending(second_prg));

    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              first_prg |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT)
                   << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(2u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_unknown);
    EXPECT_EQ(0u, m_tbu.m_arch_pri_rejected);
    EXPECT_EQ(first_prg, m_tbu.m_arch_last_prg);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_order_mismatch);
    EXPECT_EQ(first_stream_id,
              m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS,
              m_tbu.m_arch_fault_replay.last_pri_response_ats_status);

    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP);
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              second_prg |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT)
                   << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 3);

    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(3u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_unknown);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_rejected);
    EXPECT_EQ(second_prg, m_tbu.m_arch_last_prg);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_EQ(second_stream_id,
              m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_UR,
              m_tbu.m_arch_fault_replay.last_pri_response_ats_status);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdPriRespAdvancesPriqConsForHeadRequest)
{
    constexpr uint64_t priq_base = 0xd000;
    constexpr uint64_t cmdq_base = 0xd400;
    constexpr uint32_t stream_id = 1;
    constexpr uint64_t iova = 0x5000;
    constexpr uint64_t pa = 0x85000;

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    m_tbu.m_priq.base = priq_base | 2;
    m_tbu.m_priq.prod = 0;
    m_tbu.m_priq.cons = 0;

    const uint16_t prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS);
    ASSERT_NE(0u, prg);
    m_tbu.push_pri_protocol_record(stream_id, prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE);

    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_PROD)) &
                     apollo_smmu_tbu::ARCH_QUEUE_INDEX_MASK);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_CONS)) &
                     apollo_smmu_tbu::ARCH_QUEUE_INDEX_MASK);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_PRIQ);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
                            (static_cast<uint64_t>(stream_id) << 32));
    store_u64(cmdq_base + sizeof(uint64_t),
              prg | (static_cast<uint64_t>(
                         apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT)
                     << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_CONS)) &
                     apollo_smmu_tbu::ARCH_QUEUE_INDEX_MASK);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_PRIQ);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_EQ(prg, m_tbu.m_arch_fault_replay.last_pri_response_head_prg);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdPriRespIgnoredWhenSmmuenDisabled)
{
    constexpr uint64_t priq_base = 0xd800;
    constexpr uint64_t cmdq_base = 0xdc00;
    constexpr uint32_t stream_id = 1;
    constexpr uint64_t iova = 0x6000;
    constexpr uint64_t pa = 0x86000;

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    m_tbu.m_priq.base = priq_base | 2;
    m_tbu.m_priq.prod = 0;
    m_tbu.m_priq.cons = 0;

    const uint16_t prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS);
    ASSERT_NE(0u, prg);
    m_tbu.push_pri_protocol_record(stream_id, prg,
                                   apollo_smmu_tbu::ARCH_ATS_RESP_SUCCESS,
                                   iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_PROD)) &
                     apollo_smmu_tbu::ARCH_QUEUE_INDEX_MASK);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
                            (static_cast<uint64_t>(stream_id) << 32));
    store_u64(cmdq_base + sizeof(uint64_t),
              prg | (static_cast<uint64_t>(
                         apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT)
                     << 16));

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_CMDQEN);
    ASSERT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0ACK)) &
                     apollo_smmu_tbu::ARCH_CR0_SMMUEN);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t disabled_cons =
        reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, disabled_cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_NONE,
              (disabled_cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_TRUE(m_tbu.pri_prg_pending(prg));
    EXPECT_EQ(0u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(0u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_CONS)) &
                     apollo_smmu_tbu::ARCH_QUEUE_INDEX_MASK);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_CONS)) &
                     apollo_smmu_tbu::ARCH_QUEUE_INDEX_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdPriRespHonorsStreamIdQualifier)
{
    constexpr uint64_t cmdq_base = 0xc000;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t wrong_stream_id = 2;
    constexpr uint64_t iova = 0x3000;

    const uint16_t prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR);
    ASSERT_NE(0u, prg);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    store_u64(cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
                  (static_cast<uint64_t>(wrong_stream_id) << 32));
    store_u64(cmdq_base + sizeof(uint64_t),
              prg |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT)
                   << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_unknown);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_stream_mismatch);
    EXPECT_EQ(wrong_stream_id,
              m_tbu.m_arch_fault_replay.last_pri_response_cmd_stream_id);
    EXPECT_EQ(0u, m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);

    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
                  (static_cast<uint64_t>(stream_id) << 32));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              prg |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT)
                   << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(2u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_unknown);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_stream_mismatch);
    EXPECT_EQ(stream_id,
              m_tbu.m_arch_fault_replay.last_pri_response_cmd_stream_id);
    EXPECT_EQ(stream_id,
              m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_UR,
              m_tbu.m_arch_fault_replay.last_pri_response_ats_status);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdPriRespHonorsSsidQualifier)
{
    constexpr uint64_t cmdq_base = 0xc800;
    constexpr uint32_t stream_id = 3;
    constexpr uint32_t ssid = 0x12345;
    constexpr uint32_t wrong_ssid = 0x12346;
    constexpr uint64_t iova = 0x4000;

    const auto pri_resp_cmd = [](uint32_t sid, bool ssv, uint32_t substream) {
        return apollo_smmu_tbu::ARCH_CMD_PRI_RESP |
               (static_cast<uint64_t>(sid) << 32) |
               (ssv ? apollo_smmu_tbu::ARCH_CMDQ_SSV : 0) |
               (ssv ? (static_cast<uint64_t>(substream &
                                             apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK)
                       << apollo_smmu_tbu::ARCH_CMDQ_SSID_SHIFT)
                    : 0);
    };

    const uint16_t prg =
        m_tbu.allocate_prg(stream_id, iova,
                           apollo_smmu_tbu::ARCH_ATS_RESP_UR, true, ssid);
    ASSERT_NE(0u, prg);
    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    store_u64(cmdq_base, pri_resp_cmd(stream_id, true, wrong_ssid));
    store_u64(cmdq_base + sizeof(uint64_t),
              prg |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT)
                   << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(1u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_unknown);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_stream_mismatch);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_ssid_mismatch);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_cmd_ssid_valid);
    EXPECT_EQ(wrong_ssid,
              m_tbu.m_arch_fault_replay.last_pri_response_cmd_ssid);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_ssid_valid);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_REJECT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);

    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              pri_resp_cmd(stream_id, true, ssid));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              prg |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT)
                   << 16));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(0u, m_tbu.arch_pri_pending_count());
    EXPECT_EQ(2u, m_tbu.m_arch_pri_responses);
    EXPECT_EQ(1u, m_tbu.m_arch_pri_unknown);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_valid);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_unknown);
    EXPECT_FALSE(m_tbu.m_arch_fault_replay.last_pri_response_ssid_mismatch);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_cmd_ssid_valid);
    EXPECT_EQ(ssid, m_tbu.m_arch_fault_replay.last_pri_response_cmd_ssid);
    EXPECT_TRUE(m_tbu.m_arch_fault_replay.last_pri_response_ssid_valid);
    EXPECT_EQ(ssid, m_tbu.m_arch_fault_replay.last_pri_response_ssid);
    EXPECT_EQ(stream_id,
              m_tbu.m_arch_fault_replay.last_pri_response_stream_id);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_PRI_RESP_ACCEPT,
              m_tbu.m_arch_fault_replay.last_pri_response_code);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_ATS_RESP_UR,
              m_tbu.m_arch_fault_replay.last_pri_response_ats_status);
}

TEST_BENCH(ApolloSmmuTbuTestBench, FaultReplayRecordsSyndromeAndResumeState)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t ttbr = 0x4000;
    constexpr uint64_t output_base = 0x8000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint64_t iova = 0x123;
    constexpr uint32_t stream_id = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base);

    store_u64(ste_pa, ste);
    store_u64(cd_base, 0);
    store_u64(cd_base + sizeof(uint64_t), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO, apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 3);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ((static_cast<uint64_t>(stream_id) << 32) |
                  apollo_smmu_tbu::ARCH_EVENT_C_BAD_CD,
              load_u64(eventq_base));
    uint64_t event_word1 = load_u64(eventq_base + 8);
    uint16_t stag = static_cast<uint16_t>(event_word1 &
                                          apollo_smmu_tbu::ARCH_EVENT_STAG_MASK);
    EXPECT_NE(0u, stag);
    EXPECT_NE(0u, event_word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(iova, load_u64(eventq_base + 16));
    EXPECT_EQ(stag, m_tbu.m_arch_last_stag);
    EXPECT_EQ(0u, load_u64(eventq_base + 24));
    uint32_t detail = reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_CD_INVALID, detail & 0xffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_CLASS_CD, (detail >> 8) & 0xffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, (detail >> 16) & 0xfu);
    EXPECT_NE(0u, detail & (apollo_smmu_tbu::ARCH_FAULT_ATTR_STALL << 20));
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);

    store_u64(cmdq_base, (static_cast<uint64_t>(stream_id) << 32) |
                             (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                              << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                             apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + sizeof(uint64_t), stag);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 8) & 0xffu);
    EXPECT_EQ(stag, m_tbu.m_arch_last_resume_stag);

    stage_translation_tables(ttbr, iova, output_base);
    const uint64_t l3_table = ttbr + 3 * apollo_smmu_tbu::PAGE_SIZE;
    const uint64_t l3_index = apollo_smmu_tbu::arch_level_index(iova, 3);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(l3_table + l3_index * sizeof(uint64_t), 0);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    EXPECT_EQ(2u, m_tbu.m_eventq.prod);
    EXPECT_EQ((static_cast<uint64_t>(stream_id) << 32) |
                  apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION,
              load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES));
    event_word1 = load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES + 8);
    stag = static_cast<uint16_t>(event_word1 & apollo_smmu_tbu::ARCH_EVENT_STAG_MASK);
    EXPECT_NE(0u, stag);
    EXPECT_NE(0u, event_word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(iova, load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES + 16));
    EXPECT_EQ(0u, load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES + 24));
    detail = reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_PAGE_INVALID, detail & 0xffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_CLASS_TRANSLATION, (detail >> 8) & 0xffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);

    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              (static_cast<uint64_t>(stream_id) << 32) |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_TERMINATE)
                   << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                  apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t),
              stag);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 16) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdResumeMatchesStreamIdAndStag)
{
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint32_t stream_id = 7;
    constexpr uint32_t other_stream_id = 8;
    constexpr uint16_t stag = 0x55;

    m_tbu.m_arch_stalls[0] = {stream_id, stag, 0x1000, 0, false, true};
    m_tbu.m_arch_stall_pending = 1;

    store_u64(cmdq_base, (static_cast<uint64_t>(other_stream_id) << 32) |
                             (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                              << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                             apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + sizeof(uint64_t), stag);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              (static_cast<uint64_t>(stream_id) << 32) |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                   << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                  apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t),
              stag + 1);
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              (static_cast<uint64_t>(stream_id) << 32) |
                  (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                   << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                  apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t),
              stag);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 24) & 0xffu);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(2u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 24) & 0xffu);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 3);

    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 8) & 0xffu);
    EXPECT_EQ(stag, m_tbu.m_arch_last_resume_stag);
}

TEST_BENCH(ApolloSmmuTbuTestBench, StalledFaultsSuppressDuplicateEventRecords)
{
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t iova = 0x5000;
    constexpr uint32_t stream_id = 0x12;
    constexpr uint32_t ssid = 0x34;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 5);
    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_PAGE_INVALID;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S1;

    const uint16_t first_stag =
        m_tbu.record_fault(stream_id, "read", iova, sizeof(uint32_t),
                           true, true, ssid);
    ASSERT_NE(0u, first_stag);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);

    const uint16_t merged_stag =
        m_tbu.record_fault(stream_id, "read", iova, sizeof(uint32_t),
                           true, true, ssid);
    EXPECT_EQ(first_stag, merged_stag);
    EXPECT_EQ(first_stag, m_tbu.m_arch_last_stag);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_MERGE_STATUS) & 0xffffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_MERGE_STATUS) >> 16);
    EXPECT_EQ(0u, load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES));

    const uint16_t second_stag =
        m_tbu.record_fault(stream_id, "read", iova + apollo_smmu_tbu::PAGE_SIZE,
                           sizeof(uint32_t), true, true, ssid);
    EXPECT_NE(0u, second_stag);
    EXPECT_NE(first_stag, second_stag);
    EXPECT_EQ(2u, m_tbu.m_eventq.prod);
    EXPECT_EQ(2u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordLayoutCarriesSubstream)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t iova = 0x456000;
    constexpr uint32_t stream_id = 2;
    constexpr uint32_t ssid = 0x25;
    constexpr uint32_t s1cdmax = 6;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t cd_pa = cd_base + ssid * apollo_smmu_tbu::ARCH_CD_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base) |
                         (static_cast<uint64_t>(s1cdmax)
                          << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT);

    store_u64(ste_pa, ste);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(cd_pa, 0);
    store_u64(cd_pa + sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, ssid | (1u << 31));
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO, apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                eventq_base | 3);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    const uint64_t word0 = load_u64(eventq_base);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_CD, word0 & 0xffu);
    EXPECT_NE(0u, word0 & (1ULL << 11));
    EXPECT_EQ(ssid, (word0 >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
    EXPECT_NE(0u, load_u64(eventq_base + 8) & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(m_tbu.m_arch_last_stag,
              load_u64(eventq_base + 8) & apollo_smmu_tbu::ARCH_EVENT_STAG_MASK);
    EXPECT_EQ(iova, load_u64(eventq_base + 16));
    EXPECT_EQ(0u, load_u64(eventq_base + 24));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_CD_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedStreamDisabledAndBadSubstreamEvents)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t cd_base = 0x3000;
    constexpr uint64_t eventq_base = 0x9400;
    constexpr uint64_t iova = 0x456000;
    constexpr uint32_t stream_id = 0x4d;
    constexpr uint32_t s1cdmax = 4;
    constexpr uint32_t bad_ssid = 1u << s1cdmax;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t ste = arch_s1_ste(cd_base) |
                         (static_cast<uint64_t>(s1cdmax)
                          << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT);

    store_u64(ste_pa, ste);
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_TERMINATE);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, 0);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    const uint64_t disabled_word0 = load_u64(eventq_base);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_STREAM_DISABLED, disabled_word0 & 0xffu);
    EXPECT_EQ(0u, disabled_word0 & (1ULL << 11));
    EXPECT_EQ(stream_id, static_cast<uint32_t>(disabled_word0 >> 32));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STREAM_DISABLED,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);

    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI,
                iova + apollo_smmu_tbu::PAGE_SIZE);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, (1u << 31) | bad_ssid);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    const uint64_t bad_substream_record =
        eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;
    const uint64_t bad_substream_word0 = load_u64(bad_substream_record);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_SUBSTREAMID,
              bad_substream_word0 & 0xffu);
    EXPECT_NE(0u, bad_substream_word0 & (1ULL << 11));
    EXPECT_EQ(bad_ssid, (bad_substream_word0 >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(bad_substream_word0 >> 32));
    EXPECT_NE(0u, load_u64(bad_substream_record + 8) & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(iova + apollo_smmu_tbu::PAGE_SIZE, load_u64(bad_substream_record + 16));
    EXPECT_EQ(0u, load_u64(bad_substream_record + 24));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_BAD_SUBSTREAMID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);

    // C_BAD_SUBSTREAMID has an architected InputAddr payload; do not RES0 it.
    const uint64_t probe_iova = iova + 2 * apollo_smmu_tbu::PAGE_SIZE;
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, probe_iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    const uint64_t bad_substream_probe_record =
        eventq_base + 2 * apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_SUBSTREAMID,
              load_u64(bad_substream_probe_record) & 0xffu);
    EXPECT_EQ(bad_ssid,
              (load_u64(bad_substream_probe_record) >> 12) &
                  apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(probe_iova, load_u64(bad_substream_probe_record + 8));
    EXPECT_EQ(0u, load_u64(bad_substream_probe_record + 16));
    EXPECT_EQ(0u, load_u64(bad_substream_probe_record + 24));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedConfigDisabledSuppressesEvents)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t eventq_base = 0x9600;
    constexpr uint64_t iova = 0x456000;
    constexpr uint32_t stream_id = 0x21;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    store_u64(ste_pa, apollo_smmu_tbu::ARCH_STE_VALID);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STREAM_DISABLED,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_FAULT_STATUS));
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base));
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsConfigDisabledReturnsUrWithoutEvent)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t eventq_base = 0x9680;
    constexpr uint64_t iova = 0x457000;
    constexpr uint32_t stream_id = 0x22;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    store_u64(ste_pa, apollo_smmu_tbu::ARCH_STE_VALID);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_ATS_TRANSLATION_REQUEST);

    const uint32_t ats_detail = reg_read32(apollo_smmu_tbu::REG_ARCH_ATS_DETAIL);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STREAM_DISABLED,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, ats_detail & 0xffu);
    EXPECT_EQ(1u, (ats_detail >> 8) & 0xffu);
    EXPECT_EQ(0u, (ats_detail >> 16) & 0xffu);
    EXPECT_EQ(1u, (ats_detail >> 24) & 0xffu);
    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_FAULT_STATUS));
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedConflictEventsAreMapped)
{
    constexpr uint64_t eventq_base = 0x9700;
    constexpr uint64_t iova = 0x567000;
    constexpr uint32_t stream_id = 0x31;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);

    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_TLB_CONFLICT;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S1;
    m_tbu.record_fault(stream_id, "tlb-conflict", iova,
                       apollo_smmu_tbu::PAGE_SIZE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TLB_CONFLICT,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(load_u64(eventq_base) >> 32));
    EXPECT_EQ(iova, load_u64(eventq_base + 8));
    EXPECT_EQ(0u, load_u64(eventq_base + 16));
    EXPECT_TRUE(apollo_smmu_tbu::arch_event_record_has_conflict_reason(
        apollo_smmu_tbu::ARCH_EVENT_F_TLB_CONFLICT));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CONFLICT_REASON_TLB_TAG_MISMATCH,
              load_u64(eventq_base + 24));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TLB_CONFLICT,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);

    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_CFG_CONFLICT;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S1;
    m_tbu.record_fault(stream_id + 1, "cfg-conflict",
                       iova + apollo_smmu_tbu::PAGE_SIZE,
                       apollo_smmu_tbu::PAGE_SIZE);

    const uint64_t cfg_record = eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_CFG_CONFLICT,
              load_u64(cfg_record) & 0xffu);
    EXPECT_EQ(stream_id + 1, static_cast<uint32_t>(load_u64(cfg_record) >> 32));
    EXPECT_EQ(iova + apollo_smmu_tbu::PAGE_SIZE, load_u64(cfg_record + 8));
    EXPECT_EQ(0u, load_u64(cfg_record + 16));
    EXPECT_TRUE(apollo_smmu_tbu::arch_event_record_has_conflict_reason(
        apollo_smmu_tbu::ARCH_EVENT_F_CFG_CONFLICT));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CONFLICT_REASON_CFG_STE_CONT,
              load_u64(cfg_record + 24));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_CFG_CONFLICT,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, AtsCacheConflictProbeRecordsAndRecovers)
{
    constexpr uint64_t eventq_base = 0x9780;
    constexpr uint64_t iova = 0x568000;
    constexpr uint32_t stream_id = 0x32;
    constexpr uint32_t ssid = 0x44;
    const uint64_t page = apollo_smmu_tbu::page_base(iova);

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, (1u << 31) | ssid);

    EXPECT_FALSE(m_tbu.ats_cache_conflict_present(stream_id, page, 0x11, 0x20,
                                                  true, ssid));

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_TLB_CONFLICT);

    const uint64_t word0 = load_u64(eventq_base);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_TLB_CONFLICT,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TLB_CONFLICT, word0 & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
    EXPECT_NE(0ULL, word0 & (1ULL << 11));
    EXPECT_EQ(ssid, static_cast<uint32_t>((word0 >> 12) &
                                          apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK));
    EXPECT_EQ(iova, load_u64(eventq_base + 8));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CONFLICT_REASON_TLB_TAG_MISMATCH,
              load_u64(eventq_base + 24));
    EXPECT_EQ(0u, m_tbu.m_ats_entries);
    EXPECT_EQ(1u, m_tbu.m_arch_tlb_conflict_recoveries);
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, page, 0x10, 0x20, true, ssid));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ConfigCacheConflictProbeRecordsAndRecovers)
{
    constexpr uint64_t eventq_base = 0x97c0;
    constexpr uint64_t iova = 0x569000;
    constexpr uint32_t stream_id = 0x35;
    constexpr uint32_t span_log2 = 2;
    constexpr uint32_t secure_state = apollo_smmu_tbu::ARCH_SECURITY_SECURE;
    constexpr uint32_t nonsecure_state = apollo_smmu_tbu::ARCH_SECURITY_NONSECURE;
    constexpr uint64_t base_ste0 =
        apollo_smmu_tbu::ARCH_STE_VALID |
        (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_CFG_S1_TRANS)
         << apollo_smmu_tbu::ARCH_STE_CFG_SHIFT);
    constexpr uint64_t stale_ste0 = base_ste0;
    constexpr uint64_t request_ste0 =
        base_ste0 | (1ULL << apollo_smmu_tbu::ARCH_START_LEVEL_SHIFT);

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);

    m_tbu.clear_config_cache();
    m_tbu.config_cache_fill(stream_id, secure_state, stale_ste0, 0x4000, 0,
                            span_log2);
    EXPECT_FALSE(m_tbu.config_cache_conflict_present(stream_id, nonsecure_state,
                                                     request_ste0, 0x4000, 0,
                                                     span_log2));
    m_tbu.clear_config_cache();

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_CFG_CONFLICT);

    const uint64_t word0 = load_u64(eventq_base);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_CFG_CONFLICT,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_CFG_CONFLICT, word0 & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
    EXPECT_EQ(iova, load_u64(eventq_base + 8));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CONFLICT_REASON_CFG_STE_CONT,
              load_u64(eventq_base + 24));
    EXPECT_EQ(1u, m_tbu.m_arch_cfg_conflict_recoveries);
    EXPECT_FALSE(m_tbu.config_cache_conflict_present(stream_id, nonsecure_state,
                                                     request_ste0, 0x4000, 0,
                                                     span_log2));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedUnsupportedUpstreamEventCanBeInjected)
{
    constexpr uint64_t eventq_base = 0x96c0;
    constexpr uint64_t iova = 0x458000;
    constexpr uint32_t stream_id = 0x23;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_RECORD_F_UUT);

    const uint64_t word0 = load_u64(eventq_base);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_UUT, word0 & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
    EXPECT_EQ(0u, load_u64(eventq_base + 8));
    EXPECT_EQ(0u, load_u64(eventq_base + 16));
    EXPECT_EQ(0u, load_u64(eventq_base + 24));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_UNSUPPORTED_UPSTREAM,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_FAULT_STATUS));
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureRealmRootEndpointAcceptedInvalidRejectedBeforeTranslation)
{
    constexpr uint64_t eventq_base = 0x9740;
    constexpr uint64_t iova = 0x12000;
    constexpr uint64_t pa = 0x13000;
    constexpr uint32_t payload = 0x5ec51001;
    constexpr uint32_t stream_id = 0x61;
    constexpr std::array<uint8_t, 3> supported_states = {
        apollo_smmu_tbu::ARCH_SECURITY_SECURE,
        apollo_smmu_tbu::ARCH_SECURITY_REALM,
        apollo_smmu_tbu::ARCH_SECURITY_ROOT,
    };
    constexpr std::array<uint8_t, 1> unsupported_states = {
        0xffu,
    };
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    std::memcpy(&m_memory_bytes[pa], &payload, sizeof(payload));
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    for (const uint8_t state : supported_states) {
        SCOPED_TRACE(static_cast<unsigned int>(state));

        EXPECT_EQ(tlm::TLM_OK_RESPONSE,
                  stream_read32_security(stream_id, state, iova, observed));
        EXPECT_EQ(payload, observed);
        EXPECT_EQ(state, reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & 0x3u);
        EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & (1u << 8));
        observed = 0;
    }

    for (size_t i = 0; i < unsupported_states.size(); i++) {
        const uint8_t state = unsupported_states[i];
        SCOPED_TRACE(static_cast<unsigned int>(state));

        EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
                  stream_read32_security(stream_id, state, iova, observed));

        const uint64_t entry = eventq_base + i * apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;
        const uint64_t word0 = load_u64(entry);

        EXPECT_EQ(0u, observed);
        EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
                  reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
        EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_UNSUPPORTED_UPSTREAM,
                  reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
        EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_UUT, word0 & 0xffu);
        EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
        EXPECT_EQ(0u, load_u64(entry + 8));
        EXPECT_EQ(0u, load_u64(entry + 16));
        EXPECT_EQ(0u, load_u64(entry + 24));
        EXPECT_EQ(state & apollo_smmu_tbu::ARCH_SECURITY_EVENTQ_STATE_MASK,
                  reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & 0x3u);
        EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & (1u << 8));
        const uint8_t route_state =
            state & apollo_smmu_tbu::ARCH_SECURITY_EVENTQ_STATE_MASK;
        EXPECT_EQ(route_state, m_tbu.m_arch_last_event_security_state);
        EXPECT_EQ(state & apollo_smmu_tbu::ARCH_SECURITY_EVENTQ_STATE_MASK,
                  (reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) >>
                   apollo_smmu_tbu::ARCH_SECURITY_EVENTQ_STATE_SHIFT) &
                      apollo_smmu_tbu::ARCH_SECURITY_EVENTQ_STATE_MASK);
        EXPECT_EQ(1u, m_tbu.arch_security_eventq_count(route_state));
        const auto& bank = m_tbu.arch_security_eventq_bank_state(state);
        EXPECT_TRUE(bank.valid);
        EXPECT_EQ(1u, bank.records);
        EXPECT_EQ(word0, bank.last_record[0]);
        EXPECT_EQ(0u, bank.last_record[1]);
        EXPECT_EQ(0u, bank.last_record[2]);
        EXPECT_EQ(0u, bank.last_record[3]);
        EXPECT_EQ(entry, bank.last_guest_record_addr);
        EXPECT_EQ(static_cast<uint32_t>(i + 1), bank.last_prod);
    }
    EXPECT_EQ(static_cast<uint32_t>(unsupported_states.size()), m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, m_tbu.arch_security_eventq_count(
                      apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    EXPECT_FALSE(m_tbu.arch_security_eventq_bank_state(
                      apollo_smmu_tbu::ARCH_SECURITY_NONSECURE).valid);
    EXPECT_EQ(0u, m_tbu.m_arch_eventq_secure_records);
    EXPECT_EQ(0u, m_tbu.m_arch_eventq_realm_records);
    EXPECT_EQ(1u, m_tbu.m_arch_eventq_root_records);

    EXPECT_EQ(tlm::TLM_OK_RESPONSE,
              stream_read32_security(stream_id, apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
                                     iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
              reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & 0x3u);
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & (1u << 8));

    for (const uint8_t state : supported_states) {
        SCOPED_TRACE(static_cast<unsigned int>(state));

        EXPECT_EQ(sizeof(observed),
                  stream_dbg_read32_security(stream_id, state, iova, observed));
        EXPECT_EQ(payload, observed);
        EXPECT_EQ(state, reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & 0x3u);
        EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & (1u << 8));
        observed = 0;
    }

    for (const uint8_t state : unsupported_states) {
        SCOPED_TRACE(static_cast<unsigned int>(state));

        EXPECT_EQ(0u, stream_dbg_read32_security(stream_id, state, iova, observed));
        EXPECT_EQ(0u, observed);
        EXPECT_EQ(state & apollo_smmu_tbu::ARCH_SECURITY_EVENTQ_STATE_MASK,
                  reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & 0x3u);
        EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & (1u << 8));
    }

    EXPECT_EQ(sizeof(observed),
              stream_dbg_read32_security(stream_id,
                                         apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
                                         iova, observed));
    EXPECT_EQ(payload, observed);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
              reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & 0x3u);
    EXPECT_NE(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_SECURITY_STATUS) & (1u << 8));
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureEventsUseConfiguredEventqBank)
{
    constexpr uint64_t nonsecure_eventq_base = 0x9740;
    constexpr uint64_t secure_eventq_base = 0x9840;
    constexpr uint64_t iova = 0x12000;
    constexpr uint64_t bad_iova = iova + apollo_smmu_tbu::PAGE_SIZE;
    constexpr uint64_t pa = 0x13000;
    constexpr uint32_t payload = 0x5ec51002;
    constexpr uint32_t stream_id = 0x62;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    std::memcpy(&m_memory_bytes[pa], &payload, sizeof(payload));
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                nonsecure_eventq_base | 2);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                  secure_eventq_base | 2);
    EXPECT_EQ(static_cast<uint32_t>(secure_eventq_base | 2),
              reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO)));
    EXPECT_EQ(0u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_PROD)));

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              stream_read32_security(stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE,
                                     bad_iova, observed));

    const uint64_t word0 = load_u64(secure_eventq_base);
    const auto& bank = m_tbu.arch_security_eventq_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE);

    EXPECT_EQ(0u, observed);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(nonsecure_eventq_base));
    EXPECT_EQ(1u, bank.queue.prod);
    EXPECT_EQ(1u, reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_PROD)));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, word0 & 0xffu);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
    EXPECT_TRUE(bank.queue_configured);
    EXPECT_TRUE(bank.routed_to_separate_queue);
    EXPECT_TRUE(bank.valid);
    EXPECT_EQ(1u, bank.records);
    EXPECT_EQ(word0, bank.last_record[0]);
    EXPECT_EQ(secure_eventq_base, bank.last_guest_record_addr);
    EXPECT_EQ(1u, bank.last_prod);
    EXPECT_EQ(0u, bank.last_cons);
    EXPECT_EQ(1u, m_tbu.arch_security_eventq_count(
                      apollo_smmu_tbu::ARCH_SECURITY_SECURE));
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureEventqMsiAndAbortUseSecureBank)
{
    constexpr uint64_t nonsecure_eventq_base = 0x9740;
    constexpr uint64_t secure_eventq_base = 0x9840;
    constexpr uint64_t nonsecure_event_msi_addr = 0x1000;
    constexpr uint64_t secure_event_msi_addr = 0x1004;
    constexpr uint64_t bad_secure_event_msi_addr = MEM_SIZE + 0x1000;
    constexpr uint64_t iova = 0x12000;
    constexpr uint64_t bad_iova = iova + apollo_smmu_tbu::PAGE_SIZE;
    constexpr uint64_t second_bad_iova = bad_iova + apollo_smmu_tbu::PAGE_SIZE;
    constexpr uint64_t pa = 0x13000;
    constexpr uint32_t payload = 0x5ec51003;
    constexpr uint32_t stream_id = 0x63;
    uint32_t observed = 0;

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    std::memcpy(&m_memory_bytes[pa], &payload, sizeof(payload));
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                nonsecure_eventq_base | 2);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI,
                  secure_eventq_base | 2);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0,
                apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0_HI,
                nonsecure_event_msi_addr);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG1),
                0xa5a50011u);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0,
                  apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0_HI,
                  secure_event_msi_addr);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG1),
                0xa5a50022u);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_EVENTQ |
                    apollo_smmu_tbu::ARCH_IRQ_GERROR);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              stream_read32_security(stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE,
                                     bad_iova, observed));

    const auto& bank = m_tbu.arch_security_eventq_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    EXPECT_EQ(0xa5a50022u, load_u32(secure_event_msi_addr));
    EXPECT_EQ(0u, load_u32(nonsecure_event_msi_addr));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_EVENTQ, m_tbu.m_arch_last_msi_source);
    EXPECT_EQ(1u, m_tbu.m_arch_msi_writes);
    EXPECT_EQ(0u, m_tbu.m_arch_msi_aborts);
    EXPECT_EQ(1u, bank.queue.prod);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_EVENTQ_ABORT);
    EXPECT_EQ(0u, reg_read_secure_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_EVENTQ_ABORT);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS), bank.queue.prod);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_EVENTQ);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_GERROR);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0,
                  apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG0_HI,
                  bad_secure_event_msi_addr);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_IRQ_CFG1),
                0xa5a50033u);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_EVENTQ |
                    apollo_smmu_tbu::ARCH_IRQ_GERROR);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE,
              stream_read32_security(stream_id, apollo_smmu_tbu::ARCH_SECURITY_SECURE,
                                     second_bad_iova, observed));

    EXPECT_EQ(1u, m_tbu.m_arch_msi_aborts);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_EVENTQ_ABORT);
    EXPECT_NE(0u, reg_read_secure_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_MSI_EVENTQ_ABORT);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_MSI_EVENTQ_ABORT);
    EXPECT_EQ(0u, reg_read_secure_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_EVENTQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecureGerrorMsiAndAbortUseSecureBank)
{
    constexpr uint64_t nonsecure_gerror_msi_addr = 0x1010;
    constexpr uint64_t secure_gerror_msi_addr = 0x1014;
    constexpr uint64_t bad_secure_gerror_msi_addr = MEM_SIZE + 0x1010;
    constexpr uint32_t secure_gerror_msi_data = 0xa5a50044u;
    constexpr uint32_t bad_secure_gerror_msi_data = 0xa5a50055u;

    reg_write64(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0,
                apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0_HI,
                nonsecure_gerror_msi_addr);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG1),
                0xa5a50033u);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0,
                  apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0_HI,
                  secure_gerror_msi_addr);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG1),
                secure_gerror_msi_data);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_GERROR);

    m_tbu.set_secure_gerror(apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);

    EXPECT_EQ(secure_gerror_msi_data, load_u32(secure_gerror_msi_addr));
    EXPECT_EQ(0u, load_u32(nonsecure_gerror_msi_addr));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_GERROR, m_tbu.m_arch_last_msi_source);
    EXPECT_EQ(1u, m_tbu.m_arch_msi_writes);
    EXPECT_EQ(0u, m_tbu.m_arch_msi_aborts);
    EXPECT_NE(0u, reg_read_secure_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, reg_read_secure_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL), 0);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0,
                  apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG0_HI,
                  bad_secure_gerror_msi_addr);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERROR_IRQ_CFG1),
                bad_secure_gerror_msi_data);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_GERROR);

    m_tbu.set_secure_gerror(apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT);

    EXPECT_EQ(1u, m_tbu.m_arch_msi_aborts);
    EXPECT_NE(0u, reg_read_secure_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT);
    EXPECT_NE(0u, reg_read_secure_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_MSI_GERROR_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     (apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT |
                      apollo_smmu_tbu::ARCH_GERROR_MSI_GERROR_ABORT));
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT |
                    apollo_smmu_tbu::ARCH_GERROR_MSI_GERROR_ABORT);
    EXPECT_EQ(0u, reg_read_secure_gerror_active() &
                     (apollo_smmu_tbu::ARCH_GERROR_EVENTQ_ABORT |
                      apollo_smmu_tbu::ARCH_GERROR_MSI_GERROR_ABORT));
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SecurePriqMsiAndAbortUseSecureBank)
{
    constexpr uint64_t nonsecure_priq_base = 0x9a00;
    constexpr uint64_t secure_priq_base = 0x9b00;
    constexpr uint64_t nonsecure_priq_msi_addr = 0x1020;
    constexpr uint64_t secure_priq_msi_addr = 0x1024;
    constexpr uint64_t bad_secure_priq_msi_addr = MEM_SIZE + 0x1020;
    constexpr uint32_t secure_priq_msi_data = 0xa5a50066u;
    constexpr uint32_t bad_secure_priq_msi_data = 0xa5a50077u;
    constexpr uint32_t stream_id = 0x63;
    constexpr uint64_t iova = 0x12000;
    constexpr uint64_t pa = 0x13000;

    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI,
                nonsecure_priq_base | 2);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_PRIQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_PRIQ_BASE_HI,
                  secure_priq_base | 2);
    reg_write64(apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG0,
                apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG0_HI,
                nonsecure_priq_msi_addr);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG1),
                0xa5a50055u);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG0,
                  apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG0_HI,
                  secure_priq_msi_addr);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG1),
                secure_priq_msi_data);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_PRIQ |
                    apollo_smmu_tbu::ARCH_IRQ_GERROR);

    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_SECURE;
    m_tbu.push_pri_record(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);

    EXPECT_EQ(secure_priq_msi_data, load_u32(secure_priq_msi_addr));
    EXPECT_EQ(0u, load_u32(nonsecure_priq_msi_addr));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_IRQ_PRIQ, m_tbu.m_arch_last_msi_source);
    EXPECT_EQ(1u, m_tbu.m_arch_msi_writes);
    EXPECT_EQ(0u, m_tbu.m_arch_msi_aborts);
    EXPECT_EQ(0u, m_tbu.m_priq.prod);
    EXPECT_EQ(1u, m_tbu.m_arch_secure_priq.prod);
    EXPECT_EQ((static_cast<uint64_t>(stream_id) << 32) | 0x2u,
              load_u64(secure_priq_base));
    EXPECT_EQ(0u, load_u64(nonsecure_priq_base));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_SECURE,
              m_tbu.m_arch_priq_irq_security_state);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_PRIQ_ABORT);
    EXPECT_EQ(0u, reg_read_secure_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_PRIQ_ABORT);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_PRIQ_CONS),
                m_tbu.m_arch_secure_priq.prod);
    EXPECT_EQ(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_PRIQ);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL), 0);
    reg_s_write64(apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG0,
                  apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG0_HI,
                  bad_secure_priq_msi_addr);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_PRIQ_IRQ_CFG1),
                bad_secure_priq_msi_data);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL),
                apollo_smmu_tbu::ARCH_IRQ_PRIQ |
                    apollo_smmu_tbu::ARCH_IRQ_GERROR);

    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_SECURE;
    m_tbu.push_pri_record(stream_id, iova + apollo_smmu_tbu::PAGE_SIZE,
                          pa + apollo_smmu_tbu::PAGE_SIZE,
                          apollo_smmu_tbu::PAGE_SIZE);

    EXPECT_EQ(1u, m_tbu.m_arch_msi_aborts);
    EXPECT_NE(0u, reg_read_secure_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_MSI_PRIQ_ABORT);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_PRIQ_ABORT);
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_GERROR);

    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_MSI_PRIQ_ABORT);
    EXPECT_EQ(0u, reg_read_secure_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_MSI_PRIQ_ABORT);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordCommonAccessAttributesAreEncoded)
{
    constexpr uint64_t eventq_base = 0x9800;
    constexpr uint64_t read_iova = 0x456000;
    constexpr uint64_t write_iova = read_iova + apollo_smmu_tbu::PAGE_SIZE;
    constexpr uint32_t stream_id = 0x12;
    constexpr uint32_t ssid = 0x25;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_PAGE_INVALID;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S1;

    const uint16_t read_stag =
        m_tbu.record_fault(stream_id, "privileged-execute", read_iova,
                           sizeof(uint32_t), true, true, ssid, true, true);
    ASSERT_NE(0u, read_stag);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, word0 & 0xffu);
    EXPECT_NE(0u, word0 & (1ULL << 11));
    EXPECT_EQ(ssid, (word0 >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
    EXPECT_EQ(read_stag, word1 & apollo_smmu_tbu::ARCH_EVENT_STAG_MASK);
    EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_PNU_SHIFT));
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_IND_SHIFT));
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_RNW_SHIFT));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_IN,
              (word1 >> apollo_smmu_tbu::ARCH_EVENT_CLASS_SHIFT) &
                  apollo_smmu_tbu::ARCH_EVENT_CLASS_MASK);
    EXPECT_EQ(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_S2_SHIFT));
    EXPECT_EQ(read_iova, load_u64(eventq_base + 16));

    const uint16_t write_stag =
        m_tbu.record_fault(stream_id, "write", write_iova, sizeof(uint32_t),
                           true, true, ssid, false, true);
    ASSERT_NE(0u, write_stag);

    const uint64_t write_word1 =
        load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES + 8);
    EXPECT_EQ(write_stag, write_word1 & apollo_smmu_tbu::ARCH_EVENT_STAG_MASK);
    EXPECT_EQ(0u, write_word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_IND_SHIFT));
    EXPECT_EQ(0u, write_word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_RNW_SHIFT));
}

TEST_BENCH(ApolloSmmuTbuTestBench, EndpointAccessAttributesPropagateToFaultEvent)
{
    constexpr uint64_t eventq_base = 0xa000;
    constexpr uint64_t iova = 0x456000;
    constexpr uint32_t stream_id = 0x42;
    constexpr uint32_t ssid = 0x37;
    uint32_t value = 0;
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    gs::ApolloSmmuStreamIdExtension stream_id_ext(stream_id, ssid, true, true, true);

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(iova);
    trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_byte_enable_length(0);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    trans.set_extension(&stream_id_ext);
    m_tbu.b_transport(trans, delay);
    trans.clear_extension(&stream_id_ext);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE, trans.get_response_status());

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, word0 & 0xffu);
    EXPECT_NE(0u, word0 & (1ULL << 11));
    EXPECT_EQ(ssid, (word0 >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
    EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_PNU_SHIFT));
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_IND_SHIFT));
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_RNW_SHIFT));
    EXPECT_EQ(iova, load_u64(eventq_base + 16));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordStage2IpaIsEncoded)
{
    constexpr uint64_t eventq_base = 0xa800;
    constexpr uint64_t iova = 0x456000;
    constexpr uint64_t ipa = 0xabcde000;
    constexpr uint32_t stream_id = 0x44;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_STAGE2;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S2;
    m_tbu.m_arch_last_ipa = ipa;

    const uint16_t stag =
        m_tbu.record_fault(stream_id, "read", iova, sizeof(uint32_t), true);
    ASSERT_NE(0u, stag);

    const uint64_t word1 = load_u64(eventq_base + 8);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, load_u64(eventq_base) & 0xffu);
    EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_S2_SHIFT));
    EXPECT_EQ(iova, load_u64(eventq_base + 16));
    EXPECT_EQ(ipa & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE2,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordNsipaBitIsEncoded)
{
    constexpr uint64_t eventq_base = 0xac00;
    constexpr uint64_t iova = 0x456400;
    constexpr uint64_t ipa = 0xabce0000;
    constexpr uint32_t stream_id = 0x4c;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_STAGE2;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S2;
    m_tbu.m_arch_fault_event_class = apollo_smmu_tbu::ARCH_EVENT_CLASS_IN;
    m_tbu.m_arch_fault_nsipa = true;
    m_tbu.m_arch_last_ipa = ipa;

    const uint16_t stag =
        m_tbu.record_fault(stream_id, "read", iova, sizeof(uint32_t), true);
    ASSERT_NE(0u, stag);

    const uint64_t word1 = load_u64(eventq_base + 8);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, load_u64(eventq_base) & 0xffu);
    EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_S2_SHIFT));
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_NSIPA_SHIFT));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_IN,
              (word1 >> apollo_smmu_tbu::ARCH_EVENT_CLASS_SHIFT) &
                  apollo_smmu_tbu::ARCH_EVENT_CLASS_MASK);
    EXPECT_EQ(iova, load_u64(eventq_base + 16));
    EXPECT_EQ(ipa & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordFetchAddressIsEncoded)
{
    constexpr uint64_t eventq_base = 0xb000;
    constexpr uint64_t ste_fetch_addr = 0x12345678;
    constexpr uint64_t cd_fetch_addr = 0x23456788;
    constexpr uint64_t iova = 0x456000;
    constexpr uint32_t stream_id = 0x45;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    m_tbu.set_arch_fetch_fault(apollo_smmu_tbu::ARCH_FAULT_STE_FETCH,
                               apollo_smmu_tbu::ARCH_FAULT_STAGE_S1,
                               ste_fetch_addr);
    m_tbu.record_fault(stream_id, "ste-fetch", iova, sizeof(uint32_t));

    m_tbu.set_arch_fetch_fault(apollo_smmu_tbu::ARCH_FAULT_CD_FETCH,
                               apollo_smmu_tbu::ARCH_FAULT_STAGE_S1,
                               cd_fetch_addr, true);
    m_tbu.record_fault(stream_id, "cd-fetch", iova, sizeof(uint32_t));

    const uint64_t ste_word0 = load_u64(eventq_base);
    const uint64_t ste_word1 = load_u64(eventq_base + 8);
    const uint64_t ste_word3 = load_u64(eventq_base + 24);
    const uint64_t cd_record = eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;
    const uint64_t cd_word0 = load_u64(cd_record);
    const uint64_t cd_word1 = load_u64(cd_record + 8);
    const uint64_t cd_word3 = load_u64(cd_record + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_STE_FETCH, ste_word0 & 0xffu);
    EXPECT_EQ(0u, ste_word1);
    EXPECT_EQ(ste_fetch_addr & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK,
              ste_word3 & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_CD_FETCH, cd_word0 & 0xffu);
    EXPECT_EQ(1ULL << apollo_smmu_tbu::ARCH_EVENT_GPCF_SHIFT, cd_word1);
    EXPECT_EQ(cd_fetch_addr & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK,
              cd_word3 & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordClassDistinguishesTableFaults)
{
    constexpr uint64_t eventq_base = 0xb800;
    constexpr uint64_t iova = 0x456000;
    constexpr uint32_t stream_id = 0x46;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_TABLE_INVALID;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S1;
    const uint16_t stag =
        m_tbu.record_fault(stream_id, "read", iova, sizeof(uint32_t), true);
    ASSERT_NE(0u, stag);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, word0 & 0xffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_TT,
              (word1 >> apollo_smmu_tbu::ARCH_EVENT_CLASS_SHIFT) &
                  apollo_smmu_tbu::ARCH_EVENT_CLASS_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordWalkEabtCarriesFetchAddress)
{
    constexpr uint64_t eventq_base = 0xc000;
    constexpr uint64_t iova = 0x456000;
    constexpr uint64_t fetch_addr = 0x34567890;
    constexpr uint32_t stream_id = 0x47;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_WALK_EABT;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S1;
    m_tbu.m_arch_last_fetch_addr = fetch_addr;
    const uint16_t stag =
        m_tbu.record_fault(stream_id, "read", iova, sizeof(uint32_t), true);
    ASSERT_NE(0u, stag);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);
    const uint64_t word2 = load_u64(eventq_base + 16);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_WALK_EABT, word0 & 0xffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_TT,
              (word1 >> apollo_smmu_tbu::ARCH_EVENT_CLASS_SHIFT) &
                  apollo_smmu_tbu::ARCH_EVENT_CLASS_MASK);
    EXPECT_EQ(iova, word2);
    EXPECT_EQ(fetch_addr & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordVmsFetchCarriesFetchAddress)
{
    constexpr uint64_t eventq_base = 0xc400;
    constexpr uint64_t input_addr = 0x456800;
    constexpr uint64_t fetch_addr = 0x56789000;
    constexpr uint32_t stream_id = 0x4a;
    constexpr uint32_t ssid = 0x12345;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    m_tbu.set_arch_fetch_fault(apollo_smmu_tbu::ARCH_FAULT_VMS_FETCH,
                               apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, fetch_addr);
    m_tbu.record_fault(stream_id, "vms-fetch", input_addr, sizeof(uint32_t),
                       false, true, ssid);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_VMS_FETCH, word0 & 0xffu);
    EXPECT_NE(0u, word0 & (1ULL << 11));
    EXPECT_EQ(ssid, (word0 >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(stream_id, static_cast<uint32_t>(word0 >> 32));
    EXPECT_EQ(fetch_addr & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_VMS_FETCH,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordFetchGpcfBitIsEncoded)
{
    constexpr uint64_t eventq_base = 0xc600;
    constexpr uint64_t input_addr = 0x456c00;
    constexpr uint64_t fetch_addr = 0x5678c000;
    constexpr uint32_t stream_id = 0x4b;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    m_tbu.set_arch_fetch_fault(apollo_smmu_tbu::ARCH_FAULT_WALK_EABT,
                               apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, fetch_addr,
                               true);
    const uint16_t stag =
        m_tbu.record_fault(stream_id, "read", input_addr, sizeof(uint32_t), true);
    ASSERT_NE(0u, stag);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_WALK_EABT, word0 & 0xffu);
    EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_GPCF_SHIFT));
    EXPECT_EQ(fetch_addr & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_FETCH_ADDR_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedEventRecordStage2CdFaultUsesClassCd)
{
    constexpr uint64_t eventq_base = 0xc800;
    constexpr uint64_t input_addr = 0x567000;
    constexpr uint64_t cd_ipa = 0x45678000;
    constexpr uint32_t stream_id = 0x48;
    constexpr uint32_t ssid = 0x123;

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);

    m_tbu.m_arch_fault_reason = apollo_smmu_tbu::ARCH_FAULT_STAGE2;
    m_tbu.m_arch_fault_stage = apollo_smmu_tbu::ARCH_FAULT_STAGE_S2;
    m_tbu.m_arch_fault_event_class = apollo_smmu_tbu::ARCH_EVENT_CLASS_CD;
    m_tbu.m_arch_last_ipa = cd_ipa;
    const uint16_t stag = m_tbu.record_fault(stream_id, "read", input_addr,
                                             sizeof(uint32_t), true, true, ssid);
    ASSERT_NE(0u, stag);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);
    const uint64_t word2 = load_u64(eventq_base + 16);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, word0 & 0xffu);
    EXPECT_NE(0u, word0 & (1ULL << 11));
    EXPECT_EQ(ssid, (word0 >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_S2_SHIFT));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_CD,
              (word1 >> apollo_smmu_tbu::ARCH_EVENT_CLASS_SHIFT) &
                  apollo_smmu_tbu::ARCH_EVENT_CLASS_MASK);
    EXPECT_EQ(input_addr, word2);
    EXPECT_EQ(cd_ipa & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, Stage2SteS2rS2sControlsRecordAndStall)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t eventq_base = 0xc900;
    constexpr uint64_t iova = 0x234000;
    constexpr uint32_t stream_id = 0x4e;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    store_u64(ste_pa, arch_s2_ste(0));
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN);

    store_u64(ste_pa + sizeof(uint64_t), 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE2,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2,
              m_tbu.m_arch_fault_stage);
    EXPECT_TRUE(m_tbu.m_arch_fault_record_suppressed);
    EXPECT_FALSE(m_tbu.m_arch_fault_stage2_stall);
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(0u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, m_tbu.m_arch_stall_pending);

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S2R);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_FALSE(m_tbu.m_arch_fault_record_suppressed);
    EXPECT_FALSE(m_tbu.m_arch_fault_stage2_stall);
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(0u, load_u64(eventq_base + 8) &
                      apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2,
              (reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL) >> 16) & 0xfu);
    EXPECT_EQ(0u, m_tbu.m_arch_stall_pending);

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    store_u64(ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S2S);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    const uint64_t stall_record = eventq_base +
                                  apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;
    EXPECT_FALSE(m_tbu.m_arch_fault_record_suppressed);
    EXPECT_TRUE(m_tbu.m_arch_fault_stage2_stall);
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(2u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION,
              load_u64(stall_record) & 0xffu);
    EXPECT_NE(0u, load_u64(stall_record + 8) &
                      apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, load_u64(stall_record + 8) &
                      apollo_smmu_tbu::ARCH_EVENT_STAG_MASK);
    EXPECT_NE(0u, load_u64(stall_record + 8) &
                      (1ULL << apollo_smmu_tbu::ARCH_EVENT_S2_SHIFT));
    EXPECT_EQ(iova, load_u64(stall_record + 16));
    EXPECT_EQ(1u, m_tbu.m_arch_stall_pending);
}

TEST_BENCH(ApolloSmmuTbuTestBench, SteS2sRejectedWhenStallModelTerminateOnly)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t eventq_base = 0xca00;
    constexpr uint64_t s1_cd_base = 0x30000;
    constexpr uint64_t s1_ttbr = 0x80000;
    constexpr uint64_t iova = 0x245000;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t s2_stream_id = 0x4f;
    constexpr uint32_t s1_stream_id = 0x50;
    const uint64_t s2_ste_pa =
        ste_base + s2_stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t s1_ste_pa =
        ste_base + s1_stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    m_tbu.m_arch_stall_model =
        apollo_smmu_tbu::ARCH_STALL_MODEL_TERMINATE_ONLY;

    store_u64(s2_ste_pa, arch_s2_ste(0));
    store_u64(s2_ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S2S);
    store_u64(s2_ste_pa + 2 * sizeof(uint64_t), 0);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, s2_stream_id);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0),
                apollo_smmu_tbu::ARCH_CR0_SMMUEN |
                    apollo_smmu_tbu::ARCH_CR0_EVENTQEN);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S1,
              m_tbu.m_arch_fault_stage);
    EXPECT_EQ(1u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_C_BAD_STE,
              load_u64(eventq_base) & 0xffu);
    EXPECT_EQ(s2_stream_id, static_cast<uint32_t>(load_u64(eventq_base) >> 32));
    EXPECT_EQ(0u, load_u64(eventq_base + 8) &
                      apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(0u, m_tbu.m_arch_stall_pending);

    reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL,
                apollo_smmu_tbu::FAULT_CTRL_CLEAR);
    stage_translation_tables(s1_ttbr, iova, output_pa);
    store_u64(s1_cd_base, arch_valid_cd());
    store_u64(s1_cd_base + sizeof(uint64_t),
              s1_ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(s1_ste_pa, arch_s1_ste(s1_cd_base));
    store_u64(s1_ste_pa + sizeof(uint64_t), apollo_smmu_tbu::ARCH_STE_S2S);
    store_u64(s1_ste_pa + 2 * sizeof(uint64_t), 0);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, s1_stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK,
              reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_NE(apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
              reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
    EXPECT_EQ(0u, m_tbu.m_fault_count);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ((output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (iova & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedNestedCdFetchStage2FaultRecordsClassCd)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t eventq_base = 0xd000;
    constexpr uint64_t input_addr = 0x3456;
    constexpr uint32_t stream_id = 0x49;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0 |
                  apollo_smmu_tbu::ARCH_STE_S2S);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, input_addr);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);
    const uint64_t word2 = load_u64(eventq_base + 16);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_fault_stage);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_CD, m_tbu.m_arch_fault_event_class);
    EXPECT_EQ(cd_ipa, m_tbu.m_arch_last_ipa);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, word0 & 0xffu);
    EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_S2_SHIFT));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_CD,
              (word1 >> apollo_smmu_tbu::ARCH_EVENT_CLASS_SHIFT) &
                  apollo_smmu_tbu::ARCH_EVENT_CLASS_MASK);
    EXPECT_EQ(input_addr, word2);
    EXPECT_EQ(cd_ipa & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedNestedL1CdFetchStage2Walks)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_l1_ipa = 0x24000;
    constexpr uint64_t cd_l1_pa = 0xe0000;
    constexpr uint64_t cd_l2_ipa = 0x34000;
    constexpr uint64_t cd_l2_pa = 0xf0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t input_addr = 0x3456;
    constexpr uint64_t ipa = 0x23000;
    constexpr uint64_t output_pa = 0xb0000;
    constexpr uint32_t stream_id = 0x4b;
    constexpr uint32_t ssid = apollo_smmu_tbu::ARCH_CD_L2_ENTRIES + 1;
    constexpr uint32_t s1cdmax = 11;
    constexpr uint16_t asid = 0x55;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint32_t l1_index = ssid / apollo_smmu_tbu::ARCH_CD_L2_ENTRIES;
    const uint32_t l2_index = ssid % apollo_smmu_tbu::ARCH_CD_L2_ENTRIES;
    const uint64_t cd_ipa = cd_l2_ipa + static_cast<uint64_t>(l2_index) *
                                             apollo_smmu_tbu::ARCH_CD_SIZE;
    const uint64_t cd_pa = cd_l2_pa + static_cast<uint64_t>(l2_index) *
                                         apollo_smmu_tbu::ARCH_CD_SIZE;
    const uint64_t cd = arch_valid_cd() |
                        (static_cast<uint64_t>(asid) << apollo_smmu_tbu::ARCH_CD_ASID_SHIFT);

    stage_translation_tables(s2ttb, cd_l1_ipa, cd_l1_pa);
    stage_translation_tables(s2ttb, cd_l2_ipa, cd_l2_pa);
    stage_translation_tables(s2ttb, s1ttb, s1ttb);
    stage_translation_tables(s2ttb, s1ttb + apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 2 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s2ttb, s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE,
                             s1ttb + 3 * apollo_smmu_tbu::PAGE_SIZE);
    stage_translation_tables(s1ttb, input_addr, ipa);
    stage_translation_tables(s2ttb, ipa, output_pa);
    store_u64(cd_l1_pa + l1_index * sizeof(uint64_t),
              apollo_smmu_tbu::ARCH_CD_L1_DESC_VALID | cd_l2_ipa);
    store_u64(cd_pa, cd);
    store_u64(cd_pa + sizeof(uint64_t), s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    store_u64(ste_pa, arch_nested_ste() |
                          (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_S1FMT_64K_L2)
                           << apollo_smmu_tbu::ARCH_STE_S1FMT_SHIFT) |
                          (static_cast<uint64_t>(s1cdmax)
                           << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT));
    store_u64(ste_pa + sizeof(uint64_t),
              cd_l1_ipa | apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0 |
                  apollo_smmu_tbu::ARCH_STE_S2S);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, input_addr);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, (1u << 31) | ssid);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_PROBE);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_OK, reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
    EXPECT_EQ(cd, m_tbu.m_arch_last_cd);
    EXPECT_EQ(cd_pa, m_tbu.m_arch_last_cd_pa);
    EXPECT_EQ(ssid, m_tbu.m_arch_last_cd_ssid);
    EXPECT_TRUE(m_tbu.m_arch_last_cd_l2);
    EXPECT_EQ((ipa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (input_addr & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_ipa);
    EXPECT_EQ((output_pa & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  (m_tbu.m_arch_last_ipa & (apollo_smmu_tbu::PAGE_SIZE - 1)),
              m_tbu.m_arch_last_pa);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(input_addr),
                                 asid, 0, true, ssid));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedNestedL1CdFetchStage2FaultRecordsClassCd)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_l1_ipa = 0x24000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t eventq_base = 0xe000;
    constexpr uint64_t input_addr = 0x3456;
    constexpr uint32_t stream_id = 0x4c;
    constexpr uint32_t ssid = apollo_smmu_tbu::ARCH_CD_L2_ENTRIES + 1;
    constexpr uint32_t s1cdmax = 11;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint32_t l1_index = ssid / apollo_smmu_tbu::ARCH_CD_L2_ENTRIES;
    const uint64_t l1_fetch_ipa = cd_l1_ipa + l1_index * sizeof(uint64_t);

    store_u64(ste_pa, arch_nested_ste() |
                          (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_STE_S1FMT_64K_L2)
                           << apollo_smmu_tbu::ARCH_STE_S1FMT_SHIFT) |
                          (static_cast<uint64_t>(s1cdmax)
                           << apollo_smmu_tbu::ARCH_STE_S1CDMAX_SHIFT));
    store_u64(ste_pa + sizeof(uint64_t),
              cd_l1_ipa | apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0 |
                  apollo_smmu_tbu::ARCH_STE_S2S);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, input_addr);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_SSID, (1u << 31) | ssid);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);
    const uint64_t word2 = load_u64(eventq_base + 16);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_fault_stage);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_CD, m_tbu.m_arch_fault_event_class);
    EXPECT_EQ(l1_fetch_ipa, m_tbu.m_arch_last_ipa);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, word0 & 0xffu);
    EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_S2_SHIFT));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_CD,
              (word1 >> apollo_smmu_tbu::ARCH_EVENT_CLASS_SHIFT) &
                  apollo_smmu_tbu::ARCH_EVENT_CLASS_MASK);
    EXPECT_EQ(input_addr, word2);
    EXPECT_EQ(l1_fetch_ipa & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedNestedTtFetchStage2FaultRecordsClassTt)
{
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_ipa = 0x24000;
    constexpr uint64_t cd_pa = 0xe0000;
    constexpr uint64_t s1ttb = 0x80000;
    constexpr uint64_t s2ttb = 0xa0000;
    constexpr uint64_t eventq_base = 0xd800;
    constexpr uint64_t input_addr = 0x3456;
    constexpr uint32_t stream_id = 0x4a;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t first_s1_desc_ipa =
        s1ttb + apollo_smmu_tbu::arch_level_index(input_addr, 0) * sizeof(uint64_t);

    store_u64(ste_pa, arch_nested_ste());
    store_u64(ste_pa + sizeof(uint64_t),
              cd_ipa | apollo_smmu_tbu::ARCH_STE_S1DSS_SSID0 |
                  apollo_smmu_tbu::ARCH_STE_S2S);
    store_u64(ste_pa + 2 * sizeof(uint64_t),
              s2ttb & apollo_smmu_tbu::ARCH_STE_S2TTB_MASK);
    stage_translation_tables(s2ttb, cd_ipa, cd_pa);
    store_u64(cd_pa, arch_valid_cd());
    store_u64(cd_pa + sizeof(uint64_t), s1ttb & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);

    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                apollo_smmu_tbu::REG_ARCH_IOVA_HI, input_addr);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 4);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);
    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL,
                apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    const uint64_t word0 = load_u64(eventq_base);
    const uint64_t word1 = load_u64(eventq_base + 8);
    const uint64_t word2 = load_u64(eventq_base + 16);
    const uint64_t word3 = load_u64(eventq_base + 24);

    EXPECT_EQ(apollo_smmu_tbu::ARCH_FAULT_STAGE_S2, m_tbu.m_arch_fault_stage);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_TT, m_tbu.m_arch_fault_event_class);
    EXPECT_EQ(first_s1_desc_ipa, m_tbu.m_arch_last_ipa);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, word0 & 0xffu);
    EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_NE(0u, word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_S2_SHIFT));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_CLASS_TT,
              (word1 >> apollo_smmu_tbu::ARCH_EVENT_CLASS_SHIFT) &
                  apollo_smmu_tbu::ARCH_EVENT_CLASS_MASK);
    EXPECT_EQ(input_addr, word2);
    EXPECT_EQ(first_s1_desc_ipa & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK,
              word3 & apollo_smmu_tbu::ARCH_EVENT_IPA_MASK);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdStallTermTerminatesPendingStalls)
{
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint32_t stream_id = 3;

    m_tbu.m_arch_stalls[0] = {stream_id, 0x10, 0x1000, 0, false, true};
    m_tbu.m_arch_stalls[1] = {stream_id, 0x11, 0x2000, 0, false, true};
    m_tbu.m_arch_stalls[2] = {stream_id + 1, 0x12, 0x3000, 0, false, true};
    m_tbu.m_arch_stall_pending = 3;

    store_u64(cmdq_base, (static_cast<uint64_t>(stream_id) << 32) |
                             apollo_smmu_tbu::ARCH_CMD_STALL_TERM);
    store_u64(cmdq_base + sizeof(uint64_t), 0);

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(1u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(2u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 16) & 0xffu);
    EXPECT_EQ(2u, reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REPLAY));
    EXPECT_TRUE(m_tbu.m_arch_stalls[2].pending);
}

TEST_BENCH(ApolloSmmuTbuTestBench, NegativeFaultReplayMatrixRecordsArchitectedEvents)
{
    constexpr uint64_t strtab_base = 0x18000;
    constexpr uint64_t ste_base = 0x20000;
    constexpr uint64_t cd_base = 0x24000;
    constexpr uint64_t ttbr = 0x40000;
    constexpr uint64_t output_base = 0x90000;
    constexpr uint64_t eventq_base = 0x100000;
    constexpr uint64_t iova = 0x1234;
    constexpr uint32_t stream_id = 1;
    constexpr uint32_t bad_stream_id = 4;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;
    const uint64_t valid_ste = arch_s1_ste(cd_base);

    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 5);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);

    const auto reset_case = [this](uint32_t sid, uint64_t case_iova) {
        reg_write32(apollo_smmu_tbu::REG_FAULT_CTRL, apollo_smmu_tbu::FAULT_CTRL_CLEAR);
        reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                    apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, 0);
        reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), 0);
        tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO,
                    apollo_smmu_tbu::REG_ARCH_IOVA_HI, case_iova);
        reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, sid);
    };
    const auto expect_fault_event =
        [this, eventq_base](uint32_t sid, uint64_t case_iova, uint32_t ctrl,
                            uint32_t reason, uint32_t event, uint32_t fault_class,
                            uint32_t stage, bool write) {
            const uint32_t prod_before = m_tbu.m_eventq.prod;
            const uint32_t entries = apollo_smmu_tbu::queue_entries(m_tbu.m_eventq);
            const uint64_t record_base =
                eventq_base +
                apollo_smmu_tbu::queue_index(prod_before, entries) *
                    apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES;

            reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, ctrl);

            EXPECT_EQ(apollo_smmu_tbu::ARCH_STATUS_ERROR,
                      reg_read32(apollo_smmu_tbu::REG_ARCH_STATUS));
            EXPECT_EQ(reason, reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_REASON));
            EXPECT_EQ(stage, m_tbu.m_arch_fault_stage);
            EXPECT_EQ(event, load_u64(record_base) & 0xffu);
            EXPECT_EQ(sid, static_cast<uint32_t>(load_u64(record_base) >> 32));

            const uint64_t word1 = load_u64(record_base + 8);
            const uint32_t detail = reg_read32(apollo_smmu_tbu::REG_ARCH_FAULT_DETAIL);
            EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
            EXPECT_NE(0u, word1 & apollo_smmu_tbu::ARCH_EVENT_STAG_MASK);
            EXPECT_EQ(write ? 0u : (1ULL << apollo_smmu_tbu::ARCH_EVENT_RNW_SHIFT),
                      word1 & (1ULL << apollo_smmu_tbu::ARCH_EVENT_RNW_SHIFT));
            EXPECT_EQ(case_iova, load_u64(record_base + 16));
            EXPECT_EQ(reason, detail & 0xffu);
            EXPECT_EQ(fault_class, (detail >> 8) & 0xffu);
            EXPECT_EQ(stage, (detail >> 16) & 0xfu);
            EXPECT_NE(0u, detail & (apollo_smmu_tbu::ARCH_FAULT_ATTR_STALL << 20));
            EXPECT_EQ(write ? (apollo_smmu_tbu::ARCH_FAULT_ATTR_WRITE << 20) : 0u,
                      detail & (apollo_smmu_tbu::ARCH_FAULT_ATTR_WRITE << 20));
        };

    reset_case(bad_stream_id, iova);
    reg_write64(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_LO,
                apollo_smmu_tbu::SMMUV3_STRTAB_BASE_HI, strtab_base);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_STRTAB_BASE_CFG), 1);
    expect_fault_event(bad_stream_id, iova,
                       apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY,
                       apollo_smmu_tbu::ARCH_FAULT_BAD_STREAM_ID,
                       apollo_smmu_tbu::ARCH_EVENT_C_BAD_STREAMID,
                       apollo_smmu_tbu::ARCH_FAULT_CLASS_STE,
                       apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, false);

    reset_case(stream_id, iova + apollo_smmu_tbu::PAGE_SIZE);
    store_u64(ste_pa, 0);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    expect_fault_event(stream_id, iova + apollo_smmu_tbu::PAGE_SIZE,
                       apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY,
                       apollo_smmu_tbu::ARCH_FAULT_STE_INVALID,
                       apollo_smmu_tbu::ARCH_EVENT_C_BAD_STE,
                       apollo_smmu_tbu::ARCH_FAULT_CLASS_STE,
                       apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, false);

    reset_case(stream_id, iova + 2 * apollo_smmu_tbu::PAGE_SIZE);
    store_u64(ste_pa, valid_ste);
    store_u64(ste_pa + sizeof(uint64_t), 0);
    store_u64(ste_pa + 2 * sizeof(uint64_t), 0);
    store_u64(cd_base, 0);
    store_u64(cd_base + sizeof(uint64_t), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    expect_fault_event(stream_id, iova + 2 * apollo_smmu_tbu::PAGE_SIZE,
                       apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY,
                       apollo_smmu_tbu::ARCH_FAULT_CD_INVALID,
                       apollo_smmu_tbu::ARCH_EVENT_C_BAD_CD,
                       apollo_smmu_tbu::ARCH_FAULT_CLASS_CD,
                       apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, false);

    const uint64_t read_fault_iova = iova + 3 * apollo_smmu_tbu::PAGE_SIZE;
    reset_case(stream_id, read_fault_iova);
    store_u64(ste_pa, valid_ste);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    stage_translation_tables(ttbr, read_fault_iova, output_base);
    store_u64(ttbr + 3 * apollo_smmu_tbu::PAGE_SIZE +
                  apollo_smmu_tbu::arch_level_index(read_fault_iova, 3) *
                      sizeof(uint64_t),
              (output_base & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  apollo_smmu_tbu::ARCH_DESC_PAGE);
    expect_fault_event(stream_id, read_fault_iova,
                       apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY,
                       apollo_smmu_tbu::ARCH_FAULT_ACCESS,
                       apollo_smmu_tbu::ARCH_EVENT_F_ACCESS,
                       apollo_smmu_tbu::ARCH_FAULT_CLASS_TRANSLATION,
                       apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, false);

    const uint64_t write_fault_iova = iova + 4 * apollo_smmu_tbu::PAGE_SIZE;
    reset_case(stream_id, write_fault_iova);
    store_u64(ste_pa, valid_ste);
    store_u64(cd_base, arch_valid_cd());
    store_u64(cd_base + sizeof(uint64_t), ttbr & apollo_smmu_tbu::ARCH_CD_TTB0_MASK);
    stage_translation_tables(ttbr, write_fault_iova, output_base);
    store_u64(ttbr + 3 * apollo_smmu_tbu::PAGE_SIZE +
                  apollo_smmu_tbu::arch_level_index(write_fault_iova, 3) *
                      sizeof(uint64_t),
              (output_base & apollo_smmu_tbu::ARCH_DESC_OUTPUT_MASK) |
                  apollo_smmu_tbu::ARCH_DESC_AF |
                  apollo_smmu_tbu::ARCH_DESC_AP_RO |
                  apollo_smmu_tbu::ARCH_DESC_PAGE);
    expect_fault_event(stream_id, write_fault_iova,
                       apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY_WRITE,
                       apollo_smmu_tbu::ARCH_FAULT_PERMISSION,
                       apollo_smmu_tbu::ARCH_EVENT_F_PERMISSION,
                       apollo_smmu_tbu::ARCH_FAULT_CLASS_TRANSLATION,
                       apollo_smmu_tbu::ARCH_FAULT_STAGE_S1, true);
}

TEST_BENCH(ApolloSmmuTbuTestBench, EndpointTransactionReplayRetriesAfterCmdResume)
{
    constexpr uint64_t iova = 0x4000;
    constexpr uint64_t pa = 0x8000;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint32_t stream_id = 0x9;
    uint32_t value = 0;

    m_tbu.m_dynamic_enabled = true;
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 3);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE, stream_read32(stream_id, iova, value));
    const uint16_t stag = m_tbu.m_arch_last_stag;
    ASSERT_NE(0u, stag);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, load_u64(eventq_base) & 0xffu);
    EXPECT_NE(0u, load_u64(eventq_base + 8) & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(iova, load_u64(eventq_base + 16));
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS) & 0xffu);

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    EXPECT_FALSE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova)));

    store_u64(cmdq_base, (static_cast<uint64_t>(stream_id) << 32) |
                             (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                              << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                             apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + sizeof(uint64_t), stag);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t replay_status =
        reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS);
    EXPECT_EQ(0u, replay_status & 0xffu);
    EXPECT_EQ(1u, (replay_status >> 8) & 0xffu);
    EXPECT_EQ(1u, (replay_status >> 16) & 0xffu);
    EXPECT_EQ(0u, (replay_status >> 24) & 0xffu);
    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 8) & 0xffu);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, EndpointTransactionReplayRedrivesWritePayload)
{
    constexpr uint64_t iova = 0x5000;
    constexpr uint64_t pa = 0x8800;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint32_t stream_id = 0xa;
    constexpr uint32_t payload = 0x5aa55aa5;

    m_tbu.m_dynamic_enabled = true;
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 3);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE, stream_write32(stream_id, iova, payload));
    const uint16_t stag = m_tbu.m_arch_last_stag;
    ASSERT_NE(0u, stag);
    EXPECT_EQ(0u, load_u32(pa));
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_F_TRANSLATION, load_u64(eventq_base) & 0xffu);
    EXPECT_NE(0u, load_u64(eventq_base + 8) & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(iova, load_u64(eventq_base + 16));
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS) & 0xffu);

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(cmdq_base, (static_cast<uint64_t>(stream_id) << 32) |
                             (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                              << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                             apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + sizeof(uint64_t), stag);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t replay_status =
        reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS);
    EXPECT_EQ(0u, replay_status & 0xffu);
    EXPECT_EQ(1u, (replay_status >> 8) & 0xffu);
    EXPECT_EQ(1u, (replay_status >> 16) & 0xffu);
    EXPECT_EQ(0u, (replay_status >> 24) & 0xffu);
    EXPECT_EQ(1u, m_tbu.m_arch_endpoint_replay_redriven);
    EXPECT_EQ(0u, m_tbu.m_arch_endpoint_replay_failed);
    EXPECT_EQ(payload, load_u32(pa));
}

TEST_BENCH(ApolloSmmuTbuTestBench, EndpointTransactionReplayBlocksCallerUntilCmdResume)
{
    constexpr uint64_t iova = 0x6000;
    constexpr uint64_t pa = 0x9800;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint32_t stream_id = 0xb;
    constexpr uint32_t payload = 0xc001d00d;

    m_tbu.m_dynamic_enabled = true;
    reg_write32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_CTRL,
                apollo_smmu_tbu::ARCH_ENDPOINT_REPLAY_BLOCKING_ENABLE);
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 3);

    m_async_stream_id = stream_id;
    m_async_iova = iova;
    m_async_payload = payload;
    m_async_status = tlm::TLM_INCOMPLETE_RESPONSE;
    m_async_done = false;
    sc_core::sc_spawn(sc_core::sc_bind(
        &ApolloSmmuTbuTestBench::endpoint_blocking_write_body, this));
    sc_core::wait(sc_core::SC_ZERO_TIME);

    const uint16_t stag = m_tbu.m_arch_last_stag;
    ASSERT_NE(0u, stag);
    EXPECT_FALSE(m_async_done);
    EXPECT_EQ(tlm::TLM_INCOMPLETE_RESPONSE, m_async_status);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS) & 0xffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_BLOCK_STATUS) & 0xffu);
    EXPECT_EQ(0u, load_u32(pa));

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    store_u64(cmdq_base, (static_cast<uint64_t>(stream_id) << 32) |
                             (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                              << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                             apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + sizeof(uint64_t), stag);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);

    EXPECT_TRUE(m_async_done);
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, m_async_status);
    EXPECT_EQ(payload, load_u32(pa));
    const uint32_t block_status =
        reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_BLOCK_STATUS);
    EXPECT_EQ(1u, block_status & 0xffu);
    EXPECT_EQ(1u, (block_status >> 8) & 0xffu);
    EXPECT_EQ(0u, (block_status >> 16) & 0xffu);
    EXPECT_EQ(1u, m_tbu.m_arch_endpoint_replay_redriven);
}

TEST_BENCH(ApolloSmmuTbuTestBench, EndpointEarlyRetryDoesNotDuplicateFaultAndRequiresResume)
{
    constexpr uint64_t iova = 0x7000;
    constexpr uint64_t pa = 0xa800;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint32_t stream_id = 0xc;
    constexpr uint32_t payload = 0x1ee7c0de;

    m_tbu.m_dynamic_enabled = true;
    reg_write64(apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_EVENTQ_BASE_HI, eventq_base | 3);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 3);

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE, stream_write32(stream_id, iova, payload));
    const uint16_t stag = m_tbu.m_arch_last_stag;
    ASSERT_NE(0u, stag);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS) & 0xffu);

    reg_write32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_CTRL,
                apollo_smmu_tbu::ARCH_ENDPOINT_REPLAY_EARLY_RETRY);

    uint32_t early_status = reg_read32(apollo_smmu_tbu::REG_ARCH_EARLY_RETRY_STATUS);
    EXPECT_EQ(1u, early_status & 0xffu);
    EXPECT_EQ(0u, (early_status >> 8) & 0xffu);
    EXPECT_EQ(1u, (early_status >> 16) & 0xffu);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(0u, load_u32(pa));

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    reg_write32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_CTRL,
                apollo_smmu_tbu::ARCH_ENDPOINT_REPLAY_EARLY_RETRY);

    early_status = reg_read32(apollo_smmu_tbu::REG_ARCH_EARLY_RETRY_STATUS);
    EXPECT_EQ(2u, early_status & 0xffu);
    EXPECT_EQ(1u, (early_status >> 8) & 0xffu);
    EXPECT_EQ(1u, (early_status >> 16) & 0xffu);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS) & 0xffu);
    EXPECT_EQ(payload, load_u32(pa));

    store_u64(cmdq_base, (static_cast<uint64_t>(stream_id) << 32) |
                             (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                              << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                             apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + sizeof(uint64_t), stag);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    const uint32_t replay_status =
        reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS);
    EXPECT_EQ(0u, replay_status & 0xffu);
    EXPECT_EQ(1u, (replay_status >> 8) & 0xffu);
    EXPECT_EQ(1u, (replay_status >> 16) & 0xffu);
    EXPECT_EQ(0u, (replay_status >> 24) & 0xffu);
    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 8) & 0xffu);
    EXPECT_EQ(1u, m_tbu.m_arch_endpoint_replay_redriven);
}

TEST_BENCH(ApolloSmmuTbuTestBench, EndpointEarlyRetryDiscardsUncommittedStaleEvent)
{
    constexpr uint64_t iova = 0x8000;
    constexpr uint64_t pa = 0xb800;
    constexpr uint64_t eventq_base = 0x9000;
    constexpr uint32_t stream_id = 0xd;
    constexpr uint32_t payload = 0x5a1e0001;

    m_tbu.m_dynamic_enabled = true;
    m_tbu.m_eventq.base = eventq_base | 1;
    m_tbu.m_eventq.prod = 1;
    m_tbu.m_eventq.cons = 0;

    EXPECT_EQ(tlm::TLM_ADDRESS_ERROR_RESPONSE, stream_write32(stream_id, iova, payload));
    const uint16_t stag = m_tbu.m_arch_last_stag;
    ASSERT_NE(0u, stag);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(1u, m_tbu.m_arch_stall_buffered);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS) & 0xffu);

    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);
    reg_write32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_CTRL,
                apollo_smmu_tbu::ARCH_ENDPOINT_REPLAY_EARLY_RETRY);

    const uint32_t early_status =
        reg_read32(apollo_smmu_tbu::REG_ARCH_EARLY_RETRY_STATUS);
    EXPECT_EQ(1u, early_status & 0xffu);
    EXPECT_EQ(1u, (early_status >> 8) & 0xffu);
    EXPECT_EQ(0u, (early_status >> 16) & 0xffu);
    EXPECT_EQ(1u, early_status >> 24);
    EXPECT_EQ(payload, load_u32(pa));
    EXPECT_EQ(0u, m_tbu.m_arch_stall_buffered);
    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_ENDPOINT_REPLAY_STATUS) & 0xffu);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS), 1);

    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES));
}

TEST_BENCH(ApolloSmmuTbuTestBench, FaultReplayFullEventQueueBuffersAndRedrivesStall)
{
    constexpr uint64_t ste_base = 0x2000;
    constexpr uint64_t eventq_base = 0x7000;
    constexpr uint64_t cmdq_base = 0x9000;
    constexpr uint64_t iova = 0x1000;
    constexpr uint32_t stream_id = 1;
    const uint64_t ste_pa = ste_base + stream_id * apollo_smmu_tbu::ARCH_STE_SIZE;

    store_u64(ste_pa, 0);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_STE_BASE_LO,
                apollo_smmu_tbu::REG_ARCH_STE_BASE_HI, ste_base);
    tbu_write64(apollo_smmu_tbu::REG_ARCH_IOVA_LO, apollo_smmu_tbu::REG_ARCH_IOVA_HI, iova);
    reg_write32(apollo_smmu_tbu::REG_ARCH_STREAM_ID, stream_id);

    m_tbu.m_eventq.base = eventq_base | 1;
    m_tbu.m_eventq.prod = 1;
    m_tbu.m_eventq.cons = 0;
    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_REALM;

    reg_write32(apollo_smmu_tbu::REG_ARCH_CTRL, apollo_smmu_tbu::ARCH_CTRL_NEGATIVE_REPLAY);

    EXPECT_FALSE(m_tbu.m_eventq.overflow);
    EXPECT_EQ(0u, reg_read_gerror_active() &
                     apollo_smmu_tbu::ARCH_GERROR_QUEUE_OVERFLOW);
    EXPECT_EQ(1u, m_tbu.m_eventq.prod);
    EXPECT_EQ(1u, m_tbu.m_arch_stall_buffered);
    EXPECT_EQ(0u, m_tbu.m_arch_stall_redriven);
    EXPECT_EQ(0u, m_tbu.arch_security_eventq_count(
                      apollo_smmu_tbu::ARCH_SECURITY_REALM));
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);

    m_tbu.m_arch_last_security_state = apollo_smmu_tbu::ARCH_SECURITY_NONSECURE;
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS), 1);

    EXPECT_EQ(2u, m_tbu.m_eventq.prod);
    EXPECT_EQ(0u, m_tbu.m_arch_stall_buffered);
    EXPECT_EQ(1u, m_tbu.m_arch_stall_redriven);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_REALM,
              m_tbu.m_arch_last_event_security_state);
    EXPECT_EQ(1u, m_tbu.arch_security_eventq_count(
                      apollo_smmu_tbu::ARCH_SECURITY_REALM));
    EXPECT_EQ(0u, m_tbu.arch_security_eventq_count(
                      apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    const auto& realm_bank = m_tbu.arch_security_eventq_bank_state(
        apollo_smmu_tbu::ARCH_SECURITY_REALM);
    EXPECT_TRUE(realm_bank.valid);
    EXPECT_EQ(1u, realm_bank.records);
    EXPECT_EQ(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES,
              realm_bank.last_guest_record_addr);
    EXPECT_EQ(2u, realm_bank.last_prod);
    EXPECT_EQ((static_cast<uint64_t>(stream_id) << 32) |
                  apollo_smmu_tbu::ARCH_EVENT_C_BAD_STE,
              load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES));
    const uint64_t event_word1 =
        load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES + 8);
    const uint16_t stag =
        static_cast<uint16_t>(event_word1 & apollo_smmu_tbu::ARCH_EVENT_STAG_MASK);
    EXPECT_NE(0u, stag);
    EXPECT_NE(0u, event_word1 & apollo_smmu_tbu::ARCH_EVENT_STALL);
    EXPECT_EQ(iova, load_u64(eventq_base + apollo_smmu_tbu::ARCH_EVENTQ_ENTRY_BYTES + 16));
    EXPECT_NE(0u, m_tbu.m_arch_irq_status & apollo_smmu_tbu::ARCH_IRQ_EVENTQ);

    store_u64(cmdq_base, (static_cast<uint64_t>(stream_id) << 32) |
                             (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_RESUME_RETRY)
                              << apollo_smmu_tbu::ARCH_CMD_RESUME_RESP_SHIFT) |
                             apollo_smmu_tbu::ARCH_CMD_RESUME);
    store_u64(cmdq_base + sizeof(uint64_t), stag);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(0u, reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) & 0xffu);
    EXPECT_EQ(1u, (reg_read32(apollo_smmu_tbu::REG_ARCH_STALL_STATUS) >> 8) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, MultiStreamIdMapsAreIsolated)
{
    constexpr uint64_t iova = 0x1000;
    constexpr uint64_t sid1_pa = 0x2000;
    constexpr uint64_t sid2_pa = 0x3000;
    constexpr uint32_t sid1_value = 0x11112222;
    constexpr uint32_t sid2_value = 0x33334444;
    uint32_t value = 0;

    std::memcpy(&m_memory_bytes[sid1_pa], &sid1_value, sizeof(sid1_value));
    std::memcpy(&m_memory_bytes[sid2_pa], &sid2_value, sizeof(sid2_value));

    add_map(0x1, iova, sid1_pa, 0x1000);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_MAP_STREAM_COUNT));
    add_map(0x2, iova, sid2_pa, 0x1000);
    EXPECT_EQ(2u, reg_read32(apollo_smmu_tbu::REG_MAP_COUNT));
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_MAP_STREAM_COUNT));

    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x1, iova, value));
    EXPECT_EQ(sid1_value, value);
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x2, iova, value));
    EXPECT_EQ(sid2_value, value);
    EXPECT_EQ(0u, stream_dbg_read32(0x3, iova, value));

    EXPECT_TRUE(m_tbu.ats_lookup(0x1, apollo_smmu_tbu::page_base(iova)));
    EXPECT_TRUE(m_tbu.ats_lookup(0x2, apollo_smmu_tbu::page_base(iova)));

    remove_map(0x1, iova);
    EXPECT_EQ(1u, reg_read32(apollo_smmu_tbu::REG_MAP_COUNT));
    EXPECT_FALSE(m_tbu.ats_lookup(0x1, apollo_smmu_tbu::page_base(iova)));
    EXPECT_TRUE(m_tbu.ats_lookup(0x2, apollo_smmu_tbu::page_base(iova)));
    EXPECT_EQ(0u, stream_dbg_read32(0x1, iova, value));
    EXPECT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x2, iova, value));
    EXPECT_EQ(sid2_value, value);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqInvalidationCommandsClearAtsBySidPageAndGlobal)
{
    constexpr uint64_t iova = 0x1000;
    constexpr uint64_t sid1_pa = 0x2000;
    constexpr uint64_t sid2_pa = 0x3000;
    constexpr uint64_t cmdq_base = 0x9000;
    constexpr uint32_t sid1_value = 0x11112222;
    constexpr uint32_t sid2_value = 0x33334444;
    uint32_t value = 0;

    std::memcpy(&m_memory_bytes[sid1_pa], &sid1_value, sizeof(sid1_value));
    std::memcpy(&m_memory_bytes[sid2_pa], &sid2_value, sizeof(sid2_value));

    add_map(0x1, iova, sid1_pa, 0x1000);
    add_map(0x2, iova, sid2_pa, 0x1000);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 3);

    ASSERT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x1, iova, value));
    ASSERT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x2, iova, value));
    ASSERT_TRUE(m_tbu.ats_lookup(0x1, apollo_smmu_tbu::page_base(iova)));
    ASSERT_TRUE(m_tbu.ats_lookup(0x2, apollo_smmu_tbu::page_base(iova)));

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_CFGI_STE | (1ULL << 32));
    store_u64(cmdq_base + sizeof(uint64_t), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_FALSE(m_tbu.ats_lookup(0x1, apollo_smmu_tbu::page_base(iova)));
    EXPECT_TRUE(m_tbu.ats_lookup(0x2, apollo_smmu_tbu::page_base(iova)));
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_cfgis);
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(1u, (reg_read32(apollo_smmu_tbu::REG_ARCH_CMD_DETAIL) >> 8) & 0xffffu);

    ASSERT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x1, iova, value));
    ASSERT_TRUE(m_tbu.ats_lookup(0x1, apollo_smmu_tbu::page_base(iova)));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VA);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), iova);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_FALSE(m_tbu.ats_lookup(0x1, apollo_smmu_tbu::page_base(iova)));
    EXPECT_FALSE(m_tbu.ats_lookup(0x2, apollo_smmu_tbu::page_base(iova)));
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_tlbis);
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_invalidated);

    ASSERT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x1, iova, value));
    ASSERT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x2, iova, value));
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_ATC_INV | (2ULL << 32));
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t),
              iova);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 3);

    EXPECT_TRUE(m_tbu.ats_lookup(0x1, apollo_smmu_tbu::page_base(iova)));
    EXPECT_FALSE(m_tbu.ats_lookup(0x2, apollo_smmu_tbu::page_base(iova)));
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_atc_invs);
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);

    ASSERT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(0x2, iova, value));
    store_u64(cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_ATC_INV | (1ULL << 9));
    store_u64(cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);

    EXPECT_FALSE(m_tbu.ats_lookup(0x1, apollo_smmu_tbu::page_base(iova)));
    EXPECT_FALSE(m_tbu.ats_lookup(0x2, apollo_smmu_tbu::page_base(iova)));
    EXPECT_EQ(2u, m_tbu.m_arch_cmd_atc_invs);
    EXPECT_EQ(6u, m_tbu.m_arch_cmd_invalidations);
    EXPECT_EQ(4u, reg_read32(apollo_smmu_tbu::REG_ARCH_CMD_STATUS) & 0xffu);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_ATC_INV,
              (reg_read32(apollo_smmu_tbu::REG_ARCH_CMD_STATUS) >> 24) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqCfgiVmsPidmInvalidatesModeledVmsState)
{
    constexpr uint64_t cmdq_base = 0x9800;
    constexpr uint64_t vms_ptr = 0x12000;
    constexpr uint16_t vmid = 0x22;
    constexpr uint16_t other_vmid = 0x23;

    m_tbu.m_arch_last_ste5 = apollo_smmu_tbu::ARCH_STE_S1MPAM |
                             (vms_ptr & apollo_smmu_tbu::ARCH_STE_VMSPTR_MASK);
    m_tbu.m_arch_last_vms_ptr = vms_ptr;
    m_tbu.m_arch_last_vmid = vmid;

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);

    store_u64(cmdq_base,
              apollo_smmu_tbu::ARCH_CMD_CFGI_VMS_PIDM |
                  (static_cast<uint64_t>(other_vmid)
                   << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT));
    store_u64(cmdq_base + sizeof(uint64_t), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_EQ(vms_ptr, m_tbu.m_arch_last_vms_ptr);
    EXPECT_NE(0u, m_tbu.m_arch_last_ste5);
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_cfgis);
    EXPECT_EQ(0u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(other_vmid, m_tbu.m_arch_last_cmd_vmid);

    /*
     * CMD_CFGI_VMS_PIDM targets the VMID-indexed PARTID_MAP/VMS state; the
     * modeled cache slice keeps only the last fetched VMS state, so a matching
     * VMID invalidates it without flushing ATS/TLB entries.
     */
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              apollo_smmu_tbu::ARCH_CMD_CFGI_VMS_PIDM |
                  (static_cast<uint64_t>(vmid)
                   << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_EQ(2u, reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS)));
    EXPECT_EQ(0u, m_tbu.m_arch_last_vms_ptr);
    EXPECT_EQ(0u, m_tbu.m_arch_last_ste5);
    EXPECT_EQ(2u, m_tbu.m_arch_cmd_cfgis);
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_invalidations);
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(vmid, m_tbu.m_arch_last_cmd_vmid);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_CFGI_VMS_PIDM,
              (reg_read32(apollo_smmu_tbu::REG_ARCH_CMD_STATUS) >> 24) & 0xffu);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqTaggedInvalidationHonorsAsidVmidAndSsid)
{
    constexpr uint64_t page = 0x1000;
    constexpr uint64_t other_page = 0x2000;
    constexpr uint64_t cmdq_base = 0xa000;
    constexpr uint16_t asid_a = 0x1234;
    constexpr uint16_t asid_b = 0x9234;
    constexpr uint16_t asid_c = 0x9235;
    constexpr uint16_t vmid_a = 0x5678;
    constexpr uint16_t vmid_b = 0xd678;
    constexpr uint32_t sid = 0x2;
    constexpr uint32_t ssid_a = 0x55;
    constexpr uint32_t ssid_b = 0x66;

    auto tlbi_word0 = [](uint32_t opcode, uint16_t asid, uint16_t vmid) {
        return static_cast<uint64_t>(opcode) |
               (static_cast<uint64_t>(vmid) << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT) |
               (static_cast<uint64_t>(asid) << apollo_smmu_tbu::ARCH_CMDQ_TLBI_ASID_SHIFT);
    };
    auto atc_word0 = [](uint32_t stream_id, uint32_t ssid) {
        return static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_ATC_INV) |
               (static_cast<uint64_t>(stream_id) << 32) |
               apollo_smmu_tbu::ARCH_CMDQ_SSV |
               (static_cast<uint64_t>(ssid) << apollo_smmu_tbu::ARCH_CMDQ_SSID_SHIFT);
    };

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 3);

    m_tbu.ats_fill(0x1, page, asid_a, vmid_a);
    m_tbu.ats_fill(0x1, other_page, asid_a, vmid_a);
    m_tbu.ats_fill(0x1, page, asid_b, vmid_a);
    m_tbu.ats_fill(0x1, page, asid_a, vmid_b);
    ASSERT_TRUE(m_tbu.ats_lookup(0x1, page, asid_a, vmid_a));
    ASSERT_TRUE(m_tbu.ats_lookup(0x1, other_page, asid_a, vmid_a));

    store_u64(cmdq_base,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_ASID, asid_a, vmid_a));
    store_u64(cmdq_base + sizeof(uint64_t), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_FALSE(m_tbu.ats_lookup(0x1, page, asid_a, vmid_a));
    EXPECT_FALSE(m_tbu.ats_lookup(0x1, other_page, asid_a, vmid_a));
    EXPECT_TRUE(m_tbu.ats_lookup(0x1, page, asid_b, vmid_a));
    EXPECT_TRUE(m_tbu.ats_lookup(0x1, page, asid_a, vmid_b));
    EXPECT_EQ(asid_a, m_tbu.m_arch_last_cmd_asid);
    EXPECT_EQ(vmid_a, m_tbu.m_arch_last_cmd_vmid);
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_invalidated);

    m_tbu.ats_fill(0x1, page, asid_a, vmid_a);
    m_tbu.ats_fill(0x1, page, asid_c, vmid_a);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VA, asid_c, vmid_a));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), page);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_FALSE(m_tbu.ats_lookup(0x1, page, asid_c, vmid_a));
    EXPECT_TRUE(m_tbu.ats_lookup(0x1, page, asid_a, vmid_a));
    EXPECT_TRUE(m_tbu.ats_lookup(0x1, page, asid_b, vmid_a));
    EXPECT_TRUE(m_tbu.ats_lookup(0x1, page, asid_a, vmid_b));
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);

    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VAA, 0, vmid_a));
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), page);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 3);

    EXPECT_FALSE(m_tbu.ats_lookup(0x1, page, asid_a, vmid_a));
    EXPECT_FALSE(m_tbu.ats_lookup(0x1, page, asid_b, vmid_a));
    EXPECT_TRUE(m_tbu.ats_lookup(0x1, page, asid_a, vmid_b));
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_invalidated);

    m_tbu.clear_ats_cache();
    m_tbu.ats_fill(sid, page, 0, 0, true, ssid_a);
    m_tbu.ats_fill(sid, page, 0, 0, true, ssid_b);
    m_tbu.ats_fill(sid, page, 0, 0, false, 0);

    store_u64(cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES, atc_word0(sid, ssid_a));
    store_u64(cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES + sizeof(uint64_t), page);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);

    EXPECT_FALSE(m_tbu.ats_lookup(sid, page, 0, 0, true, ssid_a));
    EXPECT_TRUE(m_tbu.ats_lookup(sid, page, 0, 0, true, ssid_b));
    EXPECT_TRUE(m_tbu.ats_lookup(sid, page, 0, 0, false, 0));
    EXPECT_TRUE(m_tbu.m_arch_last_cmd_ssid_valid);
    EXPECT_EQ(ssid_a, m_tbu.m_arch_last_cmd_ssid);
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqTlbiRangeInvalidatesModeledAtsSpan)
{
    constexpr uint64_t base_page = 0x4000;
    constexpr uint64_t cmdq_base = 0xa800;
    constexpr uint16_t asid = 0x21;
    constexpr uint16_t other_asid = 0x22;
    constexpr uint16_t vmid = 0x31;
    constexpr uint16_t other_vmid = 0x32;
    constexpr uint32_t sid = 0x4;

    auto tlbi_range_word0 = [](uint32_t opcode, uint16_t asid_value,
                               uint16_t vmid_value, uint32_t pages) {
        return static_cast<uint64_t>(opcode) |
               (static_cast<uint64_t>(pages - 1)
                << apollo_smmu_tbu::ARCH_CMDQ_RANGE_NUM_SHIFT) |
               (static_cast<uint64_t>(vmid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT) |
               (static_cast<uint64_t>(asid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_ASID_SHIFT);
    };
    auto tlbi_range_word1 = [](uint64_t page) {
        return page |
               (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_4K)
                << apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_SHIFT);
    };

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);

    m_tbu.clear_ats_cache();
    for (uint32_t page = 0; page < 4; ++page) {
        m_tbu.ats_fill(sid, base_page + page * apollo_smmu_tbu::PAGE_SIZE,
                       asid, vmid);
    }
    m_tbu.ats_fill(sid, base_page + 4 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid);
    m_tbu.ats_fill(sid, base_page, other_asid, vmid);

    store_u64(cmdq_base,
              tlbi_range_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VA, asid,
                                vmid, 4));
    store_u64(cmdq_base + sizeof(uint64_t), tlbi_range_word1(base_page));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    for (uint32_t page = 0; page < 4; ++page) {
        EXPECT_FALSE(m_tbu.ats_lookup(
            sid, base_page + page * apollo_smmu_tbu::PAGE_SIZE, asid, vmid));
    }
    EXPECT_TRUE(m_tbu.ats_lookup(
        sid, base_page + 4 * apollo_smmu_tbu::PAGE_SIZE, asid, vmid));
    EXPECT_TRUE(m_tbu.ats_lookup(sid, base_page, other_asid, vmid));
    EXPECT_EQ(4u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(4 * apollo_smmu_tbu::PAGE_SIZE,
              m_tbu.m_arch_last_cmd_range_bytes);

    m_tbu.ats_fill(sid, base_page + 8 * apollo_smmu_tbu::PAGE_SIZE,
                   other_asid, vmid);
    m_tbu.ats_fill(sid, base_page + 9 * apollo_smmu_tbu::PAGE_SIZE,
                   asid, vmid);
    m_tbu.ats_fill(sid, base_page + 9 * apollo_smmu_tbu::PAGE_SIZE,
                   asid, other_vmid);

    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_range_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VAA, 0,
                                vmid, 2));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              tlbi_range_word1(base_page + 8 * apollo_smmu_tbu::PAGE_SIZE));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_FALSE(m_tbu.ats_lookup(
        sid, base_page + 8 * apollo_smmu_tbu::PAGE_SIZE, other_asid, vmid));
    EXPECT_FALSE(m_tbu.ats_lookup(
        sid, base_page + 9 * apollo_smmu_tbu::PAGE_SIZE, asid, vmid));
    EXPECT_TRUE(m_tbu.ats_lookup(
        sid, base_page + 9 * apollo_smmu_tbu::PAGE_SIZE, asid, other_vmid));
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(2 * apollo_smmu_tbu::PAGE_SIZE,
              m_tbu.m_arch_last_cmd_range_bytes);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqTlbiRangeReservedEncodingIsIllegal)
{
    constexpr uint64_t cmdq_base = 0xa900;
    constexpr uint64_t secure_cmdq_base = 0xaa00;
    constexpr uint64_t page = 0x5000;
    constexpr uint16_t asid = 0x25;
    constexpr uint16_t vmid = 0x35;
    constexpr uint32_t sid = 0x5;

    auto reserved_range_word0 = [=](bool ssec) {
        return static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VA) |
               (ssec ? apollo_smmu_tbu::ARCH_CMDQ_SSEC : 0) |
               (static_cast<uint64_t>(vmid)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT) |
               (static_cast<uint64_t>(asid)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_ASID_SHIFT);
    };
    auto reserved_range_word1 = [=] {
        return page |
               (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_4K)
                << apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_SHIFT);
    };

    m_tbu.clear_ats_cache();
    m_tbu.ats_fill(sid, page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE);
    store_u64(cmdq_base, reserved_range_word0(false));
    store_u64(cmdq_base + sizeof(uint64_t), reserved_range_word1());

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    uint32_t cons = reg_read32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_NE(0u, reg_read_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_TRUE(m_tbu.ats_lookup(sid, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_NONSECURE));
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_tlbis);
    EXPECT_EQ(0u, m_tbu.m_arch_last_cmd_range_bytes);

    m_tbu.ats_fill(sid, page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_SECURE);
    store_u64(secure_cmdq_base, reserved_range_word0(true));
    store_u64(secure_cmdq_base + sizeof(uint64_t), reserved_range_word1());

    reg_s_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                  apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, secure_cmdq_base | 2);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    cons = reg_read32(smmu_s_reg(apollo_smmu_tbu::SMMUV3_CMDQ_CONS));
    EXPECT_EQ(0u, cons & apollo_smmu_tbu::ARCH_CMDQ_CONS_RD_MASK);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_CERROR_ILL,
              (cons >> apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_SHIFT) &
                  apollo_smmu_tbu::ARCH_CMDQ_CONS_ERR_MASK);
    EXPECT_NE(0u, reg_read_secure_gerror_active() &
                      apollo_smmu_tbu::ARCH_GERROR_CMDQ_ABORT);
    EXPECT_TRUE(m_tbu.ats_lookup(sid, page, asid, vmid, false, 0,
                                 apollo_smmu_tbu::ARCH_SECURITY_SECURE));
    EXPECT_EQ(0u, m_tbu.m_arch_cmd_tlbis);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_SECURITY_SECURE,
              m_tbu.m_arch_last_cmd_security_state);
    EXPECT_TRUE(m_tbu.m_arch_last_cmd_ssec);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqTlbiTtlLeafHintsFollowRangeTg)
{
    constexpr uint64_t base_page = 0x8000;
    constexpr uint64_t cmdq_base = 0xab00;
    constexpr uint16_t asid = 0x26;
    constexpr uint16_t vmid = 0x36;
    constexpr uint32_t sid = 0x6;

    auto tlbi_word0 = [=](uint32_t pages) {
        return static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMD_TLBI_NH_VA) |
               (static_cast<uint64_t>(pages - 1)
                << apollo_smmu_tbu::ARCH_CMDQ_RANGE_NUM_SHIFT) |
               (static_cast<uint64_t>(vmid)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT) |
               (static_cast<uint64_t>(asid)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_ASID_SHIFT);
    };
    auto range_word1 = [](uint64_t page, uint32_t tg, uint32_t ttl,
                          bool leaf) {
        return page |
               (static_cast<uint64_t>(tg)
                << apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_SHIFT) |
               (static_cast<uint64_t>(ttl)
                << apollo_smmu_tbu::ARCH_CMDQ_TTL_SHIFT) |
               (leaf ? apollo_smmu_tbu::ARCH_CMDQ_LEAF : 0);
    };

    m_tbu.clear_ats_cache();
    m_tbu.ats_fill(sid, base_page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE, 2,
                   apollo_smmu_tbu::PAGE_SIZE);
    m_tbu.ats_fill(sid, base_page + apollo_smmu_tbu::PAGE_SIZE, asid, vmid,
                   false, 0, apollo_smmu_tbu::ARCH_SECURITY_NONSECURE, 2,
                   apollo_smmu_tbu::PAGE_SIZE);
    m_tbu.ats_fill(sid, base_page + 2 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE, 3,
                   apollo_smmu_tbu::PAGE_SIZE);

    store_u64(cmdq_base, tlbi_word0(3));
    store_u64(cmdq_base + sizeof(uint64_t),
              range_word1(base_page, apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_4K,
                          2, true));

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page, asid, vmid));
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page + apollo_smmu_tbu::PAGE_SIZE,
                                  asid, vmid));
    EXPECT_TRUE(m_tbu.ats_lookup(sid,
                                 base_page + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                 asid, vmid));
    EXPECT_EQ(3 * apollo_smmu_tbu::PAGE_SIZE,
              m_tbu.m_arch_last_cmd_range_bytes);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_4K,
              m_tbu.m_arch_last_cmd_tg);
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_ttl);
    EXPECT_TRUE(m_tbu.m_arch_last_cmd_leaf);
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_invalidated);

    const uint64_t single_page = base_page + 4 * apollo_smmu_tbu::PAGE_SIZE;
    m_tbu.ats_fill(sid, single_page, asid, vmid);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(1));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              range_word1(single_page, 0, 3, true));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);

    EXPECT_FALSE(m_tbu.ats_lookup(sid, single_page, asid, vmid));
    EXPECT_EQ(apollo_smmu_tbu::PAGE_SIZE, m_tbu.m_arch_last_cmd_range_bytes);
    EXPECT_EQ(0u, m_tbu.m_arch_last_cmd_tg);
    EXPECT_EQ(0u, m_tbu.m_arch_last_cmd_ttl);
    EXPECT_TRUE(m_tbu.m_arch_last_cmd_leaf);

    const uint64_t leaf_zero_page = base_page + 8 * apollo_smmu_tbu::PAGE_SIZE;
    m_tbu.ats_fill(sid, leaf_zero_page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE, 2,
                   apollo_smmu_tbu::PAGE_SIZE);
    m_tbu.ats_fill(sid, leaf_zero_page + apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE, 2,
                   apollo_smmu_tbu::PAGE_SIZE);
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(2));
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              range_word1(leaf_zero_page,
                          apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_4K, 2, false));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 3);

    EXPECT_FALSE(m_tbu.ats_lookup(sid, leaf_zero_page, asid, vmid));
    EXPECT_FALSE(m_tbu.ats_lookup(sid, leaf_zero_page + apollo_smmu_tbu::PAGE_SIZE,
                                  asid, vmid));
    EXPECT_FALSE(m_tbu.m_arch_last_cmd_leaf);
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_ttl);
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_table_invalidated);
    EXPECT_EQ(1u, m_tbu.m_arch_cmd_table_invalidations);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqAdditionalTlbiOpcodesInvalidateModeledAts)
{
    constexpr uint64_t base_page = 0xc000;
    constexpr uint64_t cmdq_base = 0xac00;
    constexpr uint16_t asid = 0x27;
    constexpr uint16_t other_asid = 0x28;
    constexpr uint16_t vmid = 0x37;
    constexpr uint16_t other_vmid = 0x38;
    constexpr uint32_t sid = 0x7;

    auto tlbi_word0 = [](uint32_t opcode, uint16_t asid_value,
                         uint16_t vmid_value, uint32_t pages = 1) {
        return static_cast<uint64_t>(opcode) |
               (pages > 1 ?
                    (static_cast<uint64_t>(pages - 1)
                     << apollo_smmu_tbu::ARCH_CMDQ_RANGE_NUM_SHIFT) :
                    0) |
               (static_cast<uint64_t>(vmid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_VMID_SHIFT) |
               (static_cast<uint64_t>(asid_value)
                << apollo_smmu_tbu::ARCH_CMDQ_TLBI_ASID_SHIFT);
    };
    auto range_word1 = [](uint64_t page, bool leaf = true) {
        return page |
               (static_cast<uint64_t>(apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_4K)
                << apollo_smmu_tbu::ARCH_CMDQ_RANGE_TG_SHIFT) |
               (leaf ? apollo_smmu_tbu::ARCH_CMDQ_LEAF : 0);
    };

    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 3);

    m_tbu.clear_ats_cache();
    m_tbu.ats_fill(sid, base_page, asid, vmid);
    m_tbu.ats_fill(sid, base_page + apollo_smmu_tbu::PAGE_SIZE, other_asid,
                   other_vmid);
    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_TLBI_NSNH_ALL);
    store_u64(cmdq_base + sizeof(uint64_t), 0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page, asid, vmid));
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page + apollo_smmu_tbu::PAGE_SIZE,
                                  other_asid, other_vmid));
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_NSNH_ALL,
              m_tbu.m_arch_last_cmd_opcode);

    m_tbu.ats_fill(sid, base_page, asid, vmid);
    m_tbu.ats_fill(sid, base_page, asid, other_vmid);
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_S12_VMALL, 0,
                         vmid));
    store_u64(cmdq_base + apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 2);
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page, asid, vmid));
    EXPECT_TRUE(m_tbu.ats_lookup(sid, base_page, asid, other_vmid));
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_S12_VMALL,
              m_tbu.m_arch_last_cmd_opcode);

    m_tbu.ats_fill(sid, base_page + 2 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid);
    m_tbu.ats_fill(sid, base_page + 3 * apollo_smmu_tbu::PAGE_SIZE, other_asid,
                   vmid);
    m_tbu.ats_fill(sid, base_page + 4 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   other_vmid);
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_S2_IPA, 0, vmid,
                         2));
    store_u64(cmdq_base + 2 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              range_word1(base_page + 2 * apollo_smmu_tbu::PAGE_SIZE));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 3);
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page + 2 * apollo_smmu_tbu::PAGE_SIZE,
                                  asid, vmid));
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page + 3 * apollo_smmu_tbu::PAGE_SIZE,
                                  other_asid, vmid));
    EXPECT_TRUE(m_tbu.ats_lookup(sid, base_page + 4 * apollo_smmu_tbu::PAGE_SIZE,
                                 asid, other_vmid));
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(2 * apollo_smmu_tbu::PAGE_SIZE,
              m_tbu.m_arch_last_cmd_range_bytes);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_S2_IPA,
              m_tbu.m_arch_last_cmd_opcode);

    m_tbu.ats_fill(sid, base_page + 5 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid);
    m_tbu.ats_fill(sid, base_page + 5 * apollo_smmu_tbu::PAGE_SIZE, other_asid,
                   vmid);
    store_u64(cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_EL2_ASID, asid,
                         vmid));
    store_u64(cmdq_base + 3 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              0);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 4);
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page + 5 * apollo_smmu_tbu::PAGE_SIZE,
                                  asid, vmid));
    EXPECT_TRUE(m_tbu.ats_lookup(sid, base_page + 5 * apollo_smmu_tbu::PAGE_SIZE,
                                 other_asid, vmid));
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_EL2_ASID,
              m_tbu.m_arch_last_cmd_opcode);

    m_tbu.ats_fill(sid, base_page + 6 * apollo_smmu_tbu::PAGE_SIZE, asid,
                   vmid);
    m_tbu.ats_fill(sid, base_page + 7 * apollo_smmu_tbu::PAGE_SIZE, other_asid,
                   vmid);
    store_u64(cmdq_base + 4 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES,
              tlbi_word0(apollo_smmu_tbu::ARCH_CMD_TLBI_EL2_VAA, 0, vmid,
                         2));
    store_u64(cmdq_base + 4 * apollo_smmu_tbu::ARCH_CMDQ_ENTRY_BYTES +
                  sizeof(uint64_t),
              range_word1(base_page + 6 * apollo_smmu_tbu::PAGE_SIZE));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 5);
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page + 6 * apollo_smmu_tbu::PAGE_SIZE,
                                  asid, vmid));
    EXPECT_FALSE(m_tbu.ats_lookup(sid, base_page + 7 * apollo_smmu_tbu::PAGE_SIZE,
                                  other_asid, vmid));
    EXPECT_TRUE(m_tbu.ats_lookup(sid, base_page + 4 * apollo_smmu_tbu::PAGE_SIZE,
                                 asid, other_vmid));
    EXPECT_EQ(2u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_EL2_VAA,
              m_tbu.m_arch_last_cmd_opcode);
}

TEST_BENCH(ApolloSmmuTbuTestBench, CmdqTlbiNsnhAllPreservesEl2RegimeEntries)
{
    constexpr uint64_t cmdq_base = 0xb800;
    constexpr uint32_t sid = 0x2;
    constexpr uint16_t asid = 0x44;
    constexpr uint16_t vmid = 0x55;
    constexpr uint64_t nsnh_page = 0x19000000;
    constexpr uint64_t el2_page = 0x19001000;

    m_tbu.clear_ats_cache();
    m_tbu.ats_fill(sid, nsnh_page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
                   apollo_smmu_tbu::ARCH_LEVELS - 1,
                   apollo_smmu_tbu::PAGE_SIZE,
                   apollo_smmu_tbu::ARCH_TLBI_REGIME_NSNH);
    m_tbu.ats_fill(sid, el2_page, asid, vmid, false, 0,
                   apollo_smmu_tbu::ARCH_SECURITY_NONSECURE,
                   apollo_smmu_tbu::ARCH_LEVELS - 1,
                   apollo_smmu_tbu::PAGE_SIZE,
                   apollo_smmu_tbu::ARCH_TLBI_REGIME_EL2);

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_TLBI_NSNH_ALL);
    store_u64(cmdq_base + sizeof(uint64_t), 0);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO,
                apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI, cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CR0), ARCH_CR0_ALL_QUEUES);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);

    EXPECT_FALSE(m_tbu.ats_lookup(sid, nsnh_page, asid, vmid));
    EXPECT_TRUE(m_tbu.ats_lookup(sid, el2_page, asid, vmid));
    EXPECT_EQ(1u, m_tbu.m_arch_last_cmd_invalidated);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_CMD_TLBI_NSNH_ALL,
              m_tbu.m_arch_last_cmd_opcode);
}

TEST_BENCH(ApolloSmmuTbuTestBench, EndpointSubstreamIdTagsAtsAndFaultEvents)
{
    constexpr uint32_t stream_id = 0x1;
    constexpr uint32_t ssid_a = 0x123;
    constexpr uint32_t ssid_b = 0x124;
    constexpr uint64_t iova = 0x10000000;
    constexpr uint64_t pa = 0x12000;
    constexpr uint64_t fault_iova = 0x10008000;
    constexpr uint64_t eventq_base = 0x6000;
    uint32_t value = 0;

    store_u64(pa, 0x55667788);
    add_map(stream_id, iova, pa, apollo_smmu_tbu::PAGE_SIZE);

    ASSERT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, true, ssid_a, iova, value));
    EXPECT_EQ(0x55667788u, value);
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova),
                                 0, 0, true, ssid_a));

    ASSERT_EQ(tlm::TLM_OK_RESPONSE, stream_read32(stream_id, true, ssid_b, iova, value));
    EXPECT_TRUE(m_tbu.ats_lookup(stream_id, apollo_smmu_tbu::page_base(iova),
                                 0, 0, true, ssid_b));

    m_tbu.m_eventq.base = eventq_base | 2;
    m_tbu.record_fault(stream_id, "endpoint-negative", fault_iova, sizeof(value),
                       false, true, ssid_a);

    const uint64_t word0 = load_u64(eventq_base);
    EXPECT_EQ(apollo_smmu_tbu::ARCH_EVENT_SYNTHETIC, word0 & 0xffu);
    EXPECT_NE(0u, word0 & (1ULL << 11));
    EXPECT_EQ(ssid_a, (word0 >> 12) & apollo_smmu_tbu::ARCH_CMDQ_SSID_MASK);
    EXPECT_EQ(stream_id, word0 >> 32);
    EXPECT_EQ(fault_iova, load_u64(eventq_base + sizeof(uint64_t)));
}

TEST_BENCH(ApolloSmmuTbuTestBench, ArchitectedIrqOutputsFollowQueueStatus)
{
    constexpr uint64_t cmdq_base = 0x8000;

    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_IRQ_CTRL), 0x0f);
    EXPECT_FALSE(irq_line(0));
    EXPECT_FALSE(irq_line(1));
    EXPECT_FALSE(irq_line(2));
    EXPECT_FALSE(irq_line(3));

    m_tbu.m_eventq.base = 0x3000 | 2;
    m_tbu.push_event_record("unit-test", 0x101000, 0x1000);
    EXPECT_TRUE(irq_line(0));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_EVENTQ_CONS), m_tbu.m_eventq.prod);
    EXPECT_FALSE(irq_line(0));

    m_tbu.m_priq.base = 0x4000 | 2;
    m_tbu.push_pri_record(0x102000, 0x202000, 0x1000);
    EXPECT_TRUE(irq_line(1));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_PRIQ_CONS), m_tbu.m_priq.prod);
    EXPECT_FALSE(irq_line(1));

    store_u64(cmdq_base, apollo_smmu_tbu::ARCH_CMD_SYNC);
    store_u64(cmdq_base + 8, 0);
    reg_write64(apollo_smmu_tbu::SMMUV3_CMDQ_BASE_LO, apollo_smmu_tbu::SMMUV3_CMDQ_BASE_HI,
                cmdq_base | 2);
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_CMDQ_PROD), 1);
    EXPECT_TRUE(irq_line(2));

    m_tbu.m_eventq.base = 0x7000 | 1;
    m_tbu.m_eventq.prod = 1;
    m_tbu.m_eventq.cons = 0;
    m_tbu.push_event_record("overflow", 0x1000, 0x1000);
    EXPECT_TRUE(irq_line(3));
    reg_write32(smmu_reg(apollo_smmu_tbu::SMMUV3_GERRORN),
                apollo_smmu_tbu::ARCH_GERROR_QUEUE_OVERFLOW);
    EXPECT_FALSE(irq_line(3));
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker {};

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
