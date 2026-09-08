/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QBOX_DW_APB_SSI_H
#define QBOX_DW_APB_SSI_H

#include <cstdint>
#include <deque>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <scp/report.h>
#include <tlm_sockets_buswidth.h>

class dw_apb_ssi : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    enum Register : uint32_t {
        CTRLR0 = 0x00,
        CTRLR1 = 0x04,
        SSIENR = 0x08,
        MWCR = 0x0c,
        SER = 0x10,
        BAUDR = 0x14,
        TXFTLR = 0x18,
        RXFTLR = 0x1c,
        TXFLR = 0x20,
        RXFLR = 0x24,
        SR = 0x28,
        IMR = 0x2c,
        ISR = 0x30,
        RISR = 0x34,
        TXOICR = 0x38,
        RXOICR = 0x3c,
        RXUICR = 0x40,
        MSTICR = 0x44,
        ICR = 0x48,
        DMACR = 0x4c,
        DMATDLR = 0x50,
        DMARDLR = 0x54,
        IDR = 0x58,
        VERSION = 0x5c,
        DR = 0x60,
        RX_SAMPLE_DLY = 0xf0,
    };

    enum Status : uint32_t {
        SR_BUSY = 1u << 0,
        SR_TF_NOT_FULL = 1u << 1,
        SR_TF_EMPTY = 1u << 2,
        SR_RF_NOT_EMPTY = 1u << 3,
        SR_RF_FULL = 1u << 4,
    };

    enum Interrupt : uint32_t {
        INT_TXEI = 1u << 0,
        INT_TXOI = 1u << 1,
        INT_RXUI = 1u << 2,
        INT_RXOI = 1u << 3,
        INT_RXFI = 1u << 4,
        INT_MSTI = 1u << 5,
        INT_MASK = 0x3f,
    };

    static constexpr uint32_t DEFAULT_FIFO_DEPTH = 16;
    static constexpr uint32_t COMPONENT_ID = 0x51535049;
    static constexpr uint32_t COMPONENT_VERSION = 0x3430352a;

    tlm_utils::simple_target_socket<dw_apb_ssi, DEFAULT_TLM_BUSWIDTH> target_socket;
    InitiatorSignalSocket<bool> irq;
    TargetSignalSocket<bool> reset;
    TargetSignalSocket<bool> pinmux_enable;

    cci::cci_param<uint64_t> p_clock_frequency_hz;
    cci::cci_param<uint32_t> p_fifo_depth;
    cci::cci_param<uint32_t> p_num_chip_selects;
    cci::cci_param<uint64_t> p_access_latency_ns;

    SC_HAS_PROCESS(dw_apb_ssi);
    explicit dw_apb_ssi(sc_core::sc_module_name name);

private:
    static constexpr uint32_t CTRLR0_MASK = 0x00001fff;

    uint32_t m_ctrlr0;
    uint32_t m_ctrlr1;
    uint32_t m_ssienr;
    uint32_t m_mwcr;
    uint32_t m_ser;
    uint32_t m_baudr;
    uint32_t m_txftlr;
    uint32_t m_rxftlr;
    uint32_t m_imr;
    uint32_t m_sticky_interrupts;
    uint32_t m_dmatdlr;
    uint32_t m_dmardlr;
    uint32_t m_rx_sample_dly;
    bool m_busy;
    bool m_irq_state;
    bool m_pinmux_enabled = true;

    std::deque<uint32_t> m_tx_fifo;
    std::deque<uint32_t> m_rx_fifo;
    sc_core::sc_event m_transfer_event;
    sc_core::sc_event m_cancel_event;
    sc_core::sc_event m_irq_event;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    uint32_t read_register(uint32_t offset);
    void write_register(uint32_t offset, uint32_t value);
    uint32_t raw_interrupt_status() const;
    uint32_t status() const;
    uint32_t data_mask() const;
    uint32_t fifo_depth() const;
    uint32_t chip_select_mask() const;
    uint32_t writable_value(uint32_t offset) const;
    sc_core::sc_time frame_delay() const;
    void update_irq();
    void drive_irq();
    void reset_state();
    void transfer_thread();
};

extern "C" void module_register();

#endif
