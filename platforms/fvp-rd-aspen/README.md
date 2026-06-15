# QBox RD-Aspen Primary Compute

This platform boots the RD-Aspen primary-compute Linux image directly on QBox
using the local `tools/qemu` checkout as `libqemu`.

## Build

```bash
./scripts/build/build_qbox_fvp_rd_aspen_linux.sh
```

This builds the required QBox AArch64 modules and compiles:

```text
build/qbox-fvp-rd-aspen/fvp-rd-aspen-primary-compute.dtb
```

## Static Map Validation

```bash
./scripts/test/validate_qbox_fvp_rd_aspen_map.py
```

The report is written to:

```text
build/qbox-fvp-rd-aspen/map-validation.json
```

The validation compares the QBox Lua/DT sources with the Yocto-generated
RD-Aspen FVP TF-A device tree for the implemented primary-compute blocks:
DRAM, GICv3, ITS, PL011, SBSA watchdog, PL031 RTC, SMMUv3, armv7
memory-mapped timer, RAS FFH, DSU PMU, SCMI MHUv3, SI remoteproc MHUv3,
and virtio-mmio block/net/rng.

The SMMUv3 node uses the libqemu-backed `arm_smmuv3` component, not the
minimal register-only stub. QEMU's SMMUv3 model requires a primary PCI bus, so
the platform instantiates `qemu_gpex` only as the backing bus host. The
RD-Aspen device tree still exposes the FVP-compatible combined SMMUv3 SPI.

The RD-Aspen DT exposes 16 GIC redistributor windows. The configured primary
compute build uses 4 CPUs, so QBox instantiates 4 active GIC CPU interfaces and
keeps the remaining redistributor windows decoded as reserved memory.

## Full Reference Coverage Audit

```bash
./scripts/test/audit_qbox_fvp_rd_aspen_coverage.py
```

The report is written to:

```text
build/qbox-fvp-rd-aspen/coverage-audit.json
```

This audit is intentionally stricter than the static map validation. It marks
the currently implemented QBox blocks as passing only when their map/IRQ
definitions match and required runtime driver evidence is present. It also
reports any reference FVP DT blocks that are still not emulated by QBox.

## Headless Boot and Driver Probe Check

```bash
python3 scripts/run/run_qbox_fvp_rd_aspen_linux.py --timeout 600 --post-login-probe
```

The script writes the boot log and result files under:

```text
build/qbox-fvp-rd-aspen/<timestamp>/
```

`result.json` records boot/login patterns, failure patterns, and driver probe
patterns for GICv3/ITS, PL011, SBSA watchdog, armv7 memory-mapped timer,
RAS FFH, SMMUv3, DSU PMU, SCMI MHUv3, SI remoteproc/RPMsg, PL031 RTC,
virtio-blk, virtio-net, and virtio-rng. With `--post-login-probe`, the log
also records platform-device links, interrupt counters, module status, failed
systemd units, remoteproc state, RPMsg module load results, and explicit
`modprobe` results for the modules loaded by the image.

## RSE-Oriented Skeleton Mode

The RSE-oriented bring-up path is separate from this primary-compute direct boot
configuration. Its Lua configuration is:

```text
tools/qbox/platforms/fvp-rd-aspen-rse/conf.lua
```

Run a file-backed trace smoke with:

```bash
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --qemu-trace \
  --timeout 5 \
  --out-dir build/qbox-fvp-rd-aspen/rse-remote-cpu-trace-<run-id>
```

