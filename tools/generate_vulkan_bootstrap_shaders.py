#!/usr/bin/env python3
"""Generate Vespera's tiny bootstrap Vulkan SPIR-V modules.

These shaders intentionally cover the 1.1 alpha world-material bring-up path:
clip-space position + vertex color/UV/layer -> sampled RGBA texture array. They are assembled directly
from core SPIR-V 1.0 instructions so source builds do not require a shader SDK.
A later production shader pipeline should replace this bootstrap mechanism.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
from typing import Iterable

MAGIC = 0x07230203
VERSION_1_0 = 0x00010000

# SPIR-V core opcodes used here.
OP_MEMORY_MODEL = 14
OP_ENTRY_POINT = 15
OP_EXECUTION_MODE = 16
OP_CAPABILITY = 17
OP_TYPE_VOID = 19
OP_TYPE_INT = 21
OP_TYPE_FLOAT = 22
OP_TYPE_VECTOR = 23
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_TYPE_FUNCTION = 33
OP_CONSTANT = 43
OP_FUNCTION = 54
OP_FUNCTION_END = 56
OP_VARIABLE = 59
OP_LOAD = 61
OP_STORE = 62
OP_ACCESS_CHAIN = 65
OP_DECORATE = 71
OP_MEMBER_DECORATE = 72
OP_COMPOSITE_CONSTRUCT = 80
OP_COMPOSITE_EXTRACT = 81
OP_IMAGE_SAMPLE_IMPLICIT_LOD = 87
OP_FMUL = 133
OP_LABEL = 248
OP_RETURN = 253

# Core enumerants.
CAPABILITY_SHADER = 1
ADDRESSING_LOGICAL = 0
MEMORY_MODEL_GLSL450 = 1
EXECUTION_MODEL_VERTEX = 0
EXECUTION_MODEL_FRAGMENT = 4
EXECUTION_MODE_ORIGIN_UPPER_LEFT = 7
STORAGE_UNIFORM_CONSTANT = 0
STORAGE_INPUT = 1
STORAGE_OUTPUT = 3
DECORATION_BLOCK = 2
DECORATION_BUILT_IN = 11
DECORATION_LOCATION = 30
DECORATION_BINDING = 33
DECORATION_DESCRIPTOR_SET = 34
BUILTIN_POSITION = 0
DIM_2D = 1
IMAGE_FORMAT_UNKNOWN = 0
FUNCTION_CONTROL_NONE = 0


def words_for_string(value: str) -> list[int]:
    raw = value.encode("utf-8") + b"\0"
    raw += b"\0" * ((4 - len(raw) % 4) % 4)
    return list(struct.unpack(f"<{len(raw) // 4}I", raw))


def inst(opcode: int, *operands: int) -> list[int]:
    word_count = 1 + len(operands)
    return [(word_count << 16) | opcode, *operands]


def inst_string(opcode: int, operands: Iterable[int], string: str, suffix: Iterable[int] = ()) -> list[int]:
    payload = [*operands, *words_for_string(string), *suffix]
    return inst(opcode, *payload)


def module(bound: int, instructions: Iterable[Iterable[int]]) -> list[int]:
    words = [MAGIC, VERSION_1_0, 0, bound, 0]
    for instruction in instructions:
        words.extend(instruction)
    return words


def vertex_module() -> list[int]:
    # Clip-space bootstrap vertex path. CPU camera transformation is still used
    # during early Vulkan bring-up, but UV/layer data now reaches the fragment
    # stage so the backend can exercise real sampled textures.
    VOID = 1
    UINT = 2
    FLOAT = 3
    VEC2 = 4
    VEC3 = 5
    VEC4 = 6
    PER_VERTEX = 7
    PTR_IN_VEC4 = 8
    PTR_IN_VEC3 = 9
    PTR_IN_VEC2 = 10
    PTR_IN_FLOAT = 11
    PTR_OUT_PER_VERTEX = 12
    PTR_OUT_VEC4 = 13
    PTR_OUT_VEC3 = 14
    PTR_OUT_VEC2 = 15
    PTR_OUT_FLOAT = 16
    FUNC_TYPE = 17
    ZERO = 18
    IN_POS = 19
    IN_COLOR = 20
    IN_UV = 21
    IN_LAYER = 22
    OUT_PER_VERTEX = 23
    OUT_COLOR = 24
    OUT_UV = 25
    OUT_LAYER = 26
    MAIN = 27
    LABEL = 28
    POS_VALUE = 29
    COLOR_VALUE = 30
    UV_VALUE = 31
    LAYER_VALUE = 32
    POSITION_PTR = 33

    instructions = [
        inst(OP_CAPABILITY, CAPABILITY_SHADER),
        inst(OP_MEMORY_MODEL, ADDRESSING_LOGICAL, MEMORY_MODEL_GLSL450),
        inst_string(
            OP_ENTRY_POINT,
            [EXECUTION_MODEL_VERTEX, MAIN],
            "main",
            [IN_POS, IN_COLOR, IN_UV, IN_LAYER, OUT_PER_VERTEX, OUT_COLOR, OUT_UV, OUT_LAYER]),
        inst(OP_DECORATE, IN_POS, DECORATION_LOCATION, 0),
        inst(OP_DECORATE, IN_COLOR, DECORATION_LOCATION, 1),
        inst(OP_DECORATE, IN_UV, DECORATION_LOCATION, 2),
        inst(OP_DECORATE, IN_LAYER, DECORATION_LOCATION, 3),
        inst(OP_DECORATE, PER_VERTEX, DECORATION_BLOCK),
        inst(OP_MEMBER_DECORATE, PER_VERTEX, 0, DECORATION_BUILT_IN, BUILTIN_POSITION),
        inst(OP_DECORATE, OUT_COLOR, DECORATION_LOCATION, 0),
        inst(OP_DECORATE, OUT_UV, DECORATION_LOCATION, 1),
        inst(OP_DECORATE, OUT_LAYER, DECORATION_LOCATION, 2),
        inst(OP_TYPE_VOID, VOID),
        inst(OP_TYPE_INT, UINT, 32, 0),
        inst(OP_TYPE_FLOAT, FLOAT, 32),
        inst(OP_TYPE_VECTOR, VEC2, FLOAT, 2),
        inst(OP_TYPE_VECTOR, VEC3, FLOAT, 3),
        inst(OP_TYPE_VECTOR, VEC4, FLOAT, 4),
        inst(OP_TYPE_STRUCT, PER_VERTEX, VEC4),
        inst(OP_TYPE_POINTER, PTR_IN_VEC4, STORAGE_INPUT, VEC4),
        inst(OP_TYPE_POINTER, PTR_IN_VEC3, STORAGE_INPUT, VEC3),
        inst(OP_TYPE_POINTER, PTR_IN_VEC2, STORAGE_INPUT, VEC2),
        inst(OP_TYPE_POINTER, PTR_IN_FLOAT, STORAGE_INPUT, FLOAT),
        inst(OP_TYPE_POINTER, PTR_OUT_PER_VERTEX, STORAGE_OUTPUT, PER_VERTEX),
        inst(OP_TYPE_POINTER, PTR_OUT_VEC4, STORAGE_OUTPUT, VEC4),
        inst(OP_TYPE_POINTER, PTR_OUT_VEC3, STORAGE_OUTPUT, VEC3),
        inst(OP_TYPE_POINTER, PTR_OUT_VEC2, STORAGE_OUTPUT, VEC2),
        inst(OP_TYPE_POINTER, PTR_OUT_FLOAT, STORAGE_OUTPUT, FLOAT),
        inst(OP_TYPE_FUNCTION, FUNC_TYPE, VOID),
        inst(OP_CONSTANT, UINT, ZERO, 0),
        inst(OP_VARIABLE, PTR_IN_VEC4, IN_POS, STORAGE_INPUT),
        inst(OP_VARIABLE, PTR_IN_VEC4, IN_COLOR, STORAGE_INPUT),
        inst(OP_VARIABLE, PTR_IN_VEC2, IN_UV, STORAGE_INPUT),
        inst(OP_VARIABLE, PTR_IN_FLOAT, IN_LAYER, STORAGE_INPUT),
        inst(OP_VARIABLE, PTR_OUT_PER_VERTEX, OUT_PER_VERTEX, STORAGE_OUTPUT),
        inst(OP_VARIABLE, PTR_OUT_VEC4, OUT_COLOR, STORAGE_OUTPUT),
        inst(OP_VARIABLE, PTR_OUT_VEC2, OUT_UV, STORAGE_OUTPUT),
        inst(OP_VARIABLE, PTR_OUT_FLOAT, OUT_LAYER, STORAGE_OUTPUT),
        inst(OP_FUNCTION, VOID, MAIN, FUNCTION_CONTROL_NONE, FUNC_TYPE),
        inst(OP_LABEL, LABEL),
        inst(OP_LOAD, VEC4, POS_VALUE, IN_POS),
        inst(OP_ACCESS_CHAIN, PTR_OUT_VEC4, POSITION_PTR, OUT_PER_VERTEX, ZERO),
        inst(OP_STORE, POSITION_PTR, POS_VALUE),
        inst(OP_LOAD, VEC4, COLOR_VALUE, IN_COLOR),
        inst(OP_STORE, OUT_COLOR, COLOR_VALUE),
        inst(OP_LOAD, VEC2, UV_VALUE, IN_UV),
        inst(OP_STORE, OUT_UV, UV_VALUE),
        inst(OP_LOAD, FLOAT, LAYER_VALUE, IN_LAYER),
        inst(OP_STORE, OUT_LAYER, LAYER_VALUE),
        inst(OP_RETURN),
        inst(OP_FUNCTION_END),
    ]
    return module(34, instructions)


def fragment_module() -> list[int]:
    VOID = 1
    FLOAT = 2
    VEC2 = 3
    VEC3 = 4
    VEC4 = 5
    IMAGE = 6
    SAMPLED_IMAGE = 7
    PTR_IN_VEC4 = 8
    PTR_IN_VEC2 = 9
    PTR_IN_FLOAT = 10
    PTR_UNIFORM_SAMPLED_IMAGE = 11
    PTR_OUT_VEC4 = 12
    FUNC_TYPE = 13
    IN_COLOR = 14
    IN_UV = 15
    IN_LAYER = 16
    WORLD_TEXTURES = 17
    OUT_COLOR = 18
    MAIN = 19
    LABEL = 20
    COLOR_VALUE = 21
    UV_VALUE = 22
    LAYER_VALUE = 23
    UV_X = 24
    UV_Y = 25
    COORD = 26
    SAMPLED_IMAGE_VALUE = 27
    SAMPLE = 28
    SHADED = 29

    instructions = [
        inst(OP_CAPABILITY, CAPABILITY_SHADER),
        inst(OP_MEMORY_MODEL, ADDRESSING_LOGICAL, MEMORY_MODEL_GLSL450),
        inst_string(
            OP_ENTRY_POINT,
            [EXECUTION_MODEL_FRAGMENT, MAIN],
            "main",
            [IN_COLOR, IN_UV, IN_LAYER, OUT_COLOR]),
        inst(OP_EXECUTION_MODE, MAIN, EXECUTION_MODE_ORIGIN_UPPER_LEFT),
        inst(OP_DECORATE, IN_COLOR, DECORATION_LOCATION, 0),
        inst(OP_DECORATE, IN_UV, DECORATION_LOCATION, 1),
        inst(OP_DECORATE, IN_LAYER, DECORATION_LOCATION, 2),
        inst(OP_DECORATE, WORLD_TEXTURES, DECORATION_DESCRIPTOR_SET, 0),
        inst(OP_DECORATE, WORLD_TEXTURES, DECORATION_BINDING, 0),
        inst(OP_DECORATE, OUT_COLOR, DECORATION_LOCATION, 0),
        inst(OP_TYPE_VOID, VOID),
        inst(OP_TYPE_FLOAT, FLOAT, 32),
        inst(OP_TYPE_VECTOR, VEC2, FLOAT, 2),
        inst(OP_TYPE_VECTOR, VEC3, FLOAT, 3),
        inst(OP_TYPE_VECTOR, VEC4, FLOAT, 4),
        inst(OP_TYPE_IMAGE, IMAGE, FLOAT, DIM_2D, 0, 1, 0, 1, IMAGE_FORMAT_UNKNOWN),
        inst(OP_TYPE_SAMPLED_IMAGE, SAMPLED_IMAGE, IMAGE),
        inst(OP_TYPE_POINTER, PTR_IN_VEC4, STORAGE_INPUT, VEC4),
        inst(OP_TYPE_POINTER, PTR_IN_VEC2, STORAGE_INPUT, VEC2),
        inst(OP_TYPE_POINTER, PTR_IN_FLOAT, STORAGE_INPUT, FLOAT),
        inst(OP_TYPE_POINTER, PTR_UNIFORM_SAMPLED_IMAGE, STORAGE_UNIFORM_CONSTANT, SAMPLED_IMAGE),
        inst(OP_TYPE_POINTER, PTR_OUT_VEC4, STORAGE_OUTPUT, VEC4),
        inst(OP_TYPE_FUNCTION, FUNC_TYPE, VOID),
        inst(OP_VARIABLE, PTR_IN_VEC4, IN_COLOR, STORAGE_INPUT),
        inst(OP_VARIABLE, PTR_IN_VEC2, IN_UV, STORAGE_INPUT),
        inst(OP_VARIABLE, PTR_IN_FLOAT, IN_LAYER, STORAGE_INPUT),
        inst(OP_VARIABLE, PTR_UNIFORM_SAMPLED_IMAGE, WORLD_TEXTURES, STORAGE_UNIFORM_CONSTANT),
        inst(OP_VARIABLE, PTR_OUT_VEC4, OUT_COLOR, STORAGE_OUTPUT),
        inst(OP_FUNCTION, VOID, MAIN, FUNCTION_CONTROL_NONE, FUNC_TYPE),
        inst(OP_LABEL, LABEL),
        inst(OP_LOAD, VEC4, COLOR_VALUE, IN_COLOR),
        inst(OP_LOAD, VEC2, UV_VALUE, IN_UV),
        inst(OP_LOAD, FLOAT, LAYER_VALUE, IN_LAYER),
        inst(OP_COMPOSITE_EXTRACT, FLOAT, UV_X, UV_VALUE, 0),
        inst(OP_COMPOSITE_EXTRACT, FLOAT, UV_Y, UV_VALUE, 1),
        inst(OP_COMPOSITE_CONSTRUCT, VEC3, COORD, UV_X, UV_Y, LAYER_VALUE),
        inst(OP_LOAD, SAMPLED_IMAGE, SAMPLED_IMAGE_VALUE, WORLD_TEXTURES),
        inst(OP_IMAGE_SAMPLE_IMPLICIT_LOD, VEC4, SAMPLE, SAMPLED_IMAGE_VALUE, COORD),
        inst(OP_FMUL, VEC4, SHADED, SAMPLE, COLOR_VALUE),
        inst(OP_STORE, OUT_COLOR, SHADED),
        inst(OP_RETURN),
        inst(OP_FUNCTION_END),
    ]
    return module(30, instructions)


def validate_structure(words: list[int]) -> None:
    if len(words) < 5 or words[0] != MAGIC:
        raise ValueError("invalid SPIR-V header")
    bound = words[3]
    if bound <= 1:
        raise ValueError("invalid SPIR-V id bound")
    cursor = 5
    while cursor < len(words):
        first = words[cursor]
        count = first >> 16
        if count == 0 or cursor + count > len(words):
            raise ValueError(f"invalid instruction at word {cursor}")
        cursor += count
    if cursor != len(words):
        raise ValueError("instruction stream does not terminate cleanly")


def write_spv(path: pathlib.Path, words: list[int]) -> None:
    validate_structure(words)
    path.write_bytes(struct.pack(f"<{len(words)}I", *words))


def format_cpp_words(words: list[int]) -> str:
    chunks = []
    for i in range(0, len(words), 8):
        line = ", ".join(f"0x{value:08x}u" for value in words[i:i + 8])
        chunks.append("    " + line)
    return ",\n".join(chunks)


def write_header(path: pathlib.Path, vertex: list[int], fragment: list[int]) -> None:
    text = f'''#pragma once

#include <array>
#include <cstdint>

namespace vespera::vulkan_bootstrap_shaders {{

// Generated by tools/generate_vulkan_bootstrap_shaders.py.
// These tiny SPIR-V 1.0 modules serve the Vulkan runtime-parity bootstrap.
// The production material/shader pipeline will replace them.
inline constexpr std::array<std::uint32_t, {len(vertex)}> kVertex = {{
{format_cpp_words(vertex)}
}};

inline constexpr std::array<std::uint32_t, {len(fragment)}> kFragment = {{
{format_cpp_words(fragment)}
}};

}} // namespace vespera::vulkan_bootstrap_shaders
'''
    path.write_text(text, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    shader_dir = root / "engine" / "shaders" / "vulkan"
    header_path = root / "engine" / "src" / "render" / "vulkan" / "vulkan_bootstrap_shaders.hpp"
    shader_dir.mkdir(parents=True, exist_ok=True)
    vertex = vertex_module()
    fragment = fragment_module()
    write_spv(shader_dir / "bootstrap_world.vert.spv", vertex)
    write_spv(shader_dir / "bootstrap_world.frag.spv", fragment)
    write_header(header_path, vertex, fragment)
    print(f"generated {len(vertex) * 4} byte vertex module and {len(fragment) * 4} byte fragment module")


if __name__ == "__main__":
    main()
