"""Shared fixtures for the Vulkan renderer verification scripts."""

import argparse
import json
import os
import re
import shutil
import subprocess
import time
from contextlib import contextmanager
from pathlib import Path


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


def prepare_project(fixture, work):
    project = work / "project"
    shutil.copytree(fixture, project, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".godot", "__pycache__", "verify.py"))
    cert = project / "ca-bundle.crt"
    shutil.copyfile(fixture.parents[2] / "thirdparty/certs/ca-bundle.crt", cert)
    with (project / "project.godot").open("a", encoding="utf-8") as config:
        config.write('\n[network]\ntls/certificate_bundle_override="res://ca-bundle.crt"\n')
    appdata = work / "appdata"
    settings = appdata / "Godot/editor_settings-4.7.tres"
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_text('[gd_resource type="EditorSettings" format=3]\n\n[resource]\n'
                        'interface/editor/localization/editor_language = "en"\n'
                        'network/connection/check_for_updates = 0\n'
                        f'network/tls/editor_tls_certificates = {json.dumps(cert.as_posix())}\n', encoding="utf-8")
    return project, appdata


@contextmanager
def embedded_only(shaders, work):
    backup = work / "shader-source-hold"
    backup.mkdir(exist_ok=True)
    if any(backup.iterdir()):
        raise RuntimeError(f"Restore the shader sources from {backup} before rerunning")
    moved = []
    try:
        for original in shaders.glob("*.slang"):
            path = original.rename(backup / original.name)
            moved.append((path, original))
        yield
    finally:
        for path, original in reversed(moved):
            path.rename(original)


def run(exe, project, appdata, shaders, label, suite, dependencies):
    work = project.parent
    prefix = suite.upper()
    reload_env = f"YUTREL_{prefix}_RELOAD_MARKER"
    editor = label == "editor"
    log = work / f"{label}.log"
    marker = work / "reload-done"
    marker.unlink(missing_ok=True)
    env = dict(os.environ, APPDATA=str(appdata), LOCALAPPDATA=str(appdata), VK_LOADER_DEBUG="layer")
    env.pop(reload_env, None)
    if "VULKAN_SDK" in env:
        env["VK_LAYER_PATH"] = str(Path(env["VULKAN_SDK"]) / "Bin")
    if label == "disk":
        env[reload_env] = str(marker)
    args = [exe, "--path", project, "--rendering-method", "yutrel", "--rendering-driver", "vulkan",
            "--gpu-validation", "--verbose", "--single-window", "--max-fps", "60"]
    args.extend(["--editor", "res://editor_scene.tscn"] if editor else ["--script", f"res://test_{suite}.gd"])
    timestamps = {}
    with log.open("wb") as stream:
        process = subprocess.Popen(args, stdout=stream, stderr=subprocess.STDOUT, env=env,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            if label == "disk":
                wait_for(process, log, f"{prefix} READY FOR RELOAD")
                for module, dependents in dependencies.items():
                    path = shaders / module
                    stat = path.stat()
                    timestamps[path] = (stat.st_atime_ns, stat.st_mtime_ns)
                    offset = log.stat().st_size
                    os.utime(path, ns=(stat.st_atime_ns, max(time.time_ns(), stat.st_mtime_ns) + 3_000_000_000))
                    for dependent in dependents:
                        wait_for(process, log, f"hot-reloaded {dependent}.", offset)
                marker.touch()
            status = process.wait(timeout=90)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            for path, timestamp in timestamps.items():
                os.utime(path, ns=timestamp)
    text = log.read_text(encoding="utf-8", errors="replace")
    errors = re.findall(r"^.*(?:ERROR:|Validation Error|VUID-|leaked|still in use at exit).*$", text, re.M | re.I)
    success = f"{prefix} EDITOR PASS" if editor else f"{prefix} RUNTIME PASS"
    if status or errors or success not in text or 'Insert instance layer "VK_LAYER_KHRONOS_validation"' not in text:
        raise RuntimeError(f"{label} failed ({status}): {errors[:12]}. See {log}")
    if label == "embedded":
        for module in {name for modules in dependencies.values() for name in modules}:
            if f"loaded {module} from embedded sources." not in text:
                raise RuntimeError(f"{module} did not load from the executable")
    print(f"{prefix} {label} passed with Vulkan validation. {log}", flush=True)
    return re.findall(rf"^{prefix} IMAGE ([0-9a-f]{{64}})$", text, re.M)


def verify_fixture(fixture, description, dependencies):
    suite = fixture.name
    root = fixture.parents[2]
    shaders = root / "servers/rendering/renderer_rd/shaders/yutrel"
    work = root / "bin/yutrel-validation" / suite
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("exe", nargs="?", type=Path,
                        default=root / "bin/godot.windows.editor.dev.x86_64.yutrel.console.exe")
    exe = parser.parse_args().exe.resolve()
    project, appdata = prepare_project(fixture, work)
    disk = run(exe, project, appdata, shaders, "disk", suite, dependencies)
    run(exe, project, appdata, shaders, "editor", suite, dependencies)
    with embedded_only(shaders, work):
        embedded = run(exe, project, appdata, shaders, "embedded", suite, dependencies)
    if len(disk) != 1 or disk != embedded:
        raise RuntimeError("Disk and embedded HDR images differ")
    print(f"{suite.upper()} HDR image matches on disk and embedded: {disk[0]}")
