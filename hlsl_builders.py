"""Functions used to embed HLSL source files during build time."""

import os
import re

from methods import generated_wrapper, to_raw_cstring


_INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*"([^"]+)"\s*(?://.*)?$')
_STAGE_PATTERN = re.compile(r"^#\[(vertex|fragment|compute|raygen|any_hit|closest_hit|miss|intersection)\]$")
_STAGE_ORDER = ("vertex", "fragment", "compute", "raygen", "any_hit", "closest_hit", "miss", "intersection")


def _display_path(filename: str) -> str:
    return os.path.relpath(filename).replace("\\", "/")


def _expand_include_file(filename: str, include_stack: tuple[str, ...]) -> str:
    absolute_filename = os.path.normcase(os.path.abspath(filename))
    if absolute_filename in include_stack:
        cycle = include_stack + (absolute_filename,)
        raise RuntimeError("Cyclic HLSL include: " + " -> ".join(_display_path(path) for path in cycle))

    if not os.path.isfile(absolute_filename):
        raise RuntimeError(f'HLSL source file "{_display_path(absolute_filename)}" does not exist.')

    display_filename = _display_path(absolute_filename)
    expanded_lines = [f'#line 1 "{display_filename}"\n']
    with open(absolute_filename, "r", encoding="utf-8-sig", newline=None) as source_file:
        for line_number, line in enumerate(source_file, start=1):
            include_match = _INCLUDE_PATTERN.match(line.rstrip("\r\n"))
            if include_match is None:
                expanded_lines.append(line)
                continue

            include_filename = os.path.normpath(
                os.path.join(os.path.dirname(absolute_filename), include_match.group(1))
            )
            if not os.path.isfile(include_filename):
                raise RuntimeError(
                    f'HLSL include "{include_match.group(1)}" referenced by '
                    f'"{display_filename}:{line_number}" does not exist.'
                )

            included_source = _expand_include_file(include_filename, include_stack + (absolute_filename,))
            expanded_lines.append(included_source)
            if included_source and not included_source.endswith("\n"):
                expanded_lines.append("\n")
            expanded_lines.append(f'#line {line_number + 1} "{display_filename}"\n')

    return "".join(expanded_lines)


def _read_stage_sources(shader: str) -> dict[str, str]:
    absolute_shader = os.path.normcase(os.path.abspath(shader))
    source_path = _display_path(absolute_shader)
    stage_lines: dict[str, list[str]] = {}
    current_stage = ""

    with open(absolute_shader, "r", encoding="utf-8-sig", newline=None) as source_file:
        for line_number, line in enumerate(source_file, start=1):
            stage_match = _STAGE_PATTERN.match(line.strip())
            if stage_match is not None:
                current_stage = stage_match.group(1)
                if current_stage in stage_lines:
                    raise RuntimeError(f'HLSL stage "{current_stage}" appears more than once in "{source_path}".')
                stage_lines[current_stage] = [f'#line {line_number + 1} "{source_path}"\n']
                continue

            if not current_stage:
                continue

            include_match = _INCLUDE_PATTERN.match(line.rstrip("\r\n"))
            if include_match is None:
                stage_lines[current_stage].append(line)
                continue

            include_filename = os.path.normpath(
                os.path.join(os.path.dirname(absolute_shader), include_match.group(1))
            )
            if not os.path.isfile(include_filename):
                raise RuntimeError(
                    f'HLSL include "{include_match.group(1)}" referenced by '
                    f'"{source_path}:{line_number}" does not exist.'
                )

            included_source = _expand_include_file(include_filename, (absolute_shader,))
            stage_lines[current_stage].append(included_source)
            if included_source and not included_source.endswith("\n"):
                stage_lines[current_stage].append("\n")
            stage_lines[current_stage].append(f'#line {line_number + 1} "{source_path}"\n')

    if not stage_lines:
        raise RuntimeError(f'HLSL source file "{source_path}" does not declare any shader stages.')

    return {stage: "".join(lines) for stage, lines in stage_lines.items()}


def _shader_class_name(shader: str) -> str:
    stem = os.path.basename(shader).removesuffix(".hlsl")
    words = [word for word in re.split(r"[^0-9A-Za-z]+", stem) if word]
    class_name = "".join(word[0].upper() + word[1:] for word in words)
    if class_name and class_name[0].isdigit():
        class_name = "_" + class_name
    return class_name + "ShaderRD"


def _write_stage_code(output_file, stage_sources: dict[str, str], stage: str) -> None:
    if stage in stage_sources:
        output_file.write(
            f"""\
\t\tstatic const char _{stage}_code[] = {{
{to_raw_cstring(stage_sources[stage])}
\t\t}};
"""
        )
    else:
        output_file.write(f"\t\tstatic const char *_{stage}_code = nullptr;\n")


def build_hlsl_header(filename: str, shader: str) -> None:
    stage_sources = _read_stage_sources(shader)
    class_name = _shader_class_name(shader)

    with generated_wrapper(filename) as output_file:
        output_file.write(
            f"""\
#include "servers/rendering/renderer_rd/shader_rd.h"

class {class_name} : public ShaderRD {{
public:
\t{class_name}() {{
"""
            )

        raytracing_stages = ("raygen", "any_hit", "closest_hit", "miss", "intersection")
        if any(stage in stage_sources for stage in raytracing_stages):
            for stage in raytracing_stages:
                _write_stage_code(output_file, stage_sources, stage)
            output_file.write(
                f'\t\tsetup_raytracing(_raygen_code, _any_hit_code, _closest_hit_code, _miss_code, '
                f'_intersection_code, "{class_name}", RD::SHADER_LANGUAGE_HLSL);\n'
            )
        elif "compute" in stage_sources:
            _write_stage_code(output_file, stage_sources, "vertex")
            _write_stage_code(output_file, stage_sources, "fragment")
            _write_stage_code(output_file, stage_sources, "compute")
            output_file.write(
                f'\t\tsetup(_vertex_code, _fragment_code, _compute_code, "{class_name}", '
                "RD::SHADER_LANGUAGE_HLSL);\n"
            )
        else:
            _write_stage_code(output_file, stage_sources, "vertex")
            _write_stage_code(output_file, stage_sources, "fragment")
            _write_stage_code(output_file, stage_sources, "compute")
            output_file.write(
                f'\t\tsetup(_vertex_code, _fragment_code, _compute_code, "{class_name}", '
                "RD::SHADER_LANGUAGE_HLSL);\n"
            )

        output_file.write("\t}\n};\n")


def build_hlsl_headers(target, source, env):
    env.NoCache(target)
    for target_file, source_file in zip(target, source):
        build_hlsl_header(str(target_file), str(source_file))
