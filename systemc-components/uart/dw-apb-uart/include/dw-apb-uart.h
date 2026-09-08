/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>
#include <deque>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

#include <module_factory_registery.h>
#include <ports/biflow-socket.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <scp/report.h>
#include <tlm_sockets_buswidth.h>

class dw_apb_uart : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    SC_HAS_PROCESS(dw_apb_uart);

    static constexpr uint32_t FIFO_DEPTH = 16;

    cci::cci_param<uint64_t> p_clock_frequency_hz;
    cci::cci_param<uint64_t> p_access_latency_ns;

    tlm_utils::simple_target_socket<dw_apb_uart, DEFAULT_TLM_BUSWIDTH> target_socket;
    InitiatorSignalSocket<bool> irq;
    gs::biflow_socket<dw_apb_uart> backend_socket;
    TargetSignalSocket<bool> reset;
    TargetSignalSocket<bool> pinmux_enable;

    explicit dw_apb_uart(sc_core::sc_module_name name);

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

private:
    enum Register : uint32_t {
        RBR_THR_DLL = 0x00,
        IER_DLH = 0x04,
        IIR_FCR = 0x08,
        LCR = 0x0c,
        MCR = 0x10,
        LSR = 0x14,
        MSR = 0x18,
        SCR = 0x1c,
        USR = 0x7c,
        TFL = 0x80,
        RFL = 0x84,
        SRR = 0x88,
        DLF = 0xc0,
        CPR = 0xf4,
        UCV = 0xf8,
        CTR = 0xfc,
    };

    enum : uint8_t {
        IER_RDI = 0x01,
        IER_THRI = 0x02,
        IER_RLSI = 0x04,
        IER_MSI = 0x08,

        IIR_MSI = 0x00,
        IIR_NO_INT = 0x01,
        IIR_THRI = 0x02,
        IIR_RDI = 0x04,
        IIR_RLSI = 0x06,
        IIR_BUSY = 0x07,
        IIR_RX_TIMEOUT = 0x0c,
        IIR_FIFO_ENABLED = 0xc0,

        FCR_ENABLE_FIFO = 0x01,
        FCR_CLEAR_RCVR = 0x02,
        FCR_CLEAR_XMIT = 0x04,

        LCR_DLAB = 0x80,

        MCR_LOOP = 0x10,

        LSR_DR = 0x01,
        LSR_OE = 0x02,
        LSR_THRE = 0x20,
        LSR_TEMT = 0x40,

        USR_BUSY = 0x01,
        USR_TFNF = 0x02,
        USR_TFE = 0x04,
        USR_RFNE = 0x08,
        USR_RFF = 0x10,
    };

    static constexpr uint32_t COMPONENT_PARAMETER = (1u << 16) | 2u;
    static constexpr uint32_t COMPONENT_VERSION = 0x3430352a;
    static constexpr uint32_t COMPONENT_TYPE = 0x44570110;

    std::deque<uint8_t> m_rx_fifo;
    std::deque<uint8_t> m_tx_fifo;
    uint8_t m_dll = 0;
    uint8_t m_dlh = 0;
    uint8_t m_ier = 0;
    uint8_t m_fcr = 0;
    uint8_t m_lcr = 0;
    uint8_t m_mcr = 0;
    uint8_t m_msr = 0xb0;
    uint8_t m_scr = 0;
    uint8_t m_lsr_errors = 0;
    bool m_thre_pending = true;
    bool m_rx_timeout_pending = false;
    bool m_busy_pending = false;
    bool m_reset_asserted = false;
    bool m_pinmux_enabled = true;
    uint64_t m_state_epoch = 0;
    uint64_t m_rx_epoch = 0;

    sc_core::sc_event m_tx_event;
    sc_core::sc_event m_rx_event;
    sc_core::sc_event m_state_event;
    sc_core::sc_event m_irq_event;
    sc_core::sc_event m_credit_event;
    sc_core::sc_signal<bool> m_irq_stub;

    void receive(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    void tx_thread();
    void rx_timeout_thread();
    void update_irq();
    void reset_registers();
    void reset_changed(bool asserted);
    void schedule_irq();
    void refresh_rx_credit();
    void refresh_rx_credit_event();
    void receive_byte(uint8_t data);
    void write_register(uint32_t offset, uint32_t value);
    uint32_t read_register(uint32_t offset);
    uint8_t interrupt_id(bool acknowledge_thre);
    unsigned int rx_trigger() const;
    unsigned int fifo_capacity() const;
    sc_core::sc_time character_time() const;
    bool busy() const;

    void end_of_elaboration() override;
    void before_end_of_elaboration() override;
};

extern "C" void module_register();
