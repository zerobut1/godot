"""Verify Yutrel post-processing, temporal exposure and HDR/display output under Vulkan validation."""

import os
import re
import sys
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent
RELOAD = ("auto_exposure_histogram.slang", "bloom_prefilter.slang", "color_grading_lut.slang", "output.slang")

if __name__ == "__main__":
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import verify_fixture

    os.environ["VK_LAYER_VALIDATE_SYNC"] = "1"
    verify_fixture(FIXTURE, __doc__, {module: [module] for module in RELOAD}, embedded_editor=True, compare_editor_image=True)

    logs = FIXTURE.parents[2] / "bin/yutrel-validation/postprocess"
    for label in ("disk", "editor", "embedded", "editor-embedded"):
        log = logs / f"{label}.log"
        contents = log.read_text(encoding="utf-8", errors="replace")
        warnings = re.findall(
            r"^.*(?:Validation Warning|WARNING:|Vulkan[^\n]*WARNING).*$",
            contents, re.M | re.I,
        )
        if warnings:
            raise RuntimeError(f"{label}: {warnings[:5]}. See {log}")
        if label in ("disk", "embedded"):
            batch = contents.split("POSTPROCESS CACHE BEGIN", 1)[1].split("POSTPROCESS CACHE END", 1)[0]
            if batch.count("Yutrel: baked color grading LUT.") != 1:
                raise RuntimeError(f"{label}: expected one LUT bake across display and grading edits. See {log}")
        if label == "disk":
            reloads = contents.split("POSTPROCESS READY FOR RELOAD", 1)[1].split("POSTPROCESS CACHE BEGIN", 1)[0]
            if reloads.count("Yutrel: baked color grading LUT.") != 2:
                raise RuntimeError(f"{label}: expected a LUT bake after reloading both LUT and output shaders. See {log}")
