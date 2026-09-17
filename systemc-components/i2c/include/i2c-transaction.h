/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef QBOX_I2C_TRANSACTION_H
#define QBOX_I2C_TRANSACTION_H

#include <tlm>

struct dw_i2c_extension : tlm::tlm_extension<dw_i2c_extension> {
    // Control events carry no data and never modify device registers.
    enum class event { data, discover, start, restart, address, read_ack, stop, cancel };
    event phase = event::data;
    bool ack = true;
    bool restart = false;
    bool stop = false;

    tlm_extension_base* clone() const override { return new dw_i2c_extension(*this); }
    void copy_from(const tlm_extension_base& ext) override { *this = static_cast<const dw_i2c_extension&>(ext); }
};

#endif