The current skeleton starts the generated RSE ROM through the existing
`RemoteCPU` Cortex-M55 wrapper and writes per-console logs plus `result.json`.
As of the 2026-05-23 runs, limited CC3XX, DTCM/ITCM alias, DMA350,
RSE system-control, ATU, LCM/OTP, KMU, Integrity Checker, RSE Strata boot
flash, host PPU, AP handoff host windows, and RSE-SI MHUv3/SCMI SystemC/TLM
models remove the previous
`rse_first_fault:0x501541c4` Data Abort, the BL1_1 DMA erase/fill timeout, the
later `rse_first_fault:0x58021100` reset-syndrome fault, the BL2
integration-layer gap, the first missing ATU-translated SI PIK host window,
the SI CL0 PPU polling loop, the SI CL0 AES-KW unwrap failure, the host ATU
placeholder gap, the RSE-SI MHUv3 init failure, the AP reset-release blocker,
the AP-RSE MHUv3 channel-count failure, and the AP BL2 image-loading timeout.
The LCM/OTP model loads the generated
`rse-otp-image.img` at the TF-M-visible LCM OTP window and provides
lifecycle/status reset values using the active TCI mode value. It also models
OTP-window writes, optional file writeback through
`QBOX_RDASPEN_RSE_OTP_WRITEBACK`, and lock-after-provision behavior through
`QBOX_RDASPEN_RSE_OTP_LOCK_AFTER_PROVISION`. The runner enables OTP writeback
only for per-run copied writable OTP images and keeps it disabled when
`--no-copy-writable-flash` uses deploy images directly. The RSE boot flash and
AP secure flash are `strata_flash_j3` CFI/Strata command-state models backed
by the runner's per-run raw `rse-flash-image.img` and `ap-flash-image.img`
copies. The active TF-M FVP Strata driver implements its erase helper by issuing
byte-program operations with value `0xff`, so the RD-Aspen platform keeps the
QBox Strata compatibility path that promotes a sector-aligned `0xff` byte
program into a modeled sector erase. A byte-scoped `0xff` experiment was
rejected because the persisted-flash second boot reached RSE BL_33 but U-Boot
could not initialize the UEFI subsystem and fell back to network boot.
`QBOX_RDASPEN_FLASH_WRITEBACK=true` enables program/erase write-through to those
raw copies only for normal copied runs; the runner sets it to `false` for
`--no-copy-writable-flash` so deploy images are not modified. The ATU model
is a translation model for the secure and non-secure host windows, with
translated DMI available only as an opt-in experiment through
`QBOX_RDASPEN_ATU_DMI=true`; selected host-memory DMI is also opt-in through
`QBOX_RDASPEN_HOST_MEMORY_DMI=true`. The translated-DMI path rejects
downstream DMI grants whose clipped upstream window does not cover the full
requested transaction span, so partial downstream windows do not become
over-broad AP/RSE host-window mappings. It also rejects two's-complement
negative add-value mappings when the logical address is smaller than the
negative offset magnitude, avoiding physical-address wraparound.
RSE boot-flash read-array DMI exists as a separate experiment. Full-device
boot-flash DMI can still hide CFI command-write side effects from the
`strata_flash_j3` model, so keep `QBOX_RDASPEN_BOOT_FLASH_DMI=false` for
unrestricted TF-M storage debugging. Even range-limited boot-flash DMI is a
diagnostic-only shortcut: a 2026-05-27 stats run with
`QBOX_RDASPEN_BOOT_FLASH_DMI_RANGES=0x7000:0x260000` granted one DMI mapping
and then recorded `write_accesses=0` and `command_writes=0`, proving the CFI
command-state model was bypassed after the grant. Use it only to split image
read throughput from CFI fidelity; do not use it as pass/fail evidence for
ITS, PS, UEFI variables, or FWU. AP flash has a matching range knob, for
example `QBOX_RDASPEN_AP_FLASH_DMI_RANGES=0x7000:0x240000` for the primary AP
FIP, with the same caveat.
Two RSE-local co-location knobs are enabled by default after the 2026-05-24
short-timeout validation: `QBOX_RDASPEN_RSE_LOCAL_CRYPTO=true` places KMU and
CC3XX in the RSE `RemoteCPU` process, and
`QBOX_RDASPEN_RSE_LOCAL_BOOT_FLASH=true` places the RSE `strata_flash_j3`
boot flash in that process. Set either variable to `false` to reproduce the
older cross-process debug path.
The RSE TCM aliases default to a single backing store for the CPU0 DTCM and
ITCM aliases. `QBOX_RDASPEN_RSE_SPLIT_CPU0_DTCM_ALIAS=true` remains available
as a debug experiment, but the split DTCM path makes TF-M's CC3XX final hash
DMA read stale TRAM-fill data from `0x34003820` instead of the copied tail at
`0x30003820`.
RSE ITCM, DTCM, and VM DMI are also enabled by default. The previous off-by-
default path is still available through `QBOX_RDASPEN_RSE_ITCM_DMI=false`,
`QBOX_RDASPEN_RSE_DTCM_DMI=false`, or `QBOX_RDASPEN_RSE_VM_DMI=false` when
debugging DMI-specific behavior.
The FVP RSE parameter defaults are exposed as Lua/CCI knobs:
`QBOX_RDASPEN_RSE_VMADDRWIDTH=18` derives the default `0x40000` VM0/VM1
windows, and `QBOX_RDASPEN_RSE_RESET_SYNDROME=0x80000000`,
`QBOX_RDASPEN_RSE_CPUWAIT=0x0000000F`,
`QBOX_RDASPEN_RSE_DMA_BOOT_EN=0x00000001`, and
`QBOX_RDASPEN_RSE_DMA_BOOT_ADDR=0x00000000` feed the `rse_sysctrl` reset
register defaults. `rse_sysctrl-tests` covers CCI overrides for those reset
defaults.
These paths still lack full permission/security/fault semantics. The KMU model includes
destination-port export writes and loads hardware slots from the LCM OTP
hardware-key area in `rse-otp-image.img`, including `KMU_HW_SLOT_KCE_CM`.
CC3XX includes SHA-256, AES-CTR component coverage, AES-CMAC tag generation,
AES-ECB decrypt coverage for AES-KW,
SHA-256 multipart `HASH_H`/`HASH_CUR_LEN` state restore for TF-M PSA hash/LMS
validation, and a modular PKA path for observed ADD/SUB immediate operations
plus component-tested status, shift, modular arithmetic, multiply, division,
exponentiation, inverse, and reduction semantics. The current default expected
result is still an RSE-oriented boot failure:

