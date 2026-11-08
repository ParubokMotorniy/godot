#!/usr/bin/env python3

import argparse
import contextlib
import fcntl
import multiprocessing
import shutil
import subprocess
import sys
from pathlib import Path


JOBS = multiprocessing.cpu_count()

ROOT = Path(__file__).resolve().parent.parent
LOCK_PATH = ROOT / "bin" / ".build.lock" 


COMMON_ARGS = {
    "platform": "linuxbsd",
    "use_llvm": "yes",
    "linker": "lld",

    "builtin_enet": "no",
    "builtin_graphite": "no",
    "builtin_harfbuzz": "yes",
    "builtin_libogg": "no",
    "builtin_libpng": "no",
    "builtin_libtheora": "no",
    "builtin_libvorbis": "no",
    "builtin_libwebp": "no",
    "builtin_mbedtls": "no",
    "builtin_miniupnpc": "no",
    "builtin_pcre2": "no",
    "builtin_sdl": "no",
    "builtin_zlib": "no",

    "optimize": "speed",
}


PRESETS = {
    "debug": {
        **COMMON_ARGS,
        "target": "editor",
        "dev_build": "yes",
    },

    "release": {
        **COMMON_ARGS,
        "target": "editor",
        "production": "yes",
    },
}


@contextlib.contextmanager
def build_lock():
    """Refuse to run next to another build in this tree.

    Two scons processes sharing bin/obj and .sconsign5.dblite delete and recompile each other's objects,
    so every run turns into a near-full rebuild. The OS drops the lock when the process dies.
    """
    LOCK_PATH.parent.mkdir(exist_ok=True)
    with open(LOCK_PATH, "w") as lock_file:
        try:
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            sys.exit("Another build is already running in this tree. Wait for it to finish or stop it first.")
        yield


def run(args: list[str]) -> int:
    print(" ".join(args))
    return subprocess.call(args)


def build(preset: str, compiledb: bool = True) -> int:
    cmd = ["scons"]

    for k, v in PRESETS[preset].items():
        cmd.append(f"{k}={v}")

    if compiledb:
        cmd.append("compiledb=yes")

    if shutil.which("ccache"):
        cmd.append("c_compiler_launcher=ccache")
        cmd.append("cpp_compiler_launcher=ccache")

    cmd.append(f"-j{JOBS}")

    return run(cmd)


def clean() -> int:
    return run(["scons", "-c"])


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "command",
        choices=[
            "debug",
            "release",
            "clean",
        ],
    )

    args = parser.parse_args()

    with build_lock():
        if args.command == "clean":
            sys.exit(clean())

        sys.exit(build(args.command))


if __name__ == "__main__":
    main()