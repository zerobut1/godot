"""Verify DDGI probe lighting, editor views and hot reload under Vulkan validation."""

import sys
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent
RELOAD_DEPENDENCIES = {
    "ddgi_common.slang": ["ddgi_probe_trace.slang", "ddgi_blend_irradiance.slang", "ddgi_blend_distance.slang", "ibl.slang", "transparent.slang"],
    "ddgi_probe_trace.slang": ["ddgi_probe_trace.slang"],
    "ddgi_blend_irradiance.slang": ["ddgi_blend_irradiance.slang"],
}

if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    verify_fixture(FIXTURE, __doc__, RELOAD_DEPENDENCIES, embedded_editor=True)