```text
blocker: qbox_platform_timeout_after_rse_runtime_chainload
first_failing_register_access: none
last short-timeout RSE UART: SI CL0 primary/secondary slot versions
flash preparation: gzip_decompressed_for_qbox_raw_memory
latest refined gap: real SI CL1 runtime/RPMsg peer beyond service-model/module-load evidence
rse_atu: translation-dmi-model when QBOX_RDASPEN_ATU_DMI=true
rse_boot_media: cfi-strata-flash-partial-model
rse_cc3xx: hash-aes-cmac-modular-pka-model
rse_host_scr: sid-system-cfg-register-model for CFG2 CL1 present bit
rse_mhuv3: SystemC mhu320ae component with reusable PBX/MBX frame model plus SCMI/RPMsg service-model hooks
rse_host_ppu: touched-status-model
rse_integrity_checker: touched-status-model
rse_kmu: touched-register-model
rse_lcm: otp-backed-writeback-lock-model
rse_sysctrl: touched-register-model
```

The default RSE CC3XX backend remains the SystemC `cc3xx` component. For
performance experiments, `scripts/run/run_qbox_fvp_rd_aspen_rse.py` and the Apollo
full-system wrappers accept `--cc3xx-qemu-native-backend`. That option selects
`QBOX_RDASPEN_CC3XX_BACKEND=qemu-native` and automatically enables the
`0x50154000:0x2000` direct MMIO fast path so RSE CPU CC3XX accesses enter the
QEMU `MemoryRegionOps` wrapper instead of the SystemC scheduler bridge. The
backend reuses the same `cc3xx_core` model and does not bypass secure boot
validation. The 2026-06-05 RSE timing bundle at
`build/qbox-apollo-fvp/cc3xx-qemu-native-20260605-001939/rse/` reduced the BL2
validation delta from 151.321s to 133.339s while preserving the RSE BL2 and
runtime handoff markers.
The full-system bundle
`build/qbox-apollo-fvp/cc3xx-qemu-native-20260605-003557/full/` reached
`passed: true` with Linux login and post-login probe evidence.
For RSE boot-time comparison against FVP, combine the qemu-native CC3XX backend
with `--rse-lms-accel --rse-fast-boot-aliases`. The fast alias preset installs
QEMU-local aliases for SI SRAM, AP BL2, RSE boot-flash read-only windows, AP
FIP reads, and the RSE PS/ITS storage direct-MMIO fast path. It is not an RSE
stub and does not force signature or hash success. Disable it for FWU,
PS/ITS persistence, negative secure-boot, or flash command-state fidelity
checks.
The current best RSE timing smoke is
`build/qbox-apollo-fvp/rse-step1-storage-direct-fastpath-20260608-1/`: with
qemu-native CC3XX, LMS acceleration, and fast boot aliases,
`rse_bl1_1` to `rse_first_image_slot` is 22.668 seconds. The matching FVP timed
run at `build/local-apollo-fvp/fvp-boot-timed-20260604/` is 4.818 seconds, so
the remaining QBox optimization target is the post-BL2 image-load/storage path
rather than an RSE-wide stub.
Do not include `--rse-bl2-load-profile` or `--qbox-perf-profile` in final
wall-time comparisons; those options are for hook/counter diagnostics and add
profiling overhead.
BL2 hook-based profile and accelerator modes resolve their function-entry
addresses from `--rse-bl2-elf`; the Apollo full-system wrapper forwards the
local-build TF-M BL2 ELF automatically. Check
`rse_bl2_load_profile.symbol_source` in the result JSON for the exact symbol
source and resolved addresses.
The BL2 load profile also records
`bl2_load_profile.ram_load_snapshot.by_image` in
`qbox-perf-profile/rse-hotpath-profile.json`. The 2026-06-08 smoke bundle at
`build/qbox-apollo-fvp/rse-bl2-ram-load-by-image-smoke-20260608/` captured
image 0, 2, 3, and 4 with zero DMI failures or unsupported layouts. Use that
section to verify the active MCUBoot header, `load_addr`, image size, hash
region size, and RAM-load flags before enabling a future
`--rse-bl2-load-accel`.
`--rse-bl2-boot-enc-accel` and `--rse-bl2-img-hash-accel` are optional
image-level semantic accelerators for this comparison. The image hash path can
read split direct-file aliases in 4 KiB chunks, which is required for images
whose header and payload fall into separate alias windows. A valid run records
`bl2_img_hash_accel.hits > 0` and `dmi_failures == 0` in
`qbox-perf-profile/rse-hotpath-profile.json`.
`--rse-bl2-verify-sig-accel` is currently a safe host-native ECDSA verifier
profile helper: it records and caches `bootutil_verify_sig` inputs, but leaves
the guest PKA-backed verification running unless a lower-level experimental
skip switch is set. Use the PKA counters to confirm whether a future
image-level semantic accelerator actually removes the remaining
`PKA_OPCODE`/`PKA_PIPE_RDY` traffic.
`--rse-bl2-verify-sig-skip` is the opt-in positive-boot variant. It only skips
the guest `bootutil_verify_sig()` body after the host-native ECDSA verifier
accepts the signature, then returns the guest `FIH_SUCCESS` value resolved from
the active BL2 ELF. The 2026-06-08 smoke bundle at
`build/qbox-apollo-fvp/rse-bl2-verify-sig-skip-remote-rebuild-smoke-20260608/`
reached RSE image 4/3/2/0 load, AP power-on, and first image slot with
`verify_matches=1` and `skip_hits=1`; it is not FWU or negative secure-boot
fidelity evidence.
The matching no-profile timing run is
`build/qbox-apollo-fvp/rse-bl2-verify-sig-skip-noprofile-smoke-20260608/`,
which recorded `rse_bl1_1` to `rse_first_image_slot` in 24.179 seconds.
With `--rse-bl2-boot-enc-accel --rse-bl2-img-hash-accel
--rse-bl2-verify-sig-skip` and profiling disabled, the smoke bundle
`build/qbox-apollo-fvp/rse-bl2-accel-no-load-profile-smoke-20260608/` reached
the same RSE image 4/3/2/0 load and first image slot markers in 22.772 seconds.
That is close to, but not better than, the 22.668-second fast-alias/storage
direct baseline, so BL2 accelerators remain opt-in development aids for the
next image-level accelerator rather than the default fidelity/performance
preset.

