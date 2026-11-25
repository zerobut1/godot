"""Verify Yutrel motion vectors, temporal upscaling and display-resolution overlays under Vulkan validation."""

import os
import subprocess
import sys
from tempfile import TemporaryDirectory
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent
SHADERS = ("depth_prepass.slang", "base_pass.slang", "skybox.slang", "transparent_rt.slang")
RELOAD_DEPENDENCIES = {shader: [shader] for shader in SHADERS}

if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    root = FIXTURE.parents[2]
    shaders = root / "servers/rendering/renderer_rd/shaders/yutrel"
    work = root / "bin/yutrel-validation/upscaler"
    work.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=work) as directory:
        for shader in ("base_pass", "skybox"):
            for stage in ("vertex", "fragment"):
                subprocess.run(["slangc", shaders / f"{shader}.slang", "-I", shaders, "-entry", f"{stage}_main",
                                "-target", "spirv", "-profile", "spirv_1_5", "-emit-spirv-directly",
                                "-o", Path(directory) / f"{shader}-{stage}.spv"], check=True)
        os.environ["YUTREL_VELOCITY_SPIRV_DIR"] = directory
        verify_fixture(FIXTURE, __doc__, RELOAD_DEPENDENCIES, embedded_editor=True)
