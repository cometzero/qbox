# QBox Apollo FVP Primary Compute

This platform boots the Apollo FVP primary-compute Linux image directly on
QBox. It is intended for local-build validation of the Linux kernel,
initramfs, and primary-compute device model wiring.

This is not the full Apollo firmware chain. RSE, TF-A, OP-TEE, and U-Boot are
bypassed by the QBox AArch64 direct-boot stub. Use the RSE-oriented QBox
platform when firmware-chain fidelity is required.

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
./scripts/build_qbox_apollo_fvp_linux.sh
```

## Headless Boot

```bash
python3 scripts/run_qbox_apollo_fvp_linux.py \
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
scripts/run_qbox_apollo_fvp_linux.sh
```

Set `QBOX_APOLLO_TIMEOUT=0` for an unbounded interactive session.
