#include <systemc>

#include <qemu_arm_arch_timer_mmio.h>

void module_register() { GSC_MODULE_REGISTER_C(qemu_arm_arch_timer_mmio, sc_core::sc_object*); }
