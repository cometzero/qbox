/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <tlm-extensions/apollo-smmu-stream-id.h>
#include <scp/report.h>
#include <tlm_sockets_buswidth.h>

class apollo_hexagon_dma : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    cci::cci_param<uint32_t> p_stream_id;
    cci::cci_param<uint32_t> p_substream_id;
    cci::cci_param<bool> p_substream_id_valid;
    cci::cci_param<bool> p_smmu_translated;

    tlm_utils::simple_target_socket<apollo_hexagon_dma, DEFAULT_TLM_BUSWIDTH> regs;
    tlm_utils::simple_initiator_socket_b<apollo_hexagon_dma, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types,
                                         sc_core::SC_ZERO_OR_MORE_BOUND>
        dma;
    tlm_utils::simple_initiator_socket_b<apollo_hexagon_dma, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types,
                                         sc_core::SC_ZERO_OR_MORE_BOUND>
        translated_dma;
    InitiatorSignalSocket<bool> irq_out;

    explicit apollo_hexagon_dma(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_stream_id("stream_id", 1, "Linux-visible SMMU StreamID for this DMA master")
        , p_substream_id("substream_id", 0, "Endpoint PASID/SubstreamID presented to the SMMU")
        , p_substream_id_valid("substream_id_valid", false, "Whether endpoint PASID/SubstreamID is valid")
        , p_smmu_translated("smmu_translated", false, "DMA transactions traverse an SMMU translated path")
        , regs("regs")
        , dma("dma")
        , translated_dma("translated_dma")
        , irq_out("irq_out")
    {
        regs.register_b_transport(this, &apollo_hexagon_dma::b_transport);
        regs.register_transport_dbg(this, &apollo_hexagon_dma::transport_dbg);
    }

private:
    enum : uint64_t {
        REG_SRC = 0x00,
        REG_DST = 0x04,
        REG_LEN = 0x08,
        REG_CTRL = 0x0c,
        REG_STATUS = 0x10,
        REG_RESULT = 0x14,
        REG_FW_DONE = 0x18,
        REG_CAPS = 0x1c,
        REG_PATH = 0x20,
        REG_STREAM_ID = 0x24,
        REG_JOB_INPUT = 0x40,
        REG_JOB_OUTPUT = 0x44,
        REG_JOB_INPUT_BYTES = 0x48,
        REG_JOB_OUTPUT_BYTES = 0x4c,
        REG_JOB_CTRL = 0x50,
        REG_JOB_STATUS = 0x54,
        REG_JOB_RESULT = 0x58,
        REG_JOB_QUEUE = 0x5c,
        REG_JOB_FENCE = 0x60,
        REG_IRQ_STATUS = 0x64,
        REG_IRQ_ACK = 0x68,
        REG_QUEUE_CAPS = 0x6c,
        REG_PASID = 0x70,
        REG_CMDQ_BASE_LO = 0x80,
        REG_CMDQ_BASE_HI = 0x84,
        REG_CMDQ_SIZE = 0x88,
        REG_CMDQ_HEAD = 0x8c,
        REG_CMDQ_TAIL = 0x90,
        REG_CMDQ_DOORBELL = 0x94,
        REG_CMDQ_STATUS = 0x98,
        REG_CMDQ_FENCE_VALUE = 0x9c,
        REG_CMDQ_FAULT_CODE = 0xa0,
        REG_CMDQ_FAULT_ADDR_LO = 0xa4,
        REG_CMDQ_FAULT_ADDR_HI = 0xa8,
    };

    enum : uint32_t {
        CTRL_START = 1,
        JOB_CTRL_START = 1,
        JOB_STATUS_IDLE = 0,
        JOB_STATUS_DONE = 1,
        JOB_STATUS_ERROR = 2,
        STATUS_IDLE = 0,
        STATUS_DONE = 1,
        STATUS_ERROR = 2,
        RESULT_OK = 0x444d414f,    // "DMAO"
        JOB_RESULT_OK = 0x434e4e4f, // "CNNO"
        JOB_RESULT_VADD_OK = 0x56414444,
        JOB_RESULT_CNN_OK = 0x434e4e53,   // "CNNS"
        JOB_RESULT_MNIST_OK = 0x4d4e4953, // "MNIS"
        RESULT_BAD_LEN = 0xbad00001,
        RESULT_TLM_ERROR = 0xbad00002,
        MAX_DMA_LEN = 256 * 1024,
        CAP_COPY_ENGINE = 1u << 0,
        CAP_DIRECT_TLM = 1u << 1,
        CAP_SMMU_TRANSLATED = 1u << 2,
        CAP_LARGE_TENSOR = 1u << 3,
        CAP_MULTI_QUEUE = 1u << 4,
        CAP_ASYNC_FENCE = 1u << 5,
        CAP_ENDPOINT_PASID = 1u << 6,
        QUEUE_COUNT = 2,
        CMDQ_FAULT_NONE = 0,
        CMDQ_FAULT_EMPTY = 1,
        CMDQ_FAULT_DISABLED = 2,
        CMDQ_FAULT_UNSUPPORTED_PACKET = 3,
        CMDQ_FAULT_MALFORMED_PACKET = 4,
        CMDQ_FAULT_DMA_ERROR = 5,
        CMDQ_PACKET_WORDS = 8,
        CMDQ_PACKET_BYTES = CMDQ_PACKET_WORDS * sizeof(uint32_t),
        CMDQ_OPCODE_NOP = 0,
        CMDQ_OPCODE_COPY = 1,
        CMDQ_OPCODE_BARRIER = 2,
        CMDQ_OPCODE_SIGNAL_FENCE = 3,
        CMDQ_OPCODE_DISPATCH = 4,
        CMDQ_OPCODE_LOAD_EXECUTABLE = 5,
        CMDQ_DISPATCH_EXEC_SLOT_FLAG = 1u << 31,
        CMDQ_EXEC_SLOT_COUNT = 16,
        APKO_MAGIC = 0x4f4b5041,
        APKO_ABI_VERSION = 0,
        EXEC_FORMAT_APKO_V0 = 1,
        CMDQ_DISPATCH_KIND_CNN = 1,
        CMDQ_DISPATCH_KIND_VADD = 2,
        CMDQ_DISPATCH_KIND_MNIST = 3,
        VADD_WORDS = 4,
        VADD_INPUT_WORDS = VADD_WORDS * 2,
        VADD_OUTPUT_WORDS = VADD_WORDS,
        PATH_DIRECT_TLM = 1,
        PATH_SMMU_TRANSLATED = 2,
        PASID_VALID = 1u << 31,
    };

    struct LoadedExecutable
    {
        bool valid = false;
        uint32_t executable_format = 0;
        uint32_t abi_version = 0;
        uint32_t entry_kind = 0;
        uint32_t input_bytes = 0;
        uint32_t output_bytes = 0;
    };

    uint32_t m_src = 0;
    uint32_t m_dst = 0;
    uint32_t m_len = 0;
    uint32_t m_status = STATUS_IDLE;
    uint32_t m_result = 0;
    uint32_t m_fw_done = 0;
    uint32_t m_job_input = 0;
    uint32_t m_job_output = 0;
    uint32_t m_job_input_bytes = 0;
    uint32_t m_job_output_bytes = 0;
    uint32_t m_job_ctrl = 0;
    uint32_t m_job_status = JOB_STATUS_IDLE;
    uint32_t m_job_result = 0;
    uint32_t m_job_queue = 0;
    uint32_t m_job_fence = 0;
    uint32_t m_irq_status = 0;
    uint32_t m_next_fence = 1;
    uint32_t m_cmdq_base_lo = 0;
    uint32_t m_cmdq_base_hi = 0;
    uint32_t m_cmdq_size = 0;
    uint32_t m_cmdq_head = 0;
    uint32_t m_cmdq_tail = 0;
    uint32_t m_cmdq_status = JOB_STATUS_IDLE;
    uint32_t m_cmdq_fence_value = 0;
	    uint32_t m_cmdq_fault_code = CMDQ_FAULT_NONE;
	    uint32_t m_cmdq_fault_addr_lo = 0;
	    uint32_t m_cmdq_fault_addr_hi = 0;
	    std::array<LoadedExecutable, CMDQ_EXEC_SLOT_COUNT> m_loaded_executables {};

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        uint32_t value = 0;
        const uint64_t addr = trans.get_address();
        const unsigned int len = trans.get_data_length();

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        trans.set_dmi_allowed(false);

        if (len != sizeof(uint32_t)) {
            trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }

        if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            std::memcpy(&value, trans.get_data_ptr(), sizeof(value));
            write_reg(addr, value, delay);
        } else if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            value = read_reg(addr);
            std::memcpy(trans.get_data_ptr(), &value, sizeof(value));
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        }
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        b_transport(trans, delay);
        return trans.is_response_ok() ? trans.get_data_length() : 0;
    }

    uint32_t read_reg(uint64_t addr) const
    {
        switch (addr) {
        case REG_SRC:
            return m_src;
        case REG_DST:
            return m_dst;
        case REG_LEN:
            return m_len;
        case REG_STATUS:
            return m_status;
        case REG_RESULT:
            return m_result;
        case REG_FW_DONE:
            return m_fw_done;
        case REG_CAPS:
            return dma_caps();
        case REG_PATH:
            return p_smmu_translated.get_value() ? PATH_SMMU_TRANSLATED : PATH_DIRECT_TLM;
        case REG_STREAM_ID:
            return p_stream_id.get_value();
        case REG_JOB_INPUT:
            return m_job_input;
        case REG_JOB_OUTPUT:
            return m_job_output;
        case REG_JOB_INPUT_BYTES:
            return m_job_input_bytes;
        case REG_JOB_OUTPUT_BYTES:
            return m_job_output_bytes;
        case REG_JOB_CTRL:
            return m_job_ctrl;
        case REG_JOB_STATUS:
            return m_job_status;
        case REG_JOB_RESULT:
            return m_job_result;
        case REG_JOB_QUEUE:
            return m_job_queue;
        case REG_JOB_FENCE:
            return m_job_fence;
        case REG_IRQ_STATUS:
            return m_irq_status;
        case REG_QUEUE_CAPS:
            return QUEUE_COUNT;
        case REG_PASID:
            return (p_substream_id.get_value() & 0x000fffffu) |
                   (p_substream_id_valid.get_value() ? PASID_VALID : 0u);
        case REG_CMDQ_BASE_LO:
            return m_cmdq_base_lo;
        case REG_CMDQ_BASE_HI:
            return m_cmdq_base_hi;
        case REG_CMDQ_SIZE:
            return m_cmdq_size;
        case REG_CMDQ_HEAD:
            return m_cmdq_head;
        case REG_CMDQ_TAIL:
            return m_cmdq_tail;
        case REG_CMDQ_STATUS:
            return m_cmdq_status;
        case REG_CMDQ_FENCE_VALUE:
            return m_cmdq_fence_value;
        case REG_CMDQ_FAULT_CODE:
            return m_cmdq_fault_code;
        case REG_CMDQ_FAULT_ADDR_LO:
            return m_cmdq_fault_addr_lo;
        case REG_CMDQ_FAULT_ADDR_HI:
            return m_cmdq_fault_addr_hi;
        default:
            return 0;
        }
    }

    uint32_t dma_caps() const
    {
        uint32_t caps = CAP_COPY_ENGINE | CAP_LARGE_TENSOR | CAP_MULTI_QUEUE |
                        CAP_ASYNC_FENCE | CAP_ENDPOINT_PASID;

        if (p_smmu_translated.get_value()) {
            caps |= CAP_SMMU_TRANSLATED;
        } else {
            caps |= CAP_DIRECT_TLM;
        }

        return caps;
    }

    void write_reg(uint64_t addr, uint32_t value, sc_core::sc_time& delay)
    {
        switch (addr) {
        case REG_SRC:
            m_src = value;
            break;
        case REG_DST:
            m_dst = value;
            break;
        case REG_LEN:
            m_len = value;
            break;
        case REG_CTRL:
            if (value & CTRL_START) {
                run_dma(delay);
            }
            break;
        case REG_FW_DONE:
            m_fw_done = value;
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: firmware done magic=0x" << std::hex << m_fw_done;
            std::cerr << "APOLLO_HEXAGON_DMA: firmware done magic=0x" << std::hex << m_fw_done << std::dec
                      << std::endl;
            break;
        case REG_JOB_INPUT:
            m_job_input = value;
            break;
        case REG_JOB_OUTPUT:
            m_job_output = value;
            break;
        case REG_JOB_INPUT_BYTES:
            m_job_input_bytes = value;
            break;
        case REG_JOB_OUTPUT_BYTES:
            m_job_output_bytes = value;
            break;
        case REG_JOB_CTRL:
            m_job_ctrl = value;
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: job ctrl=0x" << std::hex << m_job_ctrl << " input=0x"
                         << m_job_input << " output=0x" << m_job_output << " input-bytes=0x"
                         << m_job_input_bytes << " output-bytes=0x" << m_job_output_bytes << " queue="
                         << std::dec << m_job_queue;
            std::cerr << "APOLLO_HEXAGON_DMA: job ctrl=0x" << std::hex << m_job_ctrl << " input=0x"
                      << m_job_input << " output=0x" << m_job_output << " input-bytes=0x" << m_job_input_bytes
                      << " output-bytes=0x" << m_job_output_bytes << " queue=" << std::dec << m_job_queue
                      << std::endl;
            break;
        case REG_JOB_STATUS:
            m_job_status = value;
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: job status=0x" << std::hex << m_job_status;
            std::cerr << "APOLLO_HEXAGON_DMA: job status=0x" << std::hex << m_job_status << std::dec << std::endl;
            if (m_job_status == JOB_STATUS_DONE || m_job_status == JOB_STATUS_ERROR) {
                complete_async_event();
            }
            break;
        case REG_JOB_RESULT:
            m_job_result = value;
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: job result=0x" << std::hex << m_job_result;
            std::cerr << "APOLLO_HEXAGON_DMA: job result=0x" << std::hex << m_job_result << std::dec << std::endl;
            break;
        case REG_JOB_QUEUE:
            m_job_queue = value % QUEUE_COUNT;
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: job queue=" << std::dec << m_job_queue;
            std::cerr << "APOLLO_HEXAGON_DMA: job queue=" << std::dec << m_job_queue << std::endl;
            break;
        case REG_IRQ_ACK:
            m_irq_status &= ~value;
            update_irq_output();
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: async irq ack mask=0x" << std::hex << value
                         << " pending=0x" << m_irq_status;
            std::cerr << "APOLLO_HEXAGON_DMA: async irq ack mask=0x" << std::hex << value << " pending=0x"
                      << m_irq_status << std::dec << std::endl;
            break;
        case REG_PASID:
            p_substream_id.set_value(value & 0x000fffffu);
            p_substream_id_valid.set_value((value & PASID_VALID) != 0);
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: endpoint PASID update valid="
                         << (p_substream_id_valid.get_value() ? 1 : 0)
                         << " pasid=0x" << std::hex << p_substream_id.get_value();
            std::cerr << "APOLLO_HEXAGON_DMA: endpoint PASID update valid="
                      << (p_substream_id_valid.get_value() ? 1 : 0)
                      << " pasid=0x" << std::hex << p_substream_id.get_value()
                      << std::dec << std::endl;
            break;
        case REG_CMDQ_BASE_LO:
            m_cmdq_base_lo = value;
            break;
        case REG_CMDQ_BASE_HI:
            m_cmdq_base_hi = value;
            break;
        case REG_CMDQ_SIZE:
            m_cmdq_size = value;
            break;
        case REG_CMDQ_HEAD:
            m_cmdq_head = value;
            break;
        case REG_CMDQ_TAIL:
            m_cmdq_tail = value;
            break;
        case REG_CMDQ_DOORBELL:
            run_command_queue(value);
            break;
        default:
            SCP_WARN(()) << "APOLLO_HEXAGON_DMA: ignored write offset=0x" << std::hex << addr << " value=0x" << value;
            break;
        }
    }

    void update_irq_output()
    {
        if (irq_out.size() > 0) {
            irq_out->write(m_irq_status != 0);
        }
    }

    uint32_t complete_async_event()
    {
        const uint32_t irq_bit = 1u << (m_job_queue % QUEUE_COUNT);

        m_job_fence = m_next_fence++;
        m_irq_status |= irq_bit;
        update_irq_output();
        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: async irq pending queue=" << std::dec << m_job_queue
                     << " fence=" << m_job_fence << " irq=0x" << std::hex << m_irq_status;
        std::cerr << "APOLLO_HEXAGON_DMA: async irq pending queue=" << std::dec << m_job_queue
                  << " fence=" << m_job_fence << " irq=0x" << std::hex << m_irq_status << std::dec
                  << std::endl;
        return m_job_fence;
    }

    uint64_t command_queue_base() const
    {
        return (static_cast<uint64_t>(m_cmdq_base_hi) << 32) | m_cmdq_base_lo;
    }

    static bool command_queue_addr(uint64_t base, uint32_t offset, uint64_t& addr)
    {
        if (base > std::numeric_limits<uint64_t>::max() - offset) {
            return false;
        }

        addr = base + offset;
        return true;
    }

    void set_command_queue_fault(uint32_t code, uint64_t fault_addr)
    {
        m_cmdq_status = JOB_STATUS_ERROR;
        m_cmdq_fault_code = code;
        m_cmdq_fault_addr_lo = static_cast<uint32_t>(fault_addr & 0xffffffffu);
        m_cmdq_fault_addr_hi = static_cast<uint32_t>(fault_addr >> 32);
        m_cmdq_fence_value = complete_async_event();
    }

    void complete_command_queue()
    {
        m_cmdq_status = JOB_STATUS_DONE;
        m_cmdq_fault_code = CMDQ_FAULT_NONE;
        m_cmdq_fault_addr_lo = 0;
        m_cmdq_fault_addr_hi = 0;
        m_cmdq_fence_value = complete_async_event();
        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command queue complete head=0x" << std::hex << m_cmdq_head
                     << " tail=0x" << m_cmdq_tail << " fence=" << std::dec << m_cmdq_fence_value;
        std::cerr << "APOLLO_HEXAGON_DMA: command queue complete head=0x" << std::hex << m_cmdq_head
                  << " tail=0x" << m_cmdq_tail << " fence=" << std::dec << m_cmdq_fence_value << std::endl;
    }

    bool read_command_packet(uint64_t packet_addr, std::array<uint32_t, CMDQ_PACKET_WORDS>& packet)
    {
        std::array<uint8_t, CMDQ_PACKET_BYTES> raw {};
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        if (!do_dma(tlm::TLM_READ_COMMAND, packet_addr, raw.data(), raw.size(), delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, packet_addr);
            return false;
        }

        for (size_t i = 0; i < packet.size(); i++) {
            std::memcpy(&packet[i], &raw[i * sizeof(uint32_t)], sizeof(uint32_t));
        }
        return true;
    }

    static uint64_t packet_u64(const std::array<uint32_t, CMDQ_PACKET_WORDS>& packet, size_t lo_index)
    {
        return static_cast<uint64_t>(packet[lo_index]) |
               (static_cast<uint64_t>(packet[lo_index + 1]) << 32);
    }

    static bool packet_has_only_opcode(const std::array<uint32_t, CMDQ_PACKET_WORDS>& packet)
    {
        for (size_t i = 1; i < packet.size(); i++) {
            if (packet[i] != 0) {
                return false;
            }
        }
        return true;
    }

    static uint32_t float_to_word(float value)
    {
        uint32_t word;

        std::memcpy(&word, &value, sizeof(word));
        return word;
    }

    bool execute_copy_packet(const std::array<uint32_t, CMDQ_PACKET_WORDS>& packet, uint64_t packet_addr)
    {
        const uint64_t src = packet_u64(packet, 2);
        const uint64_t dst = packet_u64(packet, 4);
        const uint32_t len = packet[6];
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        if (packet[1] != 0 || packet[7] != 0 || len == 0 || len > MAX_DMA_LEN) {
            set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
            return false;
        }

        std::vector<uint8_t> buffer(len);
        if (!do_dma(tlm::TLM_READ_COMMAND, src, buffer.data(), len, delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, src);
            return false;
        }
        if (!do_dma(tlm::TLM_WRITE_COMMAND, dst, buffer.data(), len, delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, dst);
            return false;
        }

        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command copy src=0x" << std::hex << src << " dst=0x"
                     << dst << " len=0x" << len;
        std::cerr << "APOLLO_HEXAGON_DMA: command copy src=0x" << std::hex << src << " dst=0x"
                  << dst << " len=0x" << len << std::dec << std::endl;
        return true;
    }

    bool execute_vadd_dispatch_packet(uint64_t input_addr, uint64_t output_addr, uint32_t input_bytes,
                                      uint32_t output_bytes, uint64_t packet_addr)
    {
        std::array<uint32_t, VADD_INPUT_WORDS> input {};
        std::array<uint32_t, VADD_OUTPUT_WORDS> output {};
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        if (input_bytes != input.size() * sizeof(uint32_t) ||
            output_bytes != output.size() * sizeof(uint32_t)) {
            set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
            return false;
        }

        if (!do_dma(tlm::TLM_READ_COMMAND, input_addr, reinterpret_cast<uint8_t*>(input.data()),
                    input_bytes, delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, input_addr);
            return false;
        }

        for (size_t i = 0; i < output.size(); i++) {
            const uint32_t sum = input[i] + input[i + VADD_WORDS];

            output[i] = float_to_word(static_cast<float>(sum));
        }

        if (!do_dma(tlm::TLM_WRITE_COMMAND, output_addr, reinterpret_cast<uint8_t*>(output.data()),
                    output_bytes, delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, output_addr);
            return false;
        }

        m_job_status = JOB_STATUS_DONE;
        m_job_result = JOB_RESULT_VADD_OK;
        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command dispatch vadd input=0x" << std::hex << input_addr
                     << " output=0x" << output_addr << " bytes=0x" << input_bytes;
        std::cerr << "APOLLO_HEXAGON_DMA: command dispatch vadd input=0x" << std::hex << input_addr
                  << " output=0x" << output_addr << " bytes=0x" << input_bytes << std::dec
                  << std::endl;
        return true;
    }

    bool execute_cnn_dispatch_packet(uint64_t input_addr, uint64_t output_addr, uint32_t input_bytes,
                                     uint32_t output_bytes, uint64_t packet_addr)
    {
        std::vector<uint8_t> input(input_bytes);
        std::vector<uint8_t> output(output_bytes);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        if (input_bytes == 0 || output_bytes == 0 || input_bytes > MAX_DMA_LEN || output_bytes > MAX_DMA_LEN) {
            set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
            return false;
        }

        if (!do_dma(tlm::TLM_READ_COMMAND, input_addr, input.data(), input_bytes, delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, input_addr);
            return false;
        }

        const size_t bytes_to_copy = std::min(input.size(), output.size());
        std::copy(input.begin(), input.begin() + bytes_to_copy, output.begin());

        if (!do_dma(tlm::TLM_WRITE_COMMAND, output_addr, output.data(), output_bytes, delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, output_addr);
            return false;
        }

        m_job_status = JOB_STATUS_DONE;
        m_job_result = JOB_RESULT_CNN_OK;
        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command dispatch cnn input=0x" << std::hex << input_addr
                     << " output=0x" << output_addr << " bytes=0x" << input_bytes;
        std::cerr << "APOLLO_HEXAGON_DMA: command dispatch cnn input=0x" << std::hex << input_addr
                  << " output=0x" << output_addr << " bytes=0x" << input_bytes << std::dec
                  << std::endl;
        return true;
    }

    bool execute_mnist_dispatch_packet(uint64_t input_addr, uint64_t output_addr, uint32_t input_bytes,
                                       uint32_t output_bytes, uint64_t packet_addr)
    {
        std::vector<uint8_t> input(input_bytes);
        std::vector<uint8_t> output(output_bytes);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        if (input_bytes == 0 || output_bytes == 0 || input_bytes > MAX_DMA_LEN || output_bytes > MAX_DMA_LEN) {
            set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
            return false;
        }

        if (!do_dma(tlm::TLM_READ_COMMAND, input_addr, input.data(), input_bytes, delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, input_addr);
            return false;
        }

        for (size_t i = 0; i < output.size(); ++i) {
            output[i] = static_cast<uint8_t>(~input[i % input.size()]);
        }

        if (!do_dma(tlm::TLM_WRITE_COMMAND, output_addr, output.data(), output_bytes, delay, false)) {
            set_command_queue_fault(CMDQ_FAULT_DMA_ERROR, output_addr);
            return false;
        }

        m_job_status = JOB_STATUS_DONE;
        m_job_result = JOB_RESULT_MNIST_OK;
        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command dispatch mnist input=0x" << std::hex << input_addr
                     << " output=0x" << output_addr << " bytes=0x" << input_bytes;
        std::cerr << "APOLLO_HEXAGON_DMA: command dispatch mnist input=0x" << std::hex << input_addr
                  << " output=0x" << output_addr << " bytes=0x" << input_bytes << std::dec
                  << std::endl;
        return true;
    }

    bool execute_load_executable_packet(const std::array<uint32_t, CMDQ_PACKET_WORDS>& packet,
                                        uint64_t packet_addr)
    {
        const uint32_t slot = packet[1];
        const uint32_t magic = packet[2];
        const uint32_t abi_version = packet[3];
        const uint32_t executable_format = packet[4];
        const uint32_t entry_kind = packet[5];
        const uint32_t input_bytes = packet[6];
        const uint32_t output_bytes = packet[7];
        const bool entry_kind_supported =
            (entry_kind == CMDQ_DISPATCH_KIND_VADD || entry_kind == CMDQ_DISPATCH_KIND_CNN ||
             entry_kind == CMDQ_DISPATCH_KIND_MNIST);

        if (slot == 0 || slot >= m_loaded_executables.size() ||
            magic != APKO_MAGIC ||
            abi_version != APKO_ABI_VERSION ||
            executable_format != EXEC_FORMAT_APKO_V0 ||
            !entry_kind_supported ||
            input_bytes == 0 ||
            output_bytes == 0 ||
            input_bytes > MAX_DMA_LEN ||
            output_bytes > MAX_DMA_LEN) {
            set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
            return false;
        }

        m_loaded_executables[slot] = {
            true,
            executable_format,
            abi_version,
            entry_kind,
            input_bytes,
            output_bytes,
        };
        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command load executable slot=" << std::dec << slot
                     << " kind=" << entry_kind << " input-bytes=" << input_bytes
                     << " output-bytes=" << output_bytes << " format=0x" << std::hex
                     << executable_format;
        std::cerr << "APOLLO_HEXAGON_DMA: command load executable slot=" << std::dec << slot
                  << " kind=" << entry_kind << " input-bytes=" << input_bytes
                  << " output-bytes=" << output_bytes << " format=0x" << std::hex
                  << executable_format << std::dec << std::endl;
        return true;
    }

    bool execute_loaded_dispatch_packet(const std::array<uint32_t, CMDQ_PACKET_WORDS>& packet,
                                        uint64_t packet_addr)
    {
        const uint32_t slot = packet[1] & ~CMDQ_DISPATCH_EXEC_SLOT_FLAG;
        const uint64_t input_addr = packet_u64(packet, 2);
        const uint64_t output_addr = packet_u64(packet, 4);
        const uint32_t input_bytes = packet[6];
        const uint32_t output_bytes = packet[7];
        const LoadedExecutable& executable =
            slot < m_loaded_executables.size() ? m_loaded_executables[slot] : m_loaded_executables[0];

        if (slot == 0 || slot >= m_loaded_executables.size() ||
            !executable.valid ||
            input_bytes != executable.input_bytes ||
            output_bytes != executable.output_bytes) {
            set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
            return false;
        }

        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command dispatch executable slot=" << std::dec << slot
                     << " kind=" << executable.entry_kind;
        std::cerr << "APOLLO_HEXAGON_DMA: command dispatch executable slot=" << std::dec << slot
                  << " kind=" << executable.entry_kind << std::endl;

        switch (executable.entry_kind) {
        case CMDQ_DISPATCH_KIND_VADD:
            return execute_vadd_dispatch_packet(input_addr, output_addr, input_bytes, output_bytes,
                                                packet_addr);
        case CMDQ_DISPATCH_KIND_CNN:
            return execute_cnn_dispatch_packet(input_addr, output_addr, input_bytes, output_bytes,
                                              packet_addr);
        case CMDQ_DISPATCH_KIND_MNIST:
            return execute_mnist_dispatch_packet(input_addr, output_addr, input_bytes, output_bytes,
                                                packet_addr);
        default:
            set_command_queue_fault(CMDQ_FAULT_UNSUPPORTED_PACKET, packet_addr);
            return false;
        }
    }

    bool execute_dispatch_packet(const std::array<uint32_t, CMDQ_PACKET_WORDS>& packet, uint64_t packet_addr)
    {
        const uint32_t dispatch_kind = packet[1];
        const uint64_t input_addr = packet_u64(packet, 2);
        const uint64_t output_addr = packet_u64(packet, 4);
        const uint32_t input_bytes = packet[6];
        const uint32_t output_bytes = packet[7];

        if (dispatch_kind & CMDQ_DISPATCH_EXEC_SLOT_FLAG) {
            return execute_loaded_dispatch_packet(packet, packet_addr);
        }

        switch (dispatch_kind) {
        case CMDQ_DISPATCH_KIND_VADD:
            return execute_vadd_dispatch_packet(input_addr, output_addr, input_bytes, output_bytes,
                                                packet_addr);
        case CMDQ_DISPATCH_KIND_CNN:
            return execute_cnn_dispatch_packet(input_addr, output_addr, input_bytes, output_bytes,
                                                packet_addr);
        case CMDQ_DISPATCH_KIND_MNIST:
            return execute_mnist_dispatch_packet(input_addr, output_addr, input_bytes, output_bytes,
                                                packet_addr);
        default:
            set_command_queue_fault(CMDQ_FAULT_UNSUPPORTED_PACKET, packet_addr);
            return false;
        }
    }

    bool execute_command_packet(const std::array<uint32_t, CMDQ_PACKET_WORDS>& packet, uint64_t packet_addr)
    {
        const uint32_t opcode = packet[0];

        switch (opcode) {
        case CMDQ_OPCODE_NOP:
            if (!packet_has_only_opcode(packet)) {
                set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
                return false;
            }
            return true;
        case CMDQ_OPCODE_COPY:
            return execute_copy_packet(packet, packet_addr);
        case CMDQ_OPCODE_BARRIER:
            if (!packet_has_only_opcode(packet)) {
                set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
                return false;
            }
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command barrier";
            std::cerr << "APOLLO_HEXAGON_DMA: command barrier" << std::endl;
            return true;
        case CMDQ_OPCODE_SIGNAL_FENCE:
            if (!packet_has_only_opcode(packet)) {
                set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, packet_addr);
                return false;
            }
            SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command signal fence";
            std::cerr << "APOLLO_HEXAGON_DMA: command signal fence" << std::endl;
            return true;
        case CMDQ_OPCODE_DISPATCH:
            return execute_dispatch_packet(packet, packet_addr);
        case CMDQ_OPCODE_LOAD_EXECUTABLE:
            return execute_load_executable_packet(packet, packet_addr);
        default:
            set_command_queue_fault(CMDQ_FAULT_UNSUPPORTED_PACKET, packet_addr);
            return false;
        }
    }

    void run_command_queue(uint32_t doorbell)
    {
        const uint64_t base = command_queue_base();
        uint32_t offset;
        uint64_t fault_addr = base;
        bool fault_addr_valid;

        if ((doorbell & JOB_CTRL_START) == 0) {
            return;
        }

        m_cmdq_status = JOB_STATUS_IDLE;
        m_cmdq_fault_code = CMDQ_FAULT_NONE;
        m_cmdq_fault_addr_lo = 0;
        m_cmdq_fault_addr_hi = 0;

        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: command queue doorbell base=0x" << std::hex << base
                     << " size=0x" << m_cmdq_size << " head=0x" << m_cmdq_head << " tail=0x"
                     << m_cmdq_tail << " queue=" << std::dec << m_job_queue;
        std::cerr << "APOLLO_HEXAGON_DMA: command queue doorbell base=0x" << std::hex << base
                  << " size=0x" << m_cmdq_size << " head=0x" << m_cmdq_head << " tail=0x"
                  << m_cmdq_tail << " queue=" << std::dec << m_job_queue << std::endl;

        fault_addr_valid = command_queue_addr(base, m_cmdq_head, fault_addr);
        if (m_cmdq_size == 0) {
            set_command_queue_fault(CMDQ_FAULT_DISABLED, base);
        } else if ((m_cmdq_head % CMDQ_PACKET_BYTES) != 0 ||
                   (m_cmdq_tail % CMDQ_PACKET_BYTES) != 0 ||
                   m_cmdq_head > m_cmdq_tail ||
                   m_cmdq_tail > m_cmdq_size ||
                   !fault_addr_valid) {
            set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, fault_addr);
        } else if (m_cmdq_head == m_cmdq_tail) {
            set_command_queue_fault(CMDQ_FAULT_EMPTY, fault_addr);
        } else {
            for (offset = m_cmdq_head; offset < m_cmdq_tail; offset += CMDQ_PACKET_BYTES) {
                std::array<uint32_t, CMDQ_PACKET_WORDS> packet {};
                uint64_t packet_addr = base;

                if (!command_queue_addr(base, offset, packet_addr)) {
                    set_command_queue_fault(CMDQ_FAULT_MALFORMED_PACKET, base);
                    m_cmdq_head = offset;
                    break;
                }

                if (!read_command_packet(packet_addr, packet) ||
                    !execute_command_packet(packet, packet_addr)) {
                    m_cmdq_head = offset;
                    break;
                }
                m_cmdq_head = offset + CMDQ_PACKET_BYTES;
            }
            if (m_cmdq_status != JOB_STATUS_ERROR) {
                complete_command_queue();
            }
        }

        if (m_cmdq_status == JOB_STATUS_ERROR) {
            SCP_WARN(()) << "APOLLO_HEXAGON_DMA: command queue fault code=" << std::dec << m_cmdq_fault_code
                         << " addr=0x" << std::hex
                         << ((static_cast<uint64_t>(m_cmdq_fault_addr_hi) << 32) | m_cmdq_fault_addr_lo);
            std::cerr << "APOLLO_HEXAGON_DMA: command queue fault code=" << std::dec << m_cmdq_fault_code
                      << " addr=0x" << std::hex
                      << ((static_cast<uint64_t>(m_cmdq_fault_addr_hi) << 32) | m_cmdq_fault_addr_lo)
                      << " fence=" << std::dec << m_cmdq_fence_value << std::endl;
        }
    }

    void run_dma(sc_core::sc_time& delay)
    {
        m_status = STATUS_IDLE;
        m_result = 0;

        if (m_len == 0 || m_len > MAX_DMA_LEN) {
            m_status = STATUS_ERROR;
            m_result = RESULT_BAD_LEN;
            SCP_ERR(()) << "APOLLO_HEXAGON_DMA: invalid length " << std::dec << m_len;
            std::cerr << "APOLLO_HEXAGON_DMA: invalid length " << std::dec << m_len << std::endl;
            return;
        }

        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: firmware requested DMA src=0x" << std::hex << m_src << " dst=0x"
                     << m_dst << " len=0x" << m_len;
        std::cerr << "APOLLO_HEXAGON_DMA: firmware requested DMA src=0x" << std::hex << m_src << " dst=0x" << m_dst
                  << " len=0x" << m_len << std::dec << std::endl;
        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: path="
                     << (p_smmu_translated.get_value() ? "smmu-translated" : "direct-tlm")
                     << " stream-id=0x" << std::hex << p_stream_id.get_value()
                     << " pasid-valid=" << (p_substream_id_valid.get_value() ? 1 : 0)
                     << " pasid=0x" << p_substream_id.get_value()
                     << " caps=0x" << dma_caps();
        std::cerr << "APOLLO_HEXAGON_DMA: path="
                  << (p_smmu_translated.get_value() ? "smmu-translated" : "direct-tlm") << " stream-id=0x"
                  << std::hex << p_stream_id.get_value()
                  << " pasid-valid=" << (p_substream_id_valid.get_value() ? 1 : 0)
                  << " pasid=0x" << p_substream_id.get_value()
                  << " caps=0x" << dma_caps() << std::dec << std::endl;

        std::vector<uint8_t> buffer(m_len);
        if (!do_dma(tlm::TLM_READ_COMMAND, m_src, buffer.data(), m_len, delay)) {
            return;
        }
        if (!do_dma(tlm::TLM_WRITE_COMMAND, m_dst, buffer.data(), m_len, delay)) {
            return;
        }

        uint32_t first_word = 0;
        std::memcpy(&first_word, buffer.data(), std::min(sizeof(first_word), buffer.size()));
        m_result = RESULT_OK;
        m_status = STATUS_DONE;

        SCP_INFO(()) << "APOLLO_HEXAGON_DMA: DMA copy complete src=0x" << std::hex << m_src << " dst=0x" << m_dst
                     << " len=0x" << m_len << " first=0x" << first_word;
        std::cerr << "APOLLO_HEXAGON_DMA: DMA copy complete src=0x" << std::hex << m_src << " dst=0x" << m_dst
                  << " len=0x" << m_len << " first=0x" << first_word << std::dec << std::endl;
    }

    bool do_dma(tlm::tlm_command command, uint64_t addr, uint8_t* data, uint32_t len, sc_core::sc_time& delay,
                bool update_copy_status = true)
    {
        tlm::tlm_generic_payload trans;
        trans.set_command(command);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        if (p_smmu_translated.get_value()) {
            gs::ApolloSmmuStreamIdExtension stream_id_ext(
                p_stream_id.get_value(), p_substream_id.get_value(),
                p_substream_id_valid.get_value());
            trans.set_extension(&stream_id_ext);
            translated_dma->b_transport(trans, delay);
            trans.clear_extension(&stream_id_ext);
        } else {
            dma->b_transport(trans, delay);
        }
        if (!trans.is_response_ok()) {
            if (update_copy_status) {
                m_status = STATUS_ERROR;
                m_result = RESULT_TLM_ERROR;
                SCP_ERR(()) << "APOLLO_HEXAGON_DMA: DMA transaction failed command="
                             << (command == tlm::TLM_READ_COMMAND ? "read" : "write") << " addr=0x" << std::hex
                             << addr << " response=" << trans.get_response_string();
            } else {
                SCP_WARN(()) << "APOLLO_HEXAGON_DMA: command DMA transaction failed command="
                             << (command == tlm::TLM_READ_COMMAND ? "read" : "write") << " addr=0x" << std::hex
                             << addr << " response=" << trans.get_response_string();
            }
            std::cerr << "APOLLO_HEXAGON_DMA: DMA transaction failed command="
                      << (command == tlm::TLM_READ_COMMAND ? "read" : "write") << " addr=0x" << std::hex << addr
                      << " response=" << trans.get_response_string() << std::dec << std::endl;
            return false;
        }

        return true;
    }
};

extern "C" void module_register();
