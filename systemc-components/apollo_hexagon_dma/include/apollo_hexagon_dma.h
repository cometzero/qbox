/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
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
        PATH_DIRECT_TLM = 1,
        PATH_SMMU_TRANSLATED = 2,
        PASID_VALID = 1u << 31,
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

    void complete_async_event()
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

    bool do_dma(tlm::tlm_command command, uint64_t addr, uint8_t* data, uint32_t len, sc_core::sc_time& delay)
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
            m_status = STATUS_ERROR;
            m_result = RESULT_TLM_ERROR;
            SCP_ERR(()) << "APOLLO_HEXAGON_DMA: DMA transaction failed command="
                         << (command == tlm::TLM_READ_COMMAND ? "read" : "write") << " addr=0x" << std::hex
                         << addr << " response=" << trans.get_response_string();
            std::cerr << "APOLLO_HEXAGON_DMA: DMA transaction failed command="
                      << (command == tlm::TLM_READ_COMMAND ? "read" : "write") << " addr=0x" << std::hex << addr
                      << " response=" << trans.get_response_string() << std::dec << std::endl;
            return false;
        }

        return true;
    }
};

extern "C" void module_register();
