"""Verify directional RT shadows, denoising, editor output and shader reload under Vulkan validation."""

import sys
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent
SHADERS = ("rt_shadow_trace.slang", "rt_shadow_temporal.slang", "rt_shadow_denoise.slang", "rt_shadow_upsample.slang")
RELOAD_DEPENDENCIES = {
    "rt_geometry_prepare.slang": ["rt_geometry_prepare.slang"],
    "rt_shadow_common.slang": SHADERS,
    **{shader: [shader] for shader in SHADERS},
    "rt_scene.slang": ["rt_shadow_trace.slang", "transparent_rt.slang"],
    "transparent_lighting.slang": ["transparent.slang", "transparent_rt.slang"],
    "transparent_rt.slang": ["transparent_rt.slang"],
}

if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    verify_fixture(FIXTURE, __doc__, RELOAD_DEPENDENCIES, embedded_editor=True,
                   compare_editor_image=True)