When changing Cortex-M55 CPU hook code in
`qemu-components/common/include/cpu.h`, rebuild `remote_cpu` as well as the CPU
module. The RSE `RemoteCPU` wrapper links the CPU header implementation into the
`remote_cpu` executable, so rebuilding only `cpu_arm_cortexM55.so` can leave RSE
smoke runs on stale hook code.

This is not an RSE boot pass. It is evidence that the platform now progresses
past the first CC3XX register abort, the observed DMA350 fill writes, the first
RSE system-control register access, ATU programming, LCM/OTP reads, and KMU
random-delay reads. The later KMU/CC3XX traces also show non-zero OTP-backed
KCE_CM words exported into CC3XX `AES_KEY_0..7`. The runner now converts the
gzip-compressed deploy RSE flash image into a per-run raw image before binding
it to the boot-flash model, which lets BL1_2 decrypt BL2 successfully. QBox
also preserves CC3XX SHA-256 multipart state through `HASH_H[0..7]` and
`HASH_CUR_LEN0/1`, which lets BL1_2 validate BL2 and jump to BL2. AES-ECB
decrypt support removes the SI CL0 AES-KW unwrap failure, and runtime now
shows SI CL0 image 3 RAM loading, key-hash match, SI CL0 post-load, RSE-SI
SCMI shared-memory initialization, `Init SCMI comm to SCP succeeded`, Power
Domain protocol version `0x20000`, AP BL2 image 2 loading, AP ATU programming,
RSE runtime image 0 loading, `RSE to SCP SCMI power on AP succeeded`,
`Jumping to the first image slot`, modeled AP0 reset release, and AP secure
BL2 console output. Later work reaches Linux login in the RSE-oriented AP CPU
path. AP CPU execution is disabled by default. With
`QBOX_RDASPEN_ENABLE_AP_CPUS=true`, AP0 is powered but reset-held until the
modeled RSE/SCP release; secondary CPUs remain powered off for later PSCI. The
AP-RSE MHU channel-count failure, AP BL2 image id 6 panic, AP system timer
abort, AP-SI SCMI MHU feature-register abort, and AP BL31 RAS system-register
trap are removed. Current runtime evidence reaches BL31 runtime services,
Linux login, and file-backed post-login driver probes; short GDB samples may
still stop earlier by design. The CFG2 SI CL1 presence check is now backed by
a `host_scr`
SystemC/TLM model instead of zeroed memory, so RSE BL2 loads image 4 and
records `BL2: SI CL1 post load complete`. The first SI CL1 RPMsg service-model
increment is also present: the AP/SI CL1 MHU path now seeds the CL1 remoteproc
resource table through the Lua indexed CCI preset path, defers the RPMsg
name-service packet until the Linux host kick, and runtime evidence shows
Linux registering `virtio6 (type 7)` plus creating `ethsi1` for `si-cl1`.
Post-login evidence proves remoteproc attach, `rpmsg_ns`, `virtio_rpmsg_bus`,
`rpmsg_net`, `/sys/bus/rpmsg/devices/virtio6.ethsi1.-1.1024`, and
`ethsi1_iplink_rc:0`. The remaining fidelity gap is replacing this
service-modeled endpoint with a real SI CL1 CPU/Zephyr peer and packet
data-plane behavior. QBox now
accepts the TF-M System Power `SYS_POWER_STATE_NOTIFY` subscription request in
the RSE-SCP SCMI responder. A focused 2026-05-24 GDB trace showed the first
NS mailbox BusFault was the missing RSE-local MHU0 sender frame at
`0x50160fcc`; the RSE-oriented QBox platform now maps secure local MHU0/MHU2
sender and receiver frames. A later 2026-05-24 AP-RSE bridge/IRQ run maps the
AP secure mailbox as directional `ap_s_to_rse` and `rse_to_ap_s` pairs, routes
RSE MHU0/MHU2 receiver IRQs to TF-M IRQs 41/45, and routes the SI CL0-to-RSE
receiver to IRQ 139. The MHU trace proves AP writes reach the RSE MHU2
receiver, TF-M clears the receiver channels, and the RSE MHU2 sender replies
back to the AP-visible mailbox. Current short-timeout runtime
`build/qbox-fvp-rd-aspen/rse-current-runtime-markers-postlogin-20260524-v1/`
records `RT_0`, `SCMI Comms subscribed to power state notifications`, and
measured-boot markers through `BL_33`, then times out at U-Boot because the
run was capped at 160 seconds. Separate login-focused evidence remains
`build/qbox-fvp-rd-aspen/rse-post-login-threaded-input-20260524-v3/`.
RSE-local DMI paths are enabled by default for short-timeout iteration after
fd-backed RemotePass validation. ATU and host-memory DMI remain explicit
opt-ins because they can hide host-window side effects. Boot-flash DMI is kept
off for TF-M storage debug until flash command-state/DMI invalidation behavior
is proven equivalent.
The temporary ATU/LCM/KMU/Integrity Checker/host-window/boot-media behavior
also still needs to be expanded into documented hardware semantics.

