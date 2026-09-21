# -*- coding: utf-8 -*-


def can_build(env, platform):
    return platform == "windows" and env["arch"] == "x86_64"


def get_opts(platform):
    return [
        (
            "slang_build_dir",
            "Path to the CMake build directory containing the static Slang build",
            "build/slang/windows-x86_64-msvc-vs2026",
        ),
    ]


def configure(env):
    if env["use_mingw"]:
        raise RuntimeError("The Slang module requires the MSVC ABI; use_mingw=yes is not supported.")
    if env["use_llvm"]:
        raise RuntimeError("The current Slang libraries were built with MSVC; use_llvm=yes is not supported.")
    if not env["use_static_cpp"]:
        raise RuntimeError("The Slang module requires use_static_cpp=yes to match Slang's /MT runtime.")
    if env["debug_crt"]:
        raise RuntimeError("The Slang module requires debug_crt=no to match Slang's /MT runtime.")

    msvc_version = env.get("msvc_version", "")
    if msvc_version and msvc_version != "14.5":
        raise RuntimeError("The Slang module was built with VS 2026; use msvc_version=14.5.")
