#!/usr/bin/env python3

import argparse
import multiprocessing
import subprocess
import sys


JOBS = multiprocessing.cpu_count()


COMMON_ARGS = {
    "platform": "linuxbsd",
    "use_llvm": "yes",
    "linker": "lld",

    "builtin_enet": "no",
    "builtin_graphite": "no",
    "builtin_harfbuzz": "no",
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


def run(args: list[str]) -> int:
    print(" ".join(args))
    return subprocess.call(args)


def build(preset: str, compiledb: bool = True) -> int:
    cmd = ["scons"]

    for k, v in PRESETS[preset].items():
        cmd.append(f"{k}={v}")

    if compiledb:
        cmd.append("compiledb=yes")

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

    if args.command == "clean":
        sys.exit(clean())

    sys.exit(build(args.command))


if __name__ == "__main__":
    main()