"""Run from any directory with Python, slangc, spirv-val, and the Vulkan SDK.

Build the editor first, then run this script with its console executable as an
optional argument. Run it with no builds or shader edits in progress because
the embedded-source test temporarily moves the on-disk shaders.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


FIXTURE = Path(__file__).resolve().parent
ROOT = FIXTURE.parents[2]
SHADERS = ROOT / "servers/rendering/renderer_rd/shaders/yutrel"
WORK = ROOT / "bin/yutrel-validation/ibl"
RELOAD_ORDER = [
    ("ibl_common.slang", ["ibl.slang", "skybox.slang", "ibl_equirect_to_cube.slang",
                          "ibl_prefilter_specular.slang", "ibl_convolve_sh.slang"], 1),
    ("ibl_equirect_to_cube.slang", ["ibl_equirect_to_cube.slang"], 1),
    ("ibl_downsample_cube.slang", ["ibl_downsample_cube.slang"], 1),
    ("ibl_prefilter_specular.slang", ["ibl_prefilter_specular.slang"], 1),
    ("ibl_convolve_sh.slang", ["ibl_convolve_sh.slang"], 1),
    ("ibl.slang", ["ibl.slang"], 0),
    ("skybox.slang", ["skybox.slang"], 0),
]
BAKE = "Yutrel: baked IBL."


def validate_spirv(source_dir, output_dir):
    output_dir.mkdir(parents=True, exist_ok=True)
    count = 0
    for source in sorted(source_dir.glob("*.slang")):
        for stage in re.findall(r'^\[shader\("(\w+)"\)\]', source.read_text(encoding="utf-8"), re.M):
            binary = output_dir / f"{source.stem}-{stage}.spv"
            subprocess.run(["slangc", source, "-I", source_dir, "-entry", f"{stage}_main", "-stage", stage,
                            "-target", "spirv", "-profile", "spirv_1_5", "-emit-spirv-directly",
                            "-o", binary], check=True)
            subprocess.run(["spirv-val", "--target-env", "vulkan1.2", binary], check=True)
            count += 1
    if count < 17:
        raise RuntimeError(f"Expected the Yutrel stages in {source_dir}, got {count}")
    print(f"{output_dir.name}: spirv-val passed for {count} stages", flush=True)


def extract_embedded(header, destination):
    destination.mkdir(parents=True, exist_ok=True)
    text = header.read_text(encoding="utf-8")
    sources = dict(re.findall(r"source_(\d+)\[\] = \{([^}]*)\}", text))
    for filename, index in re.findall(r'\{ "([^"]+)", source_(\d+),', text):
        data = bytes.fromhex(" ".join(re.findall(r"0x([\da-f]{2})", sources[index])))
        if data[-1:] != b"\0" or data[:-1] != (SHADERS / filename).read_bytes():
            raise RuntimeError(f"Embedded shader {filename} differs from the disk source")
        (destination / filename).write_bytes(data[:-1])


def read_log(path):
    return path.read_text(encoding="utf-8", errors="replace")


def wait_for(process, log, predicate, description, timeout=40):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        lines = read_log(log)
        if predicate(lines):
            return lines
        if process.poll() is not None:
            break
        time.sleep(0.10)
    raise RuntimeError(f"Did not observe {description} in {log}\n{read_log(log)[-5000:]}")


def check_hot_reload(process, log, originals):
    wait_for(process, log, lambda text: "IBL READY FOR RELOAD" in text, "the reload marker")
    for filename, modules, expected_bakes in RELOAD_ORDER:
        source = SHADERS / filename
        stat = source.stat()
        originals[source] = (stat.st_atime_ns, stat.st_mtime_ns)
        offset = len(read_log(log))
        os.utime(source, ns=(stat.st_atime_ns, max(stat.st_mtime_ns, time.time_ns()) + 3_000_000_000))
        if expected_bakes:
            wait_for(process, log, lambda text: BAKE in text[offset:], f"the bake after saving {filename}")
        wait_for(process, log, lambda text: all(f"hot-reloaded {module}." in text[offset:] for module in modules),
                 f"reload of {filename}")
        time.sleep(0.4)
        bakes = read_log(log)[offset:].count(BAKE)
        if bakes != expected_bakes:
            raise RuntimeError(f"{filename}: expected {expected_bakes} bakes, got {bakes}")
        print(f"Reloaded {filename}, {bakes} bakes", flush=True)


def run_godot(exe, project, label, *, editor=False, hot_reload=False, embedded=False):
    log = WORK / f"{label}.log"
    appdata = WORK / "appdata"
    appdata.mkdir(exist_ok=True)
    if editor:
        settings = appdata / "Godot/editor_settings-4.7.tres"
        settings.parent.mkdir(exist_ok=True)
        cert_path = json.dumps((project / "ca-bundle.crt").as_posix())
        settings.write_text(
            '[gd_resource type="EditorSettings" format=3]\n\n[resource]\n'
            'interface/editor/localization/editor_language = "en"\n'
            'network/connection/check_for_updates = 0\n'
            f'network/tls/editor_tls_certificates = {cert_path}\n',
            encoding="utf-8",
        )
    marker = WORK / "reload-done"
    marker.unlink(missing_ok=True)
    env = dict(os.environ, APPDATA=str(appdata), LOCALAPPDATA=str(appdata), VK_LOADER_DEBUG="layer")
    env.pop("YUTREL_IBL_RELOAD_MARKER", None)
    if "VULKAN_SDK" in env:
        env["VK_LAYER_PATH"] = str(Path(env["VULKAN_SDK"]) / "Bin")
    if hot_reload:
        env["YUTREL_IBL_RELOAD_MARKER"] = str(marker)
    args = [exe, "--path", project, "--rendering-method", "yutrel", "--rendering-driver", "vulkan",
            "--gpu-validation", "--verbose", "--single-window", "--max-fps", "60"]
    args.extend(["--editor", "res://editor_scene.tscn"] if editor else ["--script", "res://test_ibl.gd"])
    originals = {}
    with log.open("wb") as stream:
        process = subprocess.Popen(args, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            if hot_reload:
                check_hot_reload(process, log, originals)
                marker.touch()
            status = process.wait(timeout=70)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            for source, times in originals.items():
                os.utime(source, ns=times)
    text = read_log(log)
    success = "IBL EDITOR PASS" if editor else "IBL PASS"
    errors = [line for line in text.splitlines() if re.search(r"ERROR:|Validation Error|VUID-", line)]
    if (status or errors or success not in text or "Vulkan " not in text
            or 'Insert instance layer "VK_LAYER_KHRONOS_validation"' not in text):
        raise RuntimeError(f"{label} failed with exit {status} and errors {errors[:10]}. See {log}")
    if not editor:
        active = text.split("LIVE SKY CHANGES BEGIN", 1)[1].split("LIVE SKY CHANGES END", 1)[0]
        if BAKE in active:
            raise RuntimeError("Changing sky rotation or intensity triggered a bake")
    if embedded:
        modules = {module for _, names, _ in RELOAD_ORDER for module in names}
        for module in modules:
            if f"loaded {module} from embedded sources." not in text:
                raise RuntimeError(f"{module} did not load from embedded sources")
    for line in text.splitlines():
        if line.startswith(("FURNACE ", "HDRI ", "IMAGE ")):
            print(line)
    print(f"{label}: passed with Vulkan validation ({log})", flush=True)
    return re.findall(r"^IMAGE .+$", text, re.M)


def main():
    sys.path.insert(0, str(FIXTURE.parent))
    from validation import embedded_only

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", nargs="?", type=Path,
                        default=ROOT / "bin/godot.windows.editor.dev.x86_64.yutrel.console.exe")
    parser.add_argument("--embedded-header", type=Path, default=SHADERS / "yutrel_embedded_shaders.gen.h")
    args = parser.parse_args()
    project = WORK / "project"
    shutil.copytree(FIXTURE, project, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".godot", "verify.py", "__pycache__"))
    shutil.copyfile(ROOT / "thirdparty/certs/ca-bundle.crt", project / "ca-bundle.crt")
    with (project / "project.godot").open("a", encoding="utf-8") as config:
        config.write('\n[network]\ntls/certificate_bundle_override="res://ca-bundle.crt"\n')

    exe = args.exe.resolve()
    validate_spirv(SHADERS, WORK / "disk-spirv")
    with tempfile.TemporaryDirectory(dir=WORK) as source_dir:
        embedded_source = Path(source_dir)
        extract_embedded(args.embedded_header, embedded_source)
        validate_spirv(embedded_source, WORK / "embedded-spirv")
    disk = run_godot(exe, project, "runtime-hot-reload", hot_reload=True)
    run_godot(exe, project, "editor", editor=True)
    with embedded_only(SHADERS, WORK):
        embedded = run_godot(exe, project, "runtime-embedded", embedded=True)
        run_godot(exe, project, "editor-embedded", editor=True, embedded=True)
    if len(disk) != 3 or disk != embedded:
        raise RuntimeError("Disk and embedded shader renders differ")
    print("Disk and embedded shader renders have identical HDR image hashes.", flush=True)


if __name__ == "__main__":
    main()
