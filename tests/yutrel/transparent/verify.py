"""Verify forward transparency with Vulkan validation. Run after building, with no concurrent shader edits."""

import sys
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent

RELOAD_DEPENDENCIES = {
    "csm.slang": ["shadow_mask.slang", "transparent.slang"],
    "transparent.slang": ["transparent.slang"],
}


if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    verify_fixture(FIXTURE, __doc__, RELOAD_DEPENDENCIES)
