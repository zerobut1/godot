"""Exercise directional shadows with a Vulkan validation layer and a built Yutrel editor."""

import sys
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent

RELOAD_DEPENDENCIES = {
    "shadow_caster.slang": ["shadow_caster.slang"],
    "shadow_mask.slang": ["shadow_mask.slang"],
    "directional_light.slang": ["directional_light.slang"],
}


if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    verify_fixture(FIXTURE, __doc__, RELOAD_DEPENDENCIES)
