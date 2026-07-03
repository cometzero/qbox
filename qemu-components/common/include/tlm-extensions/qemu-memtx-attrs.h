/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_TLM_EXTENSIONS_QEMU_MEMTX_ATTRS_H
#define _LIBQBOX_TLM_EXTENSIONS_QEMU_MEMTX_ATTRS_H

#include <tlm>

#include <libqemu-cxx/libqemu-cxx.h>

class QemuMemTxAttrsTlmExtension : public tlm::tlm_extension<QemuMemTxAttrsTlmExtension>
{
private:
    qemu::MemoryRegionOps::MemTxAttrs m_attrs;

public:
    QemuMemTxAttrsTlmExtension() = default;
    QemuMemTxAttrsTlmExtension(const QemuMemTxAttrsTlmExtension&) = default;
    explicit QemuMemTxAttrsTlmExtension(const qemu::MemoryRegionOps::MemTxAttrs& attrs): m_attrs(attrs) {}

    virtual tlm_extension_base* clone() const override { return new QemuMemTxAttrsTlmExtension(*this); }

    virtual void copy_from(tlm_extension_base const& ext) override
    {
        m_attrs = static_cast<const QemuMemTxAttrsTlmExtension&>(ext).m_attrs;
    }

    void set_attrs(const qemu::MemoryRegionOps::MemTxAttrs& attrs) { m_attrs = attrs; }
    qemu::MemoryRegionOps::MemTxAttrs get_attrs() const { return m_attrs; }
};

#endif
