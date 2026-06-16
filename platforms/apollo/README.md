# QBox Apollo Platform

This platform contains Apollo QBox entrypoints for primary-compute direct boot,
SI CL1 isolated boot, and the full RSE-first Apollo QVP.

This is not the full Apollo firmware chain. RSE, TF-A, OP-TEE, and U-Boot are
bypassed by the QBox AArch64 direct-boot stub. Use the RSE-oriented QBox
platform when firmware-chain fidelity is required.

The primary-compute direct-boot entrypoint is:

```text
tools/qbox/platforms/apollo/apollo-pc.lua
```

The SI CL1 isolated Zephyr entrypoint is:

```text
tools/qbox/platforms/apollo/apollo-si-cl1.lua
```

The full-system QBox virtual platform entrypoint is:

```text
tools/qbox/platforms/apollo/apollo-qvp.lua
```

The full-system RSE/AP base topology is owned by the Apollo platform:

```text
tools/qbox/platforms/apollo/hw-block/rse.lua
```

Hardware-block helpers used by the full-system entrypoint live under:

```text
tools/qbox/platforms/apollo/hw-block/
```

The current full-system block helpers are:

```text
hw-block/rse.lua
hw-block/primary_compute.lua
hw-block/ap_compute.lua
hw-block/si_cl0.lua
hw-block/si_cl1.lua
hw-block/si_cl1_isolated.lua
hw-block/ros.lua
hw-block/system_mgmt.lua
```

`hw-block/ros.lua` tracks the modeled Rest of System subset from the Arm Zena
CSS FVP RoS peripheral table: AP-visible virtio block/net/rng and PL031 RTC.

`hw-block/system_mgmt.lua` tracks cross-domain system-management ownership and
helpers: AP/RSE MHU logical aliases, reset/power integration, SMD shared
memory, SCMI/PFDI messaging, ATU windows, and CL0-visible safety/control
surfaces. RSE secure boot and RSE-local security peripherals remain in
`hw-block/rse.lua`.

## Build Local Artifacts

```bash
./local-build.sh build
```

The QBox runner consumes:

```text
build/local-apollo-fvp/deploy/boot/Image
build/local-apollo-fvp/deploy/boot/initramfs.cpio.gz
```

The runner generates a QBox-specific DTB at:

```text
build/qbox-apollo-fvp/apollo-fvp-primary-compute.dtb
```

## Build QBox Targets

```bash
./local-build.sh qbox
```

## Full-System RSE-First Boot

The default Apollo full-system performance path uses shared-memory SRAM DMI.
It forwards `--rse-fast-boot-sram-dmi` to the RSE runner and sets
`QBOX_RDASPEN_HOST_SRAM_SHARED_MEMORY=true` so host SI/AP SRAM regions use
transferable shared-memory DMI instead of direct-file aliases.

```bash
./run_qbox.sh
```

For a bounded headless command, use:

```bash
python3 scripts/run/run_qbox_apollo_fvp_full.py \
  --si-mode live-cl0-cl1 \
  --skip-build \
  --timeout 2400 \
  --rootfs-bootargs-profile none \
  --post-login-probe \
  --cc3xx-qemu-native-backend \
  --rse-lms-accel \
  --rse-fast-boot-sram-dmi \
  --out-dir build/qbox-apollo-fvp/full-live-cl0-cl1-sram-dmi
```

The RSE child `result.json` should report
`rse_fast_boot_sram_dmi.enabled: true`,
`rse_fast_boot_sram_dmi.env.QBOX_RDASPEN_HOST_SRAM_SHARED_MEMORY: "true"`, and
`rse_direct_file_aliases_summary.enabled: false`. The `host_sram_backing`
entries for the host SRAM regions should use `mode: "shared_memory"` and
`file_created: false`.

The default SRAM DMI path should not create file-backed host SRAM images:

```bash
find build/qbox-apollo-fvp/full-live-cl0-cl1-sram-dmi -type f \( \
  -name 'host-si-cl*-sram.bin' -o \
  -name 'host-ap-*-sram.bin' \
\) -print -quit
```

The command should print nothing. Use the legacy file-backed SRAM aliases only
for explicit debug or compatibility rollback:

```bash
./run_qbox.sh --legacy-file-backed-sram
```

For direct RSE-runner debugging, the legacy equivalent is:

```bash
python3 scripts/run/run_qbox_fvp_rd_aspen_rse.py \
  --skip-build \
  --cc3xx-qemu-native-backend \
  --rse-lms-accel \
  --rse-fast-boot-aliases \
  --qbox-perf-profile \
  --timeout 90 \
  --ignore-fail-patterns \
  --out-dir build/qbox-apollo-fvp/rse-legacy-file-backed-sram
```

## Headless Boot

```bash
python3 scripts/run/run_qbox_apollo_fvp_linux.py \
  --timeout 600 \
  --post-login-probe
```

The result files are written under:

```text
build/qbox-apollo-fvp/<timestamp>/
```

Inspect:

```text
result.json
summary.txt
qbox-apollo-fvp.log
```

## Interactive Boot

```bash
python3 scripts/run/run_qbox_apollo_fvp_linux.py \
  --skip-build \
  --interactive \
  --timeout "${QBOX_APOLLO_TIMEOUT:-0}" \
  --local-build-dir build/local-apollo-fvp
```

Set `QBOX_APOLLO_TIMEOUT=0` for an unbounded interactive session.