For the current default RSE handoff check, use:

```bash
QBOX_RDASPEN_ATU_DMI=true \
QBOX_RDASPEN_HOST_MEMORY_DMI=true \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --timeout 600 \
  --out-dir build/qbox-fvp-rd-aspen/rse-handoff-<run-id>
```

For a short RSE-only co-location progress probe, use:

```bash
QBOX_RDASPEN_RSE_LOCAL_CRYPTO=true \
QBOX_RDASPEN_RSE_LOCAL_BOOT_FLASH=true \
QBOX_RDASPEN_ATU_DMI=true \
QBOX_RDASPEN_HOST_MEMORY_DMI=true \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --timeout 90 \
  --out-dir build/qbox-fvp-rd-aspen/rse-local-progress-<run-id>
```

For the experimental AP CPU path, use:

```bash
QBOX_RDASPEN_ENABLE_AP_CPUS=true \
QBOX_RDASPEN_ATU_DMI=true \
QBOX_RDASPEN_HOST_MEMORY_DMI=true \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --pc-trace \
  --pc-trace-limit 4096 \
  --timeout 750 \
  --out-dir build/qbox-fvp-rd-aspen/rse-ap-cpus-<run-id>
```

For non-interactive GDB setup across QBox host, TF-M/RSE, AP/Linux,
SCP-Firmware symbols, and SI CL1 Zephyr symbols, use:

