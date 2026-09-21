# -*- coding: utf-8 -*-
#!/usr/bin/env python3

"""Configure and build the static Slang compiler used by this Godot fork."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


GODOT_ROOT = Path(__file__).resolve().parents[2]
SLANG_SOURCE_DIR = GODOT_ROOT / "thirdparty" / "slang"
DEFAULT_BUILD_DIR = GODOT_ROOT / "build" / "slang" / "windows-x86_64-msvc-vs2026"

GENERATOR = "Visual Studio 18 2026"
PLATFORM = "x64"

# Keep this list explicit. It is the build contract between Slang's CMake build
# and the later Godot SCons integration.
CMAKE_OPTIONS = {
    "BUILD_SHARED_LIBS": "OFF",
    "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreaded",
    "CMAKE_C_FLAGS": "/utf-8 /MP",
    "CMAKE_CXX_FLAGS": "/utf-8 /MP /EHsc /D_ITERATOR_DEBUG_LEVEL=0",
    "SLANG_LIB_TYPE": "STATIC",
    "SLANG_EMBED_CORE_MODULE": "ON",
    "SLANG_EMBED_CORE_MODULE_SOURCE": "ON",
    "SLANG_ENABLE_AFTERMATH": "OFF",
    "SLANG_ENABLE_ASAN": "OFF",
    "SLANG_ENABLE_COVERAGE": "OFF",
    "SLANG_ENABLE_CUDA": "OFF",
    "SLANG_ENABLE_DXIL": "OFF",
    "SLANG_ENABLE_EXAMPLES": "OFF",
    "SLANG_ENABLE_GFX": "OFF",
    "SLANG_ENABLE_IR_BREAK_ALLOC": "OFF",
    "SLANG_ENABLE_MIMALLOC": "OFF",
    "SLANG_ENABLE_NVAPI": "OFF",
    "SLANG_ENABLE_OPTIX": "OFF",
    "SLANG_ENABLE_PCH": "ON",
    "SLANG_ENABLE_RELEASE_DEBUG_INFO": "ON",
    "SLANG_ENABLE_RELEASE_LTO": "OFF",
    "SLANG_ENABLE_REPLAYER": "OFF",
    "SLANG_ENABLE_SLANGC": "OFF",
    "SLANG_ENABLE_SLANGD": "OFF",
    "SLANG_ENABLE_SLANGI": "OFF",
    "SLANG_ENABLE_SLANGRT": "OFF",
    "SLANG_ENABLE_SLANG_GLSLANG": "OFF",
    "SLANG_ENABLE_SLANG_PROXY": "OFF",
    "SLANG_ENABLE_SLANG_RHI": "OFF",
    "SLANG_ENABLE_SPIRV_TOOLS_MIMALLOC": "OFF",
    "SLANG_ENABLE_SPLIT_DEBUG_INFO": "OFF",
    "SLANG_ENABLE_TESTS": "OFF",
    "SLANG_ENABLE_TIME_TRACE": "OFF",
    "SLANG_ENABLE_VALIDATION_IR": "OFF",
    "SLANG_EXCLUDE_DAWN": "ON",
    "SLANG_EXCLUDE_TINT": "ON",
    "SLANG_SLANG_LLVM_FLAVOR": "DISABLE",
    "SLANG_STANDARD_MODULE_DEVELOP_BUILD": "OFF",
}

EXPECTED_LIBRARIES = (
    "slang-compiler.lib",
    "compiler-core.lib",
    "core.lib",
    "miniz.lib",
    "lz4.lib",
    "cmark-gfm.lib",
)

FORBIDDEN_RUNTIME_DLLS = (
    "slang.dll",
    "slang-compiler.dll",
    "slang-glslang.dll",
    "slang-glsl-module.dll",
)


def configure_utf8_stdio() -> None:
    """Use UTF-8 for Python diagnostics without failing on redirected streams."""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="replace")


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def command_text(command: list[str]) -> str:
    return subprocess.list2cmdline(command)


def run(command: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    print(f"> {command_text(command)}", flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def find_cmake() -> str:
    cmake = shutil.which("cmake")
    if cmake is None:
        fail("CMake was not found in PATH.")

    result = subprocess.run(
        [cmake, "--help"],
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=True,
    )
    if GENERATOR not in result.stdout:
        fail(
            f'CMake does not provide the "{GENERATOR}" generator. '
            "Install a CMake version with Visual Studio 2026 support."
        )
    return cmake


def validate_source_tree() -> None:
    required_files = (
        SLANG_SOURCE_DIR / "CMakeLists.txt",
        SLANG_SOURCE_DIR / "include" / "slang.h",
        SLANG_SOURCE_DIR / "external" / "cmark" / "CMakeLists.txt",
        SLANG_SOURCE_DIR / "external" / "lz4" / "build" / "cmake" / "CMakeLists.txt",
        SLANG_SOURCE_DIR / "external" / "miniz" / "CMakeLists.txt",
    )
    missing = [path for path in required_files if not path.is_file()]
    if missing:
        formatted = "\n".join(f"  {path}" for path in missing)
        fail(
            "The Slang submodule or one of its nested dependencies is not initialized:\n"
            f"{formatted}\n"
            "Run: git submodule update --init --recursive"
        )


def find_artifact(build_dir: Path, config: str, filename: str) -> Path:
    matches = [path for path in build_dir.rglob(filename) if config.lower() in str(path).lower()]
    if not matches:
        fail(f'Expected build artifact "{filename}" was not produced under {build_dir}.')

    # Prefer the shortest path if a dependency creates more than one copy.
    return min(matches, key=lambda path: (len(path.parts), str(path).lower()))


def get_source_revision() -> str:
    git = shutil.which("git")
    if git is None:
        return "unknown"
    result = subprocess.run(
        [git, "-C", str(SLANG_SOURCE_DIR), "rev-parse", "HEAD"],
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def validate_and_write_manifest(build_dir: Path, config: str) -> Path:
    libraries = {
        filename: str(find_artifact(build_dir, config, filename).resolve())
        for filename in EXPECTED_LIBRARIES
    }

    forbidden = []
    for filename in FORBIDDEN_RUNTIME_DLLS:
        forbidden.extend(build_dir.rglob(filename))
    if forbidden:
        formatted = "\n".join(f"  {path}" for path in forbidden)
        fail(f"A static-only Slang build produced unexpected runtime DLLs:\n{formatted}")

    generated_include_dir = build_dir / config / "include"
    generated_slang_header = generated_include_dir / "slang.h"
    if not generated_slang_header.is_file():
        fail(f"The generated Slang include tree is missing: {generated_slang_header}")

    manifest = {
        "schema_version": 1,
        "source_revision": get_source_revision(),
        "generator": GENERATOR,
        "platform": PLATFORM,
        "toolset": "Visual Studio 2026 default (MSVC 14.5x)",
        "configuration": config,
        "runtime_library": "MultiThreaded (/MT)",
        "source_dir": str(SLANG_SOURCE_DIR.resolve()),
        "build_dir": str(build_dir.resolve()),
        "include_dir": str(generated_include_dir.resolve()),
        "libraries": libraries,
    }
    manifest_path = build_dir / "slang-build.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest_path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the static Slang compiler for the Godot Windows editor."
    )
    parser.add_argument(
        "--config",
        choices=("Release", "RelWithDebInfo", "Debug"),
        default="RelWithDebInfo",
        help="CMake configuration to build (default: RelWithDebInfo).",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help=f"Out-of-source build directory (default: {DEFAULT_BUILD_DIR}).",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, (os.cpu_count() or 2) - 1),
        help="Maximum parallel build jobs (default: CPU count minus one).",
    )
    parser.add_argument(
        "--configure-only",
        action="store_true",
        help="Generate the build tree without compiling Slang.",
    )
    parser.add_argument(
        "--fresh",
        action="store_true",
        help="Discard the existing CMake cache before configuring.",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    return args


def main() -> int:
    configure_utf8_stdio()
    args = parse_arguments()
    validate_source_tree()
    cmake = find_cmake()

    build_dir = args.build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["PYTHONUTF8"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    env["VSLANG"] = "1033"
    env["PreferredUILang"] = "en-US"
    env["DOTNET_CLI_UI_LANGUAGE"] = "en-US"

    configure_command = [cmake]
    if args.fresh:
        configure_command.append("--fresh")
    configure_command.extend(
        [
            "-S",
            str(SLANG_SOURCE_DIR),
            "-B",
            str(build_dir),
            "-G",
            GENERATOR,
            "-A",
            PLATFORM,
        ]
    )
    configure_command.extend(f"-D{name}={value}" for name, value in CMAKE_OPTIONS.items())

    print("Configuring static Slang with VS 2026 MSVC and UTF-8 source handling.")
    run(configure_command, cwd=GODOT_ROOT, env=env)

    if args.configure_only:
        print(f"Slang configuration completed: {build_dir}")
        return 0

    build_command = [
        cmake,
        "--build",
        str(build_dir),
        "--config",
        args.config,
        "--target",
        "slang",
        "--parallel",
        str(args.jobs),
    ]
    print(f"Building static Slang ({args.config}).")
    run(build_command, cwd=GODOT_ROOT, env=env)

    manifest_path = validate_and_write_manifest(build_dir, args.config)
    print("Static Slang build completed successfully.")
    print(f"Build manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        fail(f"Command failed with exit code {error.returncode}: {command_text(error.cmd)}")
