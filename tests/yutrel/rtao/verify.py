"""Verify ray-traced AO, denoising, editor output and shader reload under Vulkan validation."""

import sys
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent
SHADERS = ("rtao_trace.slang", "rtao_temporal.slang", "rtao_denoise.slang")

if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    verify_fixture(FIXTURE, __doc__, {shader: [shader] for shader in SHADERS}, embedded_editor=True,
                   compare_editor_image=True)