```bash
QBOX_RDASPEN_ENABLE_AP_CPUS=true \
QBOX_RDASPEN_ATU_DMI=true \
QBOX_RDASPEN_HOST_MEMORY_DMI=true \
QBOX_RDASPEN_BOOT_FLASH_DMI=false \
python3 scripts/debug/debug_qbox_fvp_rd_aspen_rse_gdb.py \
  --launch \
  --sample-only \
  --sample-delay 190 \
  --runner-timeout 235 \
  --port-timeout 5 \
  --gdb-timeout 8 \
  --out-dir build/qbox-fvp-rd-aspen/gdb-debug-<run-id> \
  --rootfs build/qbox-fvp-rd-aspen/rse-t019da-bootargs-console-probe-20260524-v1/rootfs-console-probe.wic
```

For the current SI CL0 encrypted-image trace, use:

```bash
QBOX_RDASPEN_ATU_DMI=true \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --boot-enc-trace \
  --qemu-trace-events in_asm \
  --timeout 900 \
  --out-dir build/qbox-fvp-rd-aspen/rse-boot-enc-trace-<run-id>
```

For lightweight current-PC tracing, use:

```bash
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --pc-trace \
  --pc-trace-interval 200 \
  --pc-trace-limit 5000 \
  --timeout 300 \
  --out-dir build/qbox-fvp-rd-aspen/rse-pc-trace-<run-id>
```

To include Arm M-profile exception/fault state in the same file-backed trace,
use:

```bash
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --exception-trace \
  --pc-trace-interval 200 \
  --pc-trace-limit 5000 \
  --timeout 180 \
  --out-dir build/qbox-fvp-rd-aspen/rse-exception-trace-<run-id>
```

For GDB-backed inspection of QBox host, TF-M/RSE, AP/Linux, current
SCP-Firmware symbol state, and SI CL1 Zephyr symbol state, generate a debug
bundle:

```bash
python3 scripts/debug/debug_qbox_fvp_rd_aspen_rse_gdb.py \
  --out-dir build/qbox-fvp-rd-aspen/gdb-debug-<run-id>
```

To also run short non-interactive probes, add `--launch`. The helper defaults
to short GDB-oriented timeouts (`45s` runner, `8s` port/GDB probes) and a
current RSE debug environment with RSE-local CC3XX/KMU, RSE-local boot flash,
and RSE ITCM/DTCM/VM DMI enabled. ATU, RSE boot-flash, and host-memory DMI stay
disabled unless the caller overrides them. To
capture a QBox host thread/backtrace sample without changing Linux
`ptrace_scope`, add `--host-sample`. To avoid
early GDB attach
perturbation and inspect the current post-decrypt progress point, use
`--sample-only --sample-delay 28`. For targeted TF-M branch splits, add
`--tfm-static-boundary-trace`, `--tfm-core-init-trace`,
`--tfm-partition-panic-trace`, `--tfm-its-init-trace`, or
`--tfm-ps-init-trace` to generate and run short breakpoint traces through
static-boundary setup, `tfm_core_init()`, secure partition panic attribution,
ITS initialization, or PS initialization. Use `--tfm-ns-mailbox-trace` when
splitting `TFM_NS_MAILBOX_AGENT`, SFCP, MHUv3 local frame, and AP-RSE mailbox
progress. Use
`--tfm-ps-object-table-trace` when PS reaches object-table load/save and the
question is whether TF-M ITS flash metadata can be read back after an erase.
If the firmware is expected to print
`[ERR]` and spin in an error loop, add `--ignore-fail-patterns` so the runner
records the failure but keeps QBox alive for GDB. The helper uses QBox CPU
`gdb_port` CCI parameters rather than raw QEMU GDB arguments, and the AP GDB
target exposes CPU#0-CPU#3 through `info threads`. See
`doc/qbox-fvp-rd-aspen-gdb-debug.md` for generated artifacts, source-path
mapping, the host GDB wrapper, and current SCP/SI CL1 live-target limitations.

For login-tail debugging, add `--copy-writable-flash`,
`--post-login-probe`, and `--keep-running-after-pass`; this uses per-run
writable RSE/AP flash copies, feeds the primary UART post-login probe, and
keeps the platform attachable until the bounded GDB probes finish.

For RSE/AP Strata flash transaction counts in the same GDB workflow, add
`--flash-stats --flash-stats-interval 512`. The helper writes
`rse-strata-stats.json` and `ap-strata-stats.json` under the run directory and
records the effective `QBOX_RDASPEN_*_FLASH_STATS_FILE` environment in
`debug-env.json`. The stats include DMI hint/request/grant/reject counters, so
they also show when a boot-flash DMI experiment bypasses firmware-visible CFI
command traffic. This is the preferred way to inspect whether a short run is
spending time in firmware-visible CFI byte-program/status-poll traffic without
turning on verbose per-access tracing.

