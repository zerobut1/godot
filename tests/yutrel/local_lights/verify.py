"""Verify clustered local lights, ray-traced visibility and secondary lighting under Vulkan validation."""

import sys
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent
PASSES = ["cluster_build.slang", "local_light.slang", "local_light_rt.slang"]
RELOAD_DEPENDENCIES = {
    "local_lights.slang": PASSES,
    "cluster_build.slang": ["cluster_build.slang"],
    "local_light.slang": ["local_light.slang", "local_light_rt.slang"],
    "local_light_rt.slang": ["local_light_rt.slang"],
}

if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    verify_fixture(FIXTURE, __doc__, RELOAD_DEPENDENCIES, embedded_editor=True,
                   compare_editor_image=True)
