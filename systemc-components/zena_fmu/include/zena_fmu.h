/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class zena_fmu : public sc_core::sc_module
{
    static constexpr unsigned int MAX_BANKS = 8;
    static constexpr unsigned int MAX_RECORDS = 384;
    static constexpr uint32_t BANK_BYTES = 0x10000;
    static constexpr uint32_t RECORD_STRIDE = 0x40;
    static constexpr uint32_t RECORD_SPACE_BYTES = 0x6000;
    static constexpr uint32_t ERR_FR_OFFSET = 0x0000;
    static constexpr uint32_t ERR_CTLR_OFFSET = 0x0008;
    static constexpr uint32_t ERR_STATUS_OFFSET = 0x0010;
    static constexpr uint32_t ERR_IMPDEF_BASE = 0x8000;
    static constexpr uint32_t ERR_IMPDEF_STRIDE = 0x8;
    static constexpr uint32_t SYS_KEY = 0x8bfc;
    static constexpr uint32_t ERRGSR_LOW_BASE = 0xe000;
    static constexpr uint32_t ERRGSR_HIGH_BASE = 0xe004;
    static constexpr uint32_t ERRGSR_STRIDE = 0x8;
    static constexpr uint32_t PIDR4 = 0xffd0;
    static constexpr uint32_t PIDR0 = 0xffe0;
    static constexpr uint32_t PIDR1 = 0xffe4;
    static constexpr uint32_t PIDR2 = 0xffe8;
    static constexpr uint32_t PIDR3 = 0xffec;
    static constexpr uint32_t CIDR0 = 0xfff0;
    static constexpr uint32_t CIDR1 = 0xfff4;
    static constexpr uint32_t CIDR2 = 0xfff8;
    static constexpr uint32_t CIDR3 = 0xfffc;

    static constexpr uint32_t SYS_KEY_VALUE = 0xbe;
    static constexpr uint32_t CTLR_ED = 1u << 0;
    static constexpr uint32_t CTLR_FI = 1u << 3;
    static constexpr uint32_t CTLR_UE = 1u << 4;
    static constexpr uint32_t CTLR_CFI = 1u << 8;
    static constexpr uint32_t CTLR_CI = 1u << 13;
    static constexpr uint32_t CTLR_RW_MASK = CTLR_ED | CTLR_FI | CTLR_UE | CTLR_CFI | CTLR_CI;

    static constexpr uint32_t STATUS_SERR_MASK = 0x000000ff;
    static constexpr uint32_t STATUS_IERR_MASK = 0x00001f00;
    static constexpr uint32_t STATUS_CI = 1u << 19;
    static constexpr uint32_t STATUS_CE_MASK = 0x03000000;
    static constexpr uint32_t STATUS_OF = 1u << 27;
    static constexpr uint32_t STATUS_UE = 1u << 29;
    static constexpr uint32_t STATUS_V = 1u << 30;
    static constexpr uint32_t STATUS_RW_WHEN_VALID_MASK = STATUS_SERR_MASK | STATUS_IERR_MASK;
    static constexpr uint32_t STATUS_W1C_MASK = STATUS_V | STATUS_UE | STATUS_OF | STATUS_CE_MASK | STATUS_CI;
    static constexpr uint32_t STATUS_IERR_APB_SW = 1u << 8;
    static constexpr uint32_t STATUS_IERR_INC_SEQ = 1u << 9;
    static constexpr uint32_t STATUS_IERR_ERR_IN = 1u << 12;
    static constexpr uint32_t STATUS_SERR_SW = 1u << 0;

    static constexpr uint32_t IMPDEF_UE = 1u << 0;
    static constexpr uint32_t IMPDEF_DE_MASK = 0x000001f0;
    static constexpr uint32_t IMPDEF_IE_MASK = 0x00003e00;
    static constexpr uint32_t IMPDEF_IC_MASK = 0x0000000c;
    static constexpr uint32_t IMPDEF_STORED_MASK = 0xffff01f1;

    struct Bank {
        std::array<uint32_t, MAX_RECORDS> ctlr {};
        std::array<uint32_t, MAX_RECORDS> status {};
        std::array<uint32_t, MAX_RECORDS> impdef {};
        bool unlocked = false;
    };

    std::array<Bank, MAX_BANKS> m_banks {};
    unsigned int m_trace_count = 0;
    bool m_critical_irq = false;
    bool m_non_critical_irq = false;

    unsigned int bank_count() const
    {
        return std::min<unsigned int>(p_bank_count.get_value(), MAX_BANKS);
    }

    unsigned int record_count() const
    {
        return std::min<unsigned int>(p_record_count.get_value(), MAX_RECORDS);
    }

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    static bool is_aligned32(uint64_t offset)
    {
        return (offset & 0x3u) == 0;
    }

    bool decode(uint64_t offset, unsigned int& bank, uint32_t& reg) const
    {
        bank = static_cast<unsigned int>(offset / BANK_BYTES);
        reg = static_cast<uint32_t>(offset % BANK_BYTES);
        return bank < bank_count();
    }

    bool decode_record(uint32_t reg, unsigned int& index, uint32_t& field) const
    {
        if (reg >= RECORD_SPACE_BYTES) {
            return false;
        }

        index = reg / RECORD_STRIDE;
        field = reg % RECORD_STRIDE;
        return index < record_count();
    }

    bool decode_impdef(uint32_t reg, unsigned int& index) const
    {
        if (reg < ERR_IMPDEF_BASE) {
            return false;
        }

        const uint32_t rel = reg - ERR_IMPDEF_BASE;
        if ((rel % ERR_IMPDEF_STRIDE) != 0) {
            return false;
        }

        index = rel / ERR_IMPDEF_STRIDE;
        return index < record_count();
    }

    uint32_t default_impdef(unsigned int index) const
    {
        return index == (MAX_RECORDS - 1) ? 0x00000070u : 0x00000180u;
    }

    bool is_critical_record(unsigned int index) const
    {
        if (index < 64) {
            return (p_critical_mask.get_value() & (1ull << index)) != 0;
        }

        return false;
    }

    bool is_non_critical_record(unsigned int index) const
    {
        if (is_critical_record(index)) {
            return false;
        }

        if (index < 64) {
            return (p_non_critical_mask.get_value() & (1ull << index)) != 0;
        }

        return true;
    }

    bool require_unlock(Bank& bank)
    {
        if (!p_enforce_sys_key.get_value()) {
            return true;
        }

        if (bank.unlocked) {
            bank.unlocked = false;
            return true;
        }

        set_sequence_error(bank);
        return false;
    }

    void set_sequence_error(Bank& bank)
    {
        const unsigned int rec = record_count() == 0 ? 0 : record_count() - 1;
        bank.status[rec] |= STATUS_V | STATUS_IERR_INC_SEQ | STATUS_SERR_SW;
        update_irqs();
    }

    void set_software_error(Bank& bank, unsigned int index, bool critical)
    {
        uint32_t status = STATUS_V | STATUS_IERR_ERR_IN | STATUS_SERR_SW;

        if (critical) {
            status |= STATUS_CI;
        } else {
            status |= STATUS_UE;
        }

        bank.status[index] |= status;
        update_irqs();
    }

    void clear_record_status(Bank& bank, unsigned int index, uint32_t value)
    {
        uint32_t status = bank.status[index];
        status &= ~(value & STATUS_W1C_MASK);

        if (status & STATUS_V) {
            status = (status & ~STATUS_RW_WHEN_VALID_MASK) |
                     (value & STATUS_RW_WHEN_VALID_MASK);
        }

        bank.status[index] = status;
        update_irqs();
    }

    void write_ctlr(Bank& bank, unsigned int index, uint32_t value)
    {
        bank.ctlr[index] = (bank.ctlr[index] & ~CTLR_RW_MASK) |
                           (value & CTLR_RW_MASK);
        update_irqs();
    }

    void write_impdef(Bank& bank, unsigned int index, uint32_t value)
    {
        bank.impdef[index] = (bank.impdef[index] & ~IMPDEF_STORED_MASK) |
                             (value & IMPDEF_STORED_MASK);

        if (value & IMPDEF_IC_MASK) {
            bank.status[index] &= ~(STATUS_V | STATUS_UE | STATUS_OF |
                                    STATUS_CE_MASK | STATUS_CI |
                                    STATUS_IERR_MASK | STATUS_SERR_MASK);
        }

        if ((value & IMPDEF_IE_MASK) != 0 && (bank.ctlr[index] & CTLR_ED) != 0) {
            const bool critical = is_critical_record(index);
            if ((value & IMPDEF_DE_MASK) != 0 || (value & IMPDEF_UE) != 0) {
                set_software_error(bank, index, critical);
            } else {
                set_software_error(bank, index, critical);
            }
            return;
        }

        update_irqs();
    }

    uint32_t group_status_word(const Bank& bank, unsigned int group, bool high) const
    {
        const unsigned int base = group * 64 + (high ? 32 : 0);
        uint32_t value = 0;

        for (unsigned int i = 0; i < 32; ++i) {
            const unsigned int rec = base + i;
            if (rec >= record_count()) {
                break;
            }
            if ((bank.status[rec] & STATUS_V) != 0) {
                value |= 1u << i;
            }
        }

        return value;
    }

    uint32_t pidcid_value(uint32_t reg) const
    {
        switch (reg) {
        case PIDR4:
            return p_pidr4.get_value();
        case PIDR0:
            return p_pidr0.get_value();
        case PIDR1:
            return p_pidr1.get_value();
        case PIDR2:
            return p_pidr2.get_value();
        case PIDR3:
            return p_pidr3.get_value();
        case CIDR0:
            return p_cidr0.get_value();
        case CIDR1:
            return p_cidr1.get_value();
        case CIDR2:
            return p_cidr2.get_value();
        case CIDR3:
            return p_cidr3.get_value();
        default:
            return 0;
        }
    }

    uint32_t read32(uint64_t offset)
    {
        unsigned int bank_index = 0;
        uint32_t reg = 0;
        if (!decode(offset, bank_index, reg)) {
            return 0;
        }

        Bank& bank = m_banks[bank_index];
        unsigned int index = 0;
        uint32_t field = 0;

        if (decode_record(reg, index, field)) {
            switch (field) {
            case ERR_FR_OFFSET:
                return 0;
            case ERR_CTLR_OFFSET:
                return bank.ctlr[index];
            case ERR_STATUS_OFFSET:
                return bank.status[index];
            default:
                return 0;
            }
        }

        if (decode_impdef(reg, index)) {
            return bank.impdef[index];
        }

        if (reg == SYS_KEY) {
            return bank.unlocked ? SYS_KEY_VALUE : 0;
        }

        if (reg >= ERRGSR_LOW_BASE &&
            reg < ERRGSR_LOW_BASE + 6 * ERRGSR_STRIDE) {
            const uint32_t rel = reg - ERRGSR_LOW_BASE;
            if ((rel % ERRGSR_STRIDE) == 0) {
                return group_status_word(bank, rel / ERRGSR_STRIDE, false);
            }
        }

        if (reg >= ERRGSR_HIGH_BASE &&
            reg < ERRGSR_HIGH_BASE + 6 * ERRGSR_STRIDE) {
            const uint32_t rel = reg - ERRGSR_HIGH_BASE;
            if ((rel % ERRGSR_STRIDE) == 0) {
                return group_status_word(bank, rel / ERRGSR_STRIDE, true);
            }
        }

        return pidcid_value(reg);
    }

    void write32(uint64_t offset, uint32_t value)
    {
        unsigned int bank_index = 0;
        uint32_t reg = 0;
        if (!decode(offset, bank_index, reg)) {
            return;
        }

        Bank& bank = m_banks[bank_index];
        unsigned int index = 0;
        uint32_t field = 0;

        if (reg == SYS_KEY) {
            bank.unlocked = (value == SYS_KEY_VALUE);
            if (!bank.unlocked) {
                set_sequence_error(bank);
            }
            return;
        }

        if (decode_record(reg, index, field)) {
            if (!require_unlock(bank)) {
                return;
            }

            switch (field) {
            case ERR_CTLR_OFFSET:
                write_ctlr(bank, index, value);
                break;
            case ERR_STATUS_OFFSET:
                clear_record_status(bank, index, value);
                break;
            default:
                break;
            }
            return;
        }

        if (decode_impdef(reg, index)) {
            if (!require_unlock(bank)) {
                return;
            }
            write_impdef(bank, index, value);
            return;
        }

        require_unlock(bank);
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) ||
            offset + len > static_cast<uint64_t>(bank_count()) * BANK_BYTES) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            uint64_t remaining = len;
            uint64_t cur = offset;
            uint8_t* out = data;
            while (remaining > 0) {
                const uint32_t value = read32(cur & ~0x3ull);
                const unsigned int shift = static_cast<unsigned int>(cur & 0x3ull);
                const unsigned int chunk = std::min<uint64_t>(remaining, 4 - shift);
                std::memcpy(out, reinterpret_cast<const uint8_t*>(&value) + shift, chunk);
                remaining -= chunk;
                cur += chunk;
                out += chunk;
            }
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            if (len == sizeof(uint32_t) && is_aligned32(offset)) {
                uint32_t value = 0;
                std::memcpy(&value, data, sizeof(value));
                write32(offset, value);
            }
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trace_access(trans, offset, len, debug);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset,
                      unsigned int len, bool debug)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value()) {
            return;
        }

        uint32_t value = 0;
        if (len <= sizeof(value)) {
            std::memcpy(&value, trans.get_data_ptr(), len);
        }

        ++m_trace_count;
        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

    void reset_registers()
    {
        for (auto& bank : m_banks) {
            bank.unlocked = false;
            for (unsigned int i = 0; i < MAX_RECORDS; ++i) {
                bank.ctlr[i] = p_default_ctlr.get_value();
                bank.status[i] = 0;
                bank.impdef[i] = default_impdef(i);
            }
        }

        m_critical_irq = false;
        m_non_critical_irq = false;
    }

    void update_irqs()
    {
        bool critical = false;
        bool non_critical = false;

        for (unsigned int bank_index = 0; bank_index < bank_count(); ++bank_index) {
            const Bank& bank = m_banks[bank_index];
            for (unsigned int index = 0; index < record_count(); ++index) {
                if ((bank.status[index] & STATUS_V) == 0) {
                    continue;
                }

                if (is_critical_record(index) && (bank.ctlr[index] & CTLR_CI) != 0) {
                    critical = true;
                }

                if (is_non_critical_record(index) &&
                    (bank.ctlr[index] & (CTLR_FI | CTLR_UE | CTLR_CFI)) != 0) {
                    non_critical = true;
                }
            }
        }

        if (critical != m_critical_irq || non_critical != m_non_critical_irq) {
            m_critical_irq = critical;
            m_non_critical_irq = non_critical;
            emit_irqs();
        }
    }

    void emit_irqs()
    {
        if (critical_irq.size() != 0) {
            critical_irq->write(m_critical_irq);
        }
        if (critical_ssu.size() != 0) {
            critical_ssu->write(m_critical_irq);
        }
        if (non_critical_irq.size() != 0) {
            non_critical_irq->write(m_non_critical_irq);
        }
        if (non_critical_ssu.size() != 0) {
            non_critical_ssu->write(m_non_critical_irq);
        }
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<bool> p_enforce_sys_key;
    cci::cci_param<unsigned int> p_bank_count;
    cci::cci_param<unsigned int> p_record_count;
    cci::cci_param<uint64_t> p_critical_mask;
    cci::cci_param<uint64_t> p_non_critical_mask;
    cci::cci_param<uint32_t> p_default_ctlr;
    cci::cci_param<uint32_t> p_pidr4;
    cci::cci_param<uint32_t> p_pidr0;
    cci::cci_param<uint32_t> p_pidr1;
    cci::cci_param<uint32_t> p_pidr2;
    cci::cci_param<uint32_t> p_pidr3;
    cci::cci_param<uint32_t> p_cidr0;
    cci::cci_param<uint32_t> p_cidr1;
    cci::cci_param<uint32_t> p_cidr2;
    cci::cci_param<uint32_t> p_cidr3;

    tlm_utils::simple_target_socket<zena_fmu, DEFAULT_TLM_BUSWIDTH> target_socket;
    InitiatorSignalSocket<bool> critical_irq;
    InitiatorSignalSocket<bool> non_critical_irq;
    InitiatorSignalSocket<bool> critical_ssu;
    InitiatorSignalSocket<bool> non_critical_ssu;

    explicit zena_fmu(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_enforce_sys_key("enforce_sys_key", true)
        , p_bank_count("bank_count", 5)
        , p_record_count("record_count", MAX_RECORDS)
        , p_critical_mask("critical_mask", 0x1ull)
        , p_non_critical_mask("non_critical_mask", ~0x1ull)
        , p_default_ctlr("default_ctlr", CTLR_ED | CTLR_FI | CTLR_UE | CTLR_CFI | CTLR_CI)
        , p_pidr4("pidr4", 0x00000004)
        , p_pidr0("pidr0", 0x000000d0)
        , p_pidr1("pidr1", 0x000000b3)
        , p_pidr2("pidr2", 0x0000000b)
        , p_pidr3("pidr3", 0x00000000)
        , p_cidr0("cidr0", 0x0000000d)
        , p_cidr1("cidr1", 0x000000f0)
        , p_cidr2("cidr2", 0x00000005)
        , p_cidr3("cidr3", 0x000000b1)
        , target_socket("target_socket")
        , critical_irq("critical_irq")
        , non_critical_irq("non_critical_irq")
        , critical_ssu("critical_ssu")
        , non_critical_ssu("non_critical_ssu")
    {
        reset_registers();
        target_socket.register_b_transport(this, &zena_fmu::b_transport);
        target_socket.register_transport_dbg(this, &zena_fmu::transport_dbg);
    }

    void before_end_of_elaboration() override
    {
        reset_registers();
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access(trans, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans, true) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
