"""Exercise directional shadows with a Vulkan validation layer and a built Yutrel editor."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

FIXTURE = Path(__file__).resolve().parent
ROOT = FIXTURE.parents[2]
WORK = ROOT / "bin/yutrel-validation/csm"
SHADERS = ROOT / "servers/rendering/renderer_rd/shaders/yutrel"
MODULES = ["shadow_caster.slang", "shadow_mask.slang", "directional_light.slang"]


def wait_for(process, log, marker, offset=0):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        text = log.read_bytes()[offset:].decode(errors="replace")
        if marker in text:
            return
        if process.poll() is not None:
            break
        time.sleep(0.1)
    raise RuntimeError(f"Missing {marker!r} in {log}\n{text[-3000:]}")


def run(exe, project, appdata, editor):
    label = "editor" if editor else "runtime"
    log = WORK / f"{label}.log"
    marker = WORK / "reload-done"
    marker.unlink(missing_ok=True)
    env = dict(os.environ, APPDATA=str(appdata), LOCALAPPDATA=str(appdata), VK_LOADER_DEBUG="layer",
               YUTREL_CSM_RELOAD_MARKER=str(marker))
    if "VULKAN_SDK" in env:
        env["VK_LAYER_PATH"] = str(Path(env["VULKAN_SDK"]) / "Bin")
    args = [exe, "--path", project, "--rendering-method", "yutrel", "--rendering-driver", "vulkan",
            "--gpu-validation", "--verbose", "--single-window", "--max-fps", "60"]
    args.extend(["--editor", "res://editor_scene.tscn"] if editor else ["--script", "res://test_csm.gd"])
    timestamps = {}
    with log.open("wb") as stream:
        process = subprocess.Popen(args, stdout=stream, stderr=subprocess.STDOUT, env=env,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            if not editor:
                wait_for(process, log, "CSM READY FOR RELOAD")
                for module in MODULES:
                    source = SHADERS / module
                    stat = source.stat()
                    timestamps[source] = (stat.st_atime_ns, stat.st_mtime_ns)
                    offset = log.stat().st_size
                    os.utime(source, ns=(stat.st_atime_ns, max(time.time_ns(), stat.st_mtime_ns) + 3_000_000_000))
                    wait_for(process, log, f"hot-reloaded {module}.", offset)
                marker.touch()
            status = process.wait(timeout=90)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            for source, times in timestamps.items():
                os.utime(source, ns=times)
    text = log.read_text(encoding="utf-8", errors="replace")
    errors = re.findall(r"^.*(?:ERROR:|Validation Error|VUID-).*$", text, re.M)
    if (status or errors or f"CSM {label.upper()} PASS" not in text
            or 'Insert instance layer "VK_LAYER_KHRONOS_validation"' not in text):
        raise RuntimeError(f"{label} failed ({status}) {errors[:10]}. See {log}")
    print(f"CSM {label} passed with Vulkan validation. {log}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", nargs="?", type=Path,
                        default=ROOT / "bin/godot.windows.editor.dev.x86_64.yutrel.console.exe")
    exe = parser.parse_args().exe.resolve()
    project = WORK / "project"
    shutil.copytree(FIXTURE, project, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".godot", "__pycache__", "verify.py"))
    cert = project / "ca-bundle.crt"
    shutil.copyfile(ROOT / "thirdparty/certs/ca-bundle.crt", cert)
    with (project / "project.godot").open("a", encoding="utf-8") as config:
        config.write('\n[network]\ntls/certificate_bundle_override="res://ca-bundle.crt"\n')
    appdata = WORK / "appdata"
    settings = appdata / "Godot/editor_settings-4.7.tres"
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_text('[gd_resource type="EditorSettings" format=3]\n\n[resource]\n'
                        'interface/editor/localization/editor_language = "en"\n'
                        'network/connection/check_for_updates = 0\n'
                        f'network/tls/editor_tls_certificates = {json.dumps(cert.as_posix())}\n', encoding="utf-8")
    run(exe, project, appdata, False)
    run(exe, project, appdata, True)


if __name__ == "__main__":
    main()
