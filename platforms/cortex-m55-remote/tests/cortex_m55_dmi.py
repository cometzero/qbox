#!/usr/bin/env python3
# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import os
import platform
import signal
from pathlib import Path
from sys import stdout

import pexpect

IS_WINDOWS = platform.system() == "Windows"

if IS_WINDOWS:
    from pexpect.popen_spawn import PopenSpawn


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-e", "--exe", required=True, help="Path to cortex-m55-vp")
    parser.add_argument("-l", "--lua", required=True, help="Path to Lua configuration")
    parser.add_argument("-f", "--firmware", required=True, help="Path to test firmware binary")
    parser.add_argument("--dmi", choices=("true", "false"), default="true")
    args = parser.parse_args()

    vp_path = Path(args.exe)
    lua_path = Path(args.lua)
    fw_path = Path(args.firmware)
    env = os.environ.copy()
    env["QBOX_CORTEX_M55_DMI_FW"] = fw_path.as_posix()
    env["QBOX_CORTEX_M55_DMI_ENABLE"] = args.dmi

    if IS_WINDOWS:
        child = PopenSpawn(f'"{vp_path.as_posix()}" --gs_luafile "{lua_path.as_posix()}"', env=env)
    else:
        child = pexpect.spawn(vp_path.as_posix(), ["--gs_luafile", lua_path.as_posix()], env=env, timeout=8)
    child.logfile = stdout.buffer

    try:
        child.expect("DMI byte-store PASS")
    finally:
        if IS_WINDOWS:
            child.kill(signal.SIGTERM)
        else:
            child.kill(signal.SIGQUIT)
            child.wait()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
