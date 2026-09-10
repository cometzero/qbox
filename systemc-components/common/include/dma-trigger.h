/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QBOX_DMA_TRIGGER_H
#define QBOX_DMA_TRIGGER_H

#include <cstdint>

#include <ports/initiator-signal-socket.h>

namespace gs {

class dma_trigger_request
{
public:
    static constexpr uint32_t ACTIVE = 1U << 2;
    static constexpr uint32_t TYPE_SINGLE = 0;

    explicit dma_trigger_request(InitiatorSignalSocket<uint32_t>& request): m_request(request) {}

    void acknowledge(uint32_t value) { m_acknowledge = value; }

    void force_idle()
    {
        m_asserted = false;
        m_wait_ack_low = (m_acknowledge & ACTIVE) != 0;
        m_request->write(0);
    }

    void update(bool needed)
    {
        const bool ack = (m_acknowledge & ACTIVE) != 0;
        if (m_asserted) {
            if (ack) {
                m_asserted = false;
                m_wait_ack_low = true;
                m_request->write(0);
            }
            return;
        }
        if (m_wait_ack_low) {
            if (ack) return;
            m_wait_ack_low = false;
        }
        if (needed && !ack) {
            m_asserted = true;
            m_request->write(ACTIVE | TYPE_SINGLE);
        }
    }

private:
    InitiatorSignalSocket<uint32_t>& m_request;
    uint32_t m_acknowledge = 0;
    bool m_asserted = false;
    bool m_wait_ack_low = false;
};

} // namespace gs

#endif
