# QBox RD-Aspen Primary Compute

This platform boots the RD-Aspen primary-compute Linux image directly on QBox
using the local `tools/qemu` checkout as `libqemu`.

## Build

```bash
./scripts/build_qbox_fvp_rd_aspen_linux.sh
```

This builds the required QBox AArch64 modules and compiles:

```text
build/qbox-fvp-rd-aspen/fvp-rd-aspen-primary-compute.dtb
```

## Static Map Validation

```bash
./scripts/validate_qbox_fvp_rd_aspen_map.py
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
./scripts/audit_qbox_fvp_rd_aspen_coverage.py
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
python3 scripts/run_qbox_fvp_rd_aspen_linux.py --timeout 600 --post-login-probe
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