The latest login-tail all-target GDB artifact is
`build/qbox-fvp-rd-aspen/gdb-login-keepalive-all-targets-20260525-v1/`; the
main runtime pass evidence is
`build/qbox-fvp-rd-aspen/rse-v004-full-postlogin-20260525-v1/`.
The latest short all-layer GDB recheck is
`build/qbox-fvp-rd-aspen/gdb-current-short-20260525-v4/`; it uses a separate
rootfs copy, per-run flash copies, SSH host forward `2223`, 6-second GDB probe
timeouts, and a 65-second sample delay. The latest Linux-marker GDB recheck is
`build/qbox-fvp-rd-aspen/gdb-linux-marker-20260525-v1/`; it waits up to
240 seconds for `Linux version` before sampling the same targets.

For file-backed Linux login and driver evidence on the RSE-oriented path, use
the primary-UART post-login probe:

```bash
QBOX_RDASPEN_ENABLE_AP_CPUS=true \
QBOX_RDASPEN_ATU_DMI=true \
QBOX_RDASPEN_HOST_MEMORY_DMI=true \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --post-login-probe \
  --rootfs build/qbox-fvp-rd-aspen/rse-t019da-bootargs-console-probe-20260524-v1/rootfs-console-probe.wic \
  --timeout 900 \
  --out-dir build/qbox-fvp-rd-aspen/rse-post-login-<run-id>
```

The probe uses a FIFO-backed primary UART input file and injects `root` only
after `fvp-rd-aspen login:` appears in `qbox-primary-console.log`, avoiding the
static prefeed problem where U-Boot consumes login commands too early. The
primary UART file backend reads the FIFO from a host-side polling thread so an
empty input FIFO does not consume SystemC simulation time during long AP boot
runs. Override the default 100 ms host poll interval with
`QBOX_RDASPEN_PRIMARY_UART_POLL_INTERVAL_MS` only when the proof latency itself
is being debugged.

For bounded secure-service userspace evidence, add
`--secure-service-probe --secure-service-probe-timeout <seconds>` to the same
command. This extends the post-login probe with presence checks and short
`timeout`-guarded runs for Trusted Services PSA Initial Attestation, ITS, PS,
and UEFI variable test tools. The probe also records Linux FF-A/TEE discovery
state (`/dev/tee*`, `/sys/bus/arm_ffa/devices`, dmesg, and installed test
tools) before the bounded commands. Results are recorded under
`post_login_probe.secure_service_probe` in `result.json`; they document current
secure-service gaps and do not change the base boot pass criteria.

For the Secure FWU capsule-on-disk runtime flow, add `--fwu-probe` to the
same RSE runner. The probe waits for Linux login through the FIFO-backed UART
input, then mounts `/dev/vda1` and `/dev/vdb1`, copies `/mnt/fw.cap` into
`/boot/EFI/UpdateCapsule/`, requests reboot, and evaluates the FWU U-Boot,
RSE, and TF-A bank markers in `post_login_probe.fwu_probe`:

```bash
QBOX_RDASPEN_ENABLE_AP_CPUS=true \
QBOX_RDASPEN_ATU_DMI=true \
QBOX_RDASPEN_HOST_MEMORY_DMI=true \
QBOX_RDASPEN_BOOT_FLASH_DMI=false \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --fwu-probe \
  --ignore-fail-patterns \
  --rootfs build/qbox-fvp-rd-aspen/rse-t019da-bootargs-console-probe-20260524-v1/rootfs-console-probe.wic \
  --timeout 480 \
  --out-dir build/qbox-fvp-rd-aspen/rse-fwu-probe-<run-id>
```

This is intentionally marker-based and does not treat FWU as complete until
the capsule application, RSE image-1 boot, TF-A `FIP_B`, and U-Boot trial-state
markers are observed. If Linux login is not reached, `result.json` keeps the
run failed with `qbox_fwu_probe_incomplete`.

The RSE platform wires AP guest reset through QBox `reset_gpio`: AP QEMU reset
events are fanned out to `ap_cpu_0.reset` through `ap_cpu_3.reset`, and the
runner builds the `reset_gpio` dynamic module as a required target. This makes
the Linux/FWU reboot path observable on the modeled AP CPU reset sockets; full
FWU success still requires the bank-1 markers above.

When MHU tracing is enabled, summarize AP-RSE request/response pairing with:

