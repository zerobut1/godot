"""Verify Yutrel specular reflections on Vulkan with disk and embedded shaders."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent
STAGES = [f"rt_reflection_{stage}.slang" for stage in ("classify", "trace", "reproject", "prefilter", "temporal", "upsample")]
RELOAD_DEPENDENCIES = {
    "rt_reflection_common.slang": STAGES,
    "rt_scene.slang": ["rt_reflection_trace.slang"],
    **{stage: [stage] for stage in STAGES},
}

if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    shaders = FIXTURE.parents[2] / "servers/rendering/renderer_rd/shaders/yutrel"
    with tempfile.TemporaryDirectory() as work:
        binary = Path(work) / "upsample.spv"
        subprocess.run(["slangc", shaders / STAGES[-1], "-I", shaders, "-entry", "compute_main",
                        "-target", "spirv", "-profile", "spirv_1_5", "-emit-spirv-directly", "-o", binary], check=True)
        os.environ["YUTREL_REFLECTION_REGION_SPIRV"] = str(binary)
        verify_fixture(FIXTURE, __doc__, RELOAD_DEPENDENCIES, embedded_editor=True, compare_editor_image=True)