```bash
python3 scripts/analyze/analyze_qbox_mhu_trace.py \
  build/qbox-fvp-rd-aspen/<run-id>/mhuv3-trace.log \
  --json-out build/qbox-fvp-rd-aspen/<run-id>/mhuv3-analysis.json \
  --summary-out build/qbox-fvp-rd-aspen/<run-id>/mhuv3-analysis.txt
```

The default pairing mode checks AP secure-service doorbells from
`ap_s_to_rse` to `rse_to_ap_s` on channel 1 with value prefix `0x800`.

Before attempting the destructive Secure FWU capsule/reboot flow, inspect the
current A/B bank and metadata baseline:

```bash
python3 scripts/inspect/inspect_qbox_fvp_rd_aspen_fwu.py \
  --out-dir build/qbox-fvp-rd-aspen/fwu-inspect-<run-id>
```

The helper parses the generated RSE flash, AP flash, RSE private metadata, AP
FWU metadata, VirtIO block 1 capsule disk, capsule image, and capsule manifest.
It writes `fwu-inspection.json` and `summary.md` and does not modify deploy
artifacts. This is a preflight check only; the full Secure FWU acceptance still
requires applying the capsule, rebooting into bank 1, checking RSE and TF-A
bank-1 log markers, and proving flash state persistence across reboot.

When the default deploy WIC needs a visible primary UART console without the
high-volume `earlycon`, `ignore_loglevel`, and `initcall_debug` arguments, use
the per-run bootargs patch profile:

```bash
QBOX_RDASPEN_ENABLE_AP_CPUS=true \
QBOX_RDASPEN_ATU_DMI=true \
QBOX_RDASPEN_HOST_MEMORY_DMI=true \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --post-login-probe \
  --rootfs build/tmp_baremetal/deploy/images/fvp-rd-aspen/baremetal-image-fvp-rd-aspen.wic \
  --rootfs-bootargs-profile quiet-console \
  --timeout 180 \
  --out-dir build/qbox-fvp-rd-aspen/rse-post-login-quiet-<run-id>
```

The profile creates a sparse copied WIC under the run directory and patches
only `/loader/entries/boot.conf` in that copy.

For ATU and host PPU traces, enable:

```bash
QBOX_RDASPEN_ATU_TRACE=true \
QBOX_RDASPEN_ATU_TRACE_LIMIT=4096 \
QBOX_RDASPEN_HOST_PPU_TRACE=true \
QBOX_RDASPEN_HOST_PPU_TRACE_LIMIT=128 \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --timeout 240 \
  --out-dir build/qbox-fvp-rd-aspen/rse-host-ppu-<run-id>
```

For focused RSE boot-flash command traces, enable the Strata flash trace
budget:

```bash
QBOX_RDASPEN_BOOT_FLASH_TRACE=true \
QBOX_RDASPEN_BOOT_FLASH_TRACE_LIMIT=256 \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --timeout 300 \
  --out-dir build/qbox-fvp-rd-aspen/rse-boot-flash-<run-id>
```

For focused CC3XX traces, enable `QBOX_RDASPEN_CC3XX_TRACE=true` and select a
filter with `QBOX_RDASPEN_CC3XX_TRACE_FILTER`. Supported filters are `all`,
`pka`, `pka-opcode`, `dma`, and `crypto`. The `dma` filter now includes hash
engine `DIN_SRC_LLI` programming as well as AES DMA, which is useful for
TF-M BL1_1/BL1_2 SHA-256 source-address checks. The `pka` filter is useful
when checking whether later CRYPTOCELL PKA traffic appears, because it avoids
exhausting the trace budget on non-PKA CC3XX traffic:

The RSE runner also classifies PC-trace timeouts against the TF-M BL1_1 map.
Short pre-AP timeouts in the shared CC3XX/CFI path are reported as
`rse_bl1_1_cc3xx_crypto_timeout:*` or
`rse_bl1_1_cfi_flash_io_timeout:*`, which is more useful than a generic
platform timeout when iterating on RSE image validation and Strata traffic.

```bash
QBOX_RDASPEN_CC3XX_TRACE=true \
QBOX_RDASPEN_CC3XX_TRACE_FILTER=pka \
QBOX_RDASPEN_CC3XX_TRACE_LIMIT=200000 \
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --timeout 300 \
  --out-dir build/qbox-fvp-rd-aspen/rse-cc3xx-pka-filter-<run-id>
```

For the QEMU-native CC3XX backend, collect stats and timing with:

```bash
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --range-limited-flash-dmi \
  --cc3xx-stats \
  --cc3xx-stats-interval 65536 \
  --cc3xx-qemu-native-backend \
  --timeout 600 \
  --out-dir build/qbox-fvp-rd-aspen/rse-cc3xx-qemu-native-<run-id>
```
