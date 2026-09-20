#!/usr/bin/env python3
"""Generate Vespera's Vulkan per-pixel world-lighting SPIR-V modules.

The engine intentionally keeps these small shaders checked in as SPIR-V so a
normal source build does not require the Vulkan SDK or a shader compiler.  The
modules are assembled from core SPIR-V 1.0 + GLSL.std.450 instructions and are
validated by spirv-val in Linux CI.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
from typing import Iterable

MAGIC = 0x07230203
VERSION_1_0 = 0x00010000

OP_EXT_INST_IMPORT = 11
OP_EXT_INST = 12
OP_MEMORY_MODEL = 14
OP_ENTRY_POINT = 15
OP_EXECUTION_MODE = 16
OP_CAPABILITY = 17
OP_TYPE_VOID = 19
OP_TYPE_BOOL = 20
OP_TYPE_INT = 21
OP_TYPE_FLOAT = 22
OP_TYPE_VECTOR = 23
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_ARRAY = 28
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_TYPE_FUNCTION = 33
OP_CONSTANT = 43
OP_CONSTANT_COMPOSITE = 44
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
OP_FADD = 129
OP_FSUB = 131
OP_FMUL = 133
OP_FDIV = 136
OP_VECTOR_TIMES_SCALAR = 142
OP_DOT = 148
OP_FORD_LESS_THAN = 184
OP_SELECTION_MERGE = 247
OP_LABEL = 248
OP_BRANCH = 249
OP_BRANCH_CONDITIONAL = 250
OP_KILL = 252
OP_RETURN = 253

CAPABILITY_SHADER = 1
ADDRESSING_LOGICAL = 0
MEMORY_MODEL_GLSL450 = 1
EXECUTION_MODEL_VERTEX = 0
EXECUTION_MODEL_FRAGMENT = 4
EXECUTION_MODE_ORIGIN_UPPER_LEFT = 7
STORAGE_UNIFORM_CONSTANT = 0
STORAGE_INPUT = 1
STORAGE_UNIFORM = 2
STORAGE_OUTPUT = 3
DECORATION_BLOCK = 2
DECORATION_ARRAY_STRIDE = 6
DECORATION_BUILT_IN = 11
DECORATION_FLAT = 14
DECORATION_LOCATION = 30
DECORATION_BINDING = 33
DECORATION_DESCRIPTOR_SET = 34
DECORATION_OFFSET = 35
BUILTIN_POSITION = 0
DIM_2D = 1
IMAGE_FORMAT_UNKNOWN = 0
FUNCTION_CONTROL_NONE = 0
SELECTION_CONTROL_NONE = 0

GLSL_STD_450_SQRT = 31
GLSL_STD_450_FMAX = 40
GLSL_STD_450_FCLAMP = 43


def words_for_string(value: str) -> list[int]:
    raw = value.encode("utf-8") + b"\0"
    raw += b"\0" * ((4 - len(raw) % 4) % 4)
    return list(struct.unpack(f"<{len(raw) // 4}I", raw))


def inst(opcode: int, *operands: int) -> list[int]:
    return [((1 + len(operands)) << 16) | opcode, *operands]


def inst_string(opcode: int, operands: Iterable[int], value: str, suffix: Iterable[int] = ()) -> list[int]:
    payload = [*operands, *words_for_string(value), *suffix]
    return inst(opcode, *payload)


def f32(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


class Ids:
    def __init__(self) -> None:
        self.next = 1

    def take(self) -> int:
        result = self.next
        self.next += 1
        return result


def module(bound: int, sections: Iterable[Iterable[Iterable[int]]]) -> list[int]:
    words = [MAGIC, VERSION_1_0, 0, bound, 0]
    for section in sections:
        for instruction in section:
            words.extend(instruction)
    return words


def vertex_module() -> list[int]:
    ids = Ids()
    void = ids.take(); uint = ids.take(); float_t = ids.take(); vec2 = ids.take(); vec3 = ids.take(); vec4 = ids.take()
    per_vertex = ids.take()
    ptr_in_vec4 = ids.take(); ptr_in_vec3 = ids.take(); ptr_in_vec2 = ids.take(); ptr_in_float = ids.take()
    ptr_out_per_vertex = ids.take(); ptr_out_vec4 = ids.take(); ptr_out_vec3 = ids.take(); ptr_out_vec2 = ids.take(); ptr_out_float = ids.take()
    func_type = ids.take(); zero = ids.take()

    in_pos = ids.take(); in_color = ids.take(); in_uv = ids.take(); in_layer = ids.take(); in_world = ids.take(); in_emission = ids.take(); in_material = ids.take()
    out_per_vertex = ids.take(); out_color = ids.take(); out_uv = ids.take(); out_layer = ids.take(); out_world = ids.take(); out_emission = ids.take(); out_material = ids.take()
    main = ids.take(); label = ids.take()

    capabilities = [inst(OP_CAPABILITY, CAPABILITY_SHADER)]
    memory = [inst(OP_MEMORY_MODEL, ADDRESSING_LOGICAL, MEMORY_MODEL_GLSL450)]
    entry = [inst_string(OP_ENTRY_POINT, [EXECUTION_MODEL_VERTEX, main], "main", [
        in_pos, in_color, in_uv, in_layer, in_world, in_emission, in_material,
        out_per_vertex, out_color, out_uv, out_layer, out_world, out_emission, out_material,
    ])]
    annotations = [
        inst(OP_DECORATE, in_pos, DECORATION_LOCATION, 0),
        inst(OP_DECORATE, in_color, DECORATION_LOCATION, 1),
        inst(OP_DECORATE, in_uv, DECORATION_LOCATION, 2),
        inst(OP_DECORATE, in_layer, DECORATION_LOCATION, 3),
        inst(OP_DECORATE, in_world, DECORATION_LOCATION, 4),
        inst(OP_DECORATE, in_emission, DECORATION_LOCATION, 5),
        inst(OP_DECORATE, in_material, DECORATION_LOCATION, 6),
        inst(OP_DECORATE, per_vertex, DECORATION_BLOCK),
        inst(OP_MEMBER_DECORATE, per_vertex, 0, DECORATION_BUILT_IN, BUILTIN_POSITION),
        inst(OP_DECORATE, out_color, DECORATION_LOCATION, 0),
        inst(OP_DECORATE, out_uv, DECORATION_LOCATION, 1),
        inst(OP_DECORATE, out_layer, DECORATION_LOCATION, 2), inst(OP_DECORATE, out_layer, DECORATION_FLAT),
        inst(OP_DECORATE, out_world, DECORATION_LOCATION, 3),
        inst(OP_DECORATE, out_emission, DECORATION_LOCATION, 4), inst(OP_DECORATE, out_emission, DECORATION_FLAT),
        inst(OP_DECORATE, out_material, DECORATION_LOCATION, 5), inst(OP_DECORATE, out_material, DECORATION_FLAT),
    ]
    types = [
        inst(OP_TYPE_VOID, void), inst(OP_TYPE_INT, uint, 32, 0), inst(OP_TYPE_FLOAT, float_t, 32),
        inst(OP_TYPE_VECTOR, vec2, float_t, 2), inst(OP_TYPE_VECTOR, vec3, float_t, 3), inst(OP_TYPE_VECTOR, vec4, float_t, 4),
        inst(OP_TYPE_STRUCT, per_vertex, vec4),
        inst(OP_TYPE_POINTER, ptr_in_vec4, STORAGE_INPUT, vec4), inst(OP_TYPE_POINTER, ptr_in_vec3, STORAGE_INPUT, vec3),
        inst(OP_TYPE_POINTER, ptr_in_vec2, STORAGE_INPUT, vec2), inst(OP_TYPE_POINTER, ptr_in_float, STORAGE_INPUT, float_t),
        inst(OP_TYPE_POINTER, ptr_out_per_vertex, STORAGE_OUTPUT, per_vertex), inst(OP_TYPE_POINTER, ptr_out_vec4, STORAGE_OUTPUT, vec4),
        inst(OP_TYPE_POINTER, ptr_out_vec3, STORAGE_OUTPUT, vec3), inst(OP_TYPE_POINTER, ptr_out_vec2, STORAGE_OUTPUT, vec2),
        inst(OP_TYPE_POINTER, ptr_out_float, STORAGE_OUTPUT, float_t), inst(OP_TYPE_FUNCTION, func_type, void),
        inst(OP_CONSTANT, uint, zero, 0),
        inst(OP_VARIABLE, ptr_in_vec4, in_pos, STORAGE_INPUT), inst(OP_VARIABLE, ptr_in_vec4, in_color, STORAGE_INPUT),
        inst(OP_VARIABLE, ptr_in_vec2, in_uv, STORAGE_INPUT), inst(OP_VARIABLE, ptr_in_float, in_layer, STORAGE_INPUT),
        inst(OP_VARIABLE, ptr_in_vec3, in_world, STORAGE_INPUT), inst(OP_VARIABLE, ptr_in_vec4, in_emission, STORAGE_INPUT),
        inst(OP_VARIABLE, ptr_in_vec4, in_material, STORAGE_INPUT), inst(OP_VARIABLE, ptr_out_per_vertex, out_per_vertex, STORAGE_OUTPUT),
        inst(OP_VARIABLE, ptr_out_vec4, out_color, STORAGE_OUTPUT), inst(OP_VARIABLE, ptr_out_vec2, out_uv, STORAGE_OUTPUT),
        inst(OP_VARIABLE, ptr_out_float, out_layer, STORAGE_OUTPUT), inst(OP_VARIABLE, ptr_out_vec3, out_world, STORAGE_OUTPUT),
        inst(OP_VARIABLE, ptr_out_vec4, out_emission, STORAGE_OUTPUT), inst(OP_VARIABLE, ptr_out_vec4, out_material, STORAGE_OUTPUT),
    ]

    body: list[list[int]] = [inst(OP_FUNCTION, void, main, FUNCTION_CONTROL_NONE, func_type), inst(OP_LABEL, label)]
    pos_value = ids.take(); position_ptr = ids.take()
    body += [inst(OP_LOAD, vec4, pos_value, in_pos), inst(OP_ACCESS_CHAIN, ptr_out_vec4, position_ptr, out_per_vertex, zero), inst(OP_STORE, position_ptr, pos_value)]
    copies = [
        (vec4, in_color, out_color), (vec2, in_uv, out_uv), (float_t, in_layer, out_layer),
        (vec3, in_world, out_world), (vec4, in_emission, out_emission), (vec4, in_material, out_material),
    ]
    for type_id, source, dest in copies:
        value = ids.take(); body += [inst(OP_LOAD, type_id, value, source), inst(OP_STORE, dest, value)]
    body += [inst(OP_RETURN), inst(OP_FUNCTION_END)]
    return module(ids.next, [capabilities, memory, entry, annotations, types, body])


def fragment_module() -> list[int]:
    ids = Ids()
    void = ids.take(); bool_t = ids.take(); uint = ids.take(); float_t = ids.take(); vec2 = ids.take(); vec3 = ids.take(); vec4 = ids.take()
    image = ids.take(); sampled_image = ids.take()
    point_light = ids.take(); array32 = ids.take(); lighting = ids.take()
    ptr_in_vec4 = ids.take(); ptr_in_vec3 = ids.take(); ptr_in_vec2 = ids.take(); ptr_in_float = ids.take()
    ptr_uniform_constant_sampled = ids.take(); ptr_uniform_lighting = ids.take(); ptr_uniform_vec4 = ids.take(); ptr_out_vec4 = ids.take()
    func_type = ids.take()
    uint_indices = [ids.take() for _ in range(32)]
    c_u0 = uint_indices[0]; c_u1 = uint_indices[1]; c_u32 = ids.take()
    c_f0 = ids.take(); c_f1 = ids.take(); c_fe = ids.take()
    c_v3_zero = ids.take(); c_v3_one = ids.take()

    in_color = ids.take(); in_uv = ids.take(); in_layer = ids.take(); in_world = ids.take(); in_emission = ids.take(); in_material = ids.take()
    world_textures = ids.take(); lighting_data = ids.take(); out_color = ids.take()
    main = ids.take(); label = ids.take(); glsl = ids.take()

    capabilities = [inst(OP_CAPABILITY, CAPABILITY_SHADER)]
    imports = [inst_string(OP_EXT_INST_IMPORT, [glsl], "GLSL.std.450")]
    memory = [inst(OP_MEMORY_MODEL, ADDRESSING_LOGICAL, MEMORY_MODEL_GLSL450)]
    entry = [
        inst_string(OP_ENTRY_POINT, [EXECUTION_MODEL_FRAGMENT, main], "main", [in_color, in_uv, in_layer, in_world, in_emission, in_material, out_color]),
        inst(OP_EXECUTION_MODE, main, EXECUTION_MODE_ORIGIN_UPPER_LEFT),
    ]
    annotations = [
        inst(OP_DECORATE, in_color, DECORATION_LOCATION, 0), inst(OP_DECORATE, in_uv, DECORATION_LOCATION, 1),
        inst(OP_DECORATE, in_layer, DECORATION_LOCATION, 2), inst(OP_DECORATE, in_layer, DECORATION_FLAT), inst(OP_DECORATE, in_world, DECORATION_LOCATION, 3),
        inst(OP_DECORATE, in_emission, DECORATION_LOCATION, 4), inst(OP_DECORATE, in_emission, DECORATION_FLAT),
        inst(OP_DECORATE, in_material, DECORATION_LOCATION, 5), inst(OP_DECORATE, in_material, DECORATION_FLAT),
        inst(OP_DECORATE, world_textures, DECORATION_DESCRIPTOR_SET, 0), inst(OP_DECORATE, world_textures, DECORATION_BINDING, 0),
        inst(OP_DECORATE, lighting_data, DECORATION_DESCRIPTOR_SET, 0), inst(OP_DECORATE, lighting_data, DECORATION_BINDING, 1),
        inst(OP_DECORATE, out_color, DECORATION_LOCATION, 0),
        inst(OP_MEMBER_DECORATE, point_light, 0, DECORATION_OFFSET, 0), inst(OP_MEMBER_DECORATE, point_light, 1, DECORATION_OFFSET, 16),
        inst(OP_DECORATE, array32, DECORATION_ARRAY_STRIDE, 32),
        inst(OP_DECORATE, lighting, DECORATION_BLOCK), inst(OP_MEMBER_DECORATE, lighting, 0, DECORATION_OFFSET, 0), inst(OP_MEMBER_DECORATE, lighting, 1, DECORATION_OFFSET, 16),
    ]
    types = [
        inst(OP_TYPE_VOID, void), inst(OP_TYPE_BOOL, bool_t), inst(OP_TYPE_INT, uint, 32, 0), inst(OP_TYPE_FLOAT, float_t, 32),
        inst(OP_TYPE_VECTOR, vec2, float_t, 2), inst(OP_TYPE_VECTOR, vec3, float_t, 3), inst(OP_TYPE_VECTOR, vec4, float_t, 4),
        inst(OP_TYPE_IMAGE, image, float_t, DIM_2D, 0, 1, 0, 1, IMAGE_FORMAT_UNKNOWN), inst(OP_TYPE_SAMPLED_IMAGE, sampled_image, image),
        *[inst(OP_CONSTANT, uint, const_id, value) for value, const_id in enumerate(uint_indices)], inst(OP_CONSTANT, uint, c_u32, 32),
        inst(OP_TYPE_STRUCT, point_light, vec4, vec4), inst(OP_TYPE_ARRAY, array32, point_light, c_u32), inst(OP_TYPE_STRUCT, lighting, vec4, array32),
        inst(OP_TYPE_POINTER, ptr_in_vec4, STORAGE_INPUT, vec4), inst(OP_TYPE_POINTER, ptr_in_vec3, STORAGE_INPUT, vec3),
        inst(OP_TYPE_POINTER, ptr_in_vec2, STORAGE_INPUT, vec2), inst(OP_TYPE_POINTER, ptr_in_float, STORAGE_INPUT, float_t),
        inst(OP_TYPE_POINTER, ptr_uniform_constant_sampled, STORAGE_UNIFORM_CONSTANT, sampled_image), inst(OP_TYPE_POINTER, ptr_uniform_lighting, STORAGE_UNIFORM, lighting),
        inst(OP_TYPE_POINTER, ptr_uniform_vec4, STORAGE_UNIFORM, vec4), inst(OP_TYPE_POINTER, ptr_out_vec4, STORAGE_OUTPUT, vec4), inst(OP_TYPE_FUNCTION, func_type, void),
        inst(OP_CONSTANT, float_t, c_f0, f32(0.0)), inst(OP_CONSTANT, float_t, c_f1, f32(1.0)), inst(OP_CONSTANT, float_t, c_fe, f32(0.0001)),
        inst(OP_CONSTANT_COMPOSITE, vec3, c_v3_zero, c_f0, c_f0, c_f0),
        inst(OP_CONSTANT_COMPOSITE, vec3, c_v3_one, c_f1, c_f1, c_f1),
        inst(OP_VARIABLE, ptr_in_vec4, in_color, STORAGE_INPUT), inst(OP_VARIABLE, ptr_in_vec2, in_uv, STORAGE_INPUT),
        inst(OP_VARIABLE, ptr_in_float, in_layer, STORAGE_INPUT), inst(OP_VARIABLE, ptr_in_vec3, in_world, STORAGE_INPUT),
        inst(OP_VARIABLE, ptr_in_vec4, in_emission, STORAGE_INPUT), inst(OP_VARIABLE, ptr_in_vec4, in_material, STORAGE_INPUT),
        inst(OP_VARIABLE, ptr_uniform_constant_sampled, world_textures, STORAGE_UNIFORM_CONSTANT), inst(OP_VARIABLE, ptr_uniform_lighting, lighting_data, STORAGE_UNIFORM),
        inst(OP_VARIABLE, ptr_out_vec4, out_color, STORAGE_OUTPUT),
    ]

    b: list[list[int]] = [inst(OP_FUNCTION, void, main, FUNCTION_CONTROL_NONE, func_type), inst(OP_LABEL, label)]
    color_v=ids.take(); uv_v=ids.take(); layer_v=ids.take(); world_v=ids.take(); emission_v=ids.take(); material_v=ids.take()
    b += [inst(OP_LOAD, vec4, color_v, in_color), inst(OP_LOAD, vec2, uv_v, in_uv), inst(OP_LOAD, float_t, layer_v, in_layer),
          inst(OP_LOAD, vec3, world_v, in_world), inst(OP_LOAD, vec4, emission_v, in_emission), inst(OP_LOAD, vec4, material_v, in_material)]
    ux=ids.take(); uy=ids.take(); coord=ids.take(); sampler_v=ids.take(); sample=ids.take()
    b += [inst(OP_COMPOSITE_EXTRACT, float_t, ux, uv_v, 0), inst(OP_COMPOSITE_EXTRACT, float_t, uy, uv_v, 1),
          inst(OP_COMPOSITE_CONSTRUCT, vec3, coord, ux, uy, layer_v), inst(OP_LOAD, sampled_image, sampler_v, world_textures),
          inst(OP_IMAGE_SAMPLE_IMPLICIT_LOD, vec4, sample, sampler_v, coord)]

    # base RGB and alpha
    sample_rgb_parts=[]; color_rgb_parts=[]
    for component in range(3):
        s=ids.take(); c=ids.take(); sample_rgb_parts.append(s); color_rgb_parts.append(c)
        b += [inst(OP_COMPOSITE_EXTRACT, float_t, s, sample, component), inst(OP_COMPOSITE_EXTRACT, float_t, c, color_v, component)]
    sample_rgb=ids.take(); color_rgb=ids.take(); base_rgb=ids.take()
    b += [inst(OP_COMPOSITE_CONSTRUCT, vec3, sample_rgb, *sample_rgb_parts), inst(OP_COMPOSITE_CONSTRUCT, vec3, color_rgb, *color_rgb_parts), inst(OP_FMUL, vec3, base_rgb, sample_rgb, color_rgb)]
    sample_a=ids.take(); color_a=ids.take(); alpha=ids.take()
    b += [inst(OP_COMPOSITE_EXTRACT, float_t, sample_a, sample, 3), inst(OP_COMPOSITE_EXTRACT, float_t, color_a, color_v, 3), inst(OP_FMUL, float_t, alpha, sample_a, color_a)]

    # Match D3D12 material cutout semantics: discard fragments whose combined
    # texture/tint alpha is below the authored alpha-cutoff threshold.
    cutoff_raw=ids.take(); cutoff=ids.take(); discard_condition=ids.take(); keep_label=ids.take(); kill_label=ids.take()
    b += [inst(OP_COMPOSITE_EXTRACT, float_t, cutoff_raw, material_v, 2),
          inst(OP_EXT_INST, float_t, cutoff, glsl, GLSL_STD_450_FCLAMP, cutoff_raw, c_f0, c_f1),
          inst(OP_FORD_LESS_THAN, bool_t, discard_condition, alpha, cutoff),
          inst(OP_SELECTION_MERGE, keep_label, SELECTION_CONTROL_NONE),
          inst(OP_BRANCH_CONDITIONAL, discard_condition, kill_label, keep_label),
          inst(OP_LABEL, kill_label), inst(OP_KILL), inst(OP_LABEL, keep_label)]

    # Ambient starts the light accumulator.
    ambient_ptr=ids.take(); ambient4=ids.take(); ambient_parts=[]
    b += [inst(OP_ACCESS_CHAIN, ptr_uniform_vec4, ambient_ptr, lighting_data, c_u0), inst(OP_LOAD, vec4, ambient4, ambient_ptr)]
    for component in range(3):
        x=ids.take(); ambient_parts.append(x); b.append(inst(OP_COMPOSITE_EXTRACT, float_t, x, ambient4, component))
    ambient3=ids.take(); ambient_a=ids.take(); lit=ids.take()
    b += [inst(OP_COMPOSITE_CONSTRUCT, vec3, ambient3, *ambient_parts), inst(OP_COMPOSITE_EXTRACT, float_t, ambient_a, ambient4, 3),
          inst(OP_VECTOR_TIMES_SCALAR, vec3, lit, ambient3, ambient_a)]

    # Unroll the fixed-size light array. Unused slots carry zero intensity and a
    # radius of 1, so the shader needs no loop/control-flow or light-count branch.
    for i in range(32):
        ci=uint_indices[i]
        pos_ptr=ids.take(); col_ptr=ids.take(); pos4=ids.take(); col4=ids.take()
        b += [inst(OP_ACCESS_CHAIN, ptr_uniform_vec4, pos_ptr, lighting_data, c_u1, ci, c_u0),
              inst(OP_ACCESS_CHAIN, ptr_uniform_vec4, col_ptr, lighting_data, c_u1, ci, c_u1),
              inst(OP_LOAD, vec4, pos4, pos_ptr), inst(OP_LOAD, vec4, col4, col_ptr)]
        pos_parts=[]; col_parts=[]
        for component in range(3):
            p=ids.take(); c=ids.take(); pos_parts.append(p); col_parts.append(c)
            b += [inst(OP_COMPOSITE_EXTRACT, float_t, p, pos4, component), inst(OP_COMPOSITE_EXTRACT, float_t, c, col4, component)]
        pos3=ids.take(); col3=ids.take(); radius=ids.take(); intensity=ids.take(); delta=ids.take(); dist2=ids.take(); distance=ids.take(); safe_radius=ids.take(); ratio=ids.take(); falloff=ids.take(); attenuation=ids.take(); attenuation2=ids.take(); color_intensity=ids.take(); contribution=ids.take(); next_lit=ids.take()
        b += [inst(OP_COMPOSITE_CONSTRUCT, vec3, pos3, *pos_parts), inst(OP_COMPOSITE_CONSTRUCT, vec3, col3, *col_parts),
              inst(OP_COMPOSITE_EXTRACT, float_t, radius, pos4, 3), inst(OP_COMPOSITE_EXTRACT, float_t, intensity, col4, 3),
              inst(OP_FSUB, vec3, delta, world_v, pos3), inst(OP_DOT, float_t, dist2, delta, delta),
              inst(OP_EXT_INST, float_t, distance, glsl, GLSL_STD_450_SQRT, dist2),
              inst(OP_EXT_INST, float_t, safe_radius, glsl, GLSL_STD_450_FMAX, radius, c_fe),
              inst(OP_FDIV, float_t, ratio, distance, safe_radius), inst(OP_FSUB, float_t, falloff, c_f1, ratio),
              inst(OP_EXT_INST, float_t, attenuation, glsl, GLSL_STD_450_FCLAMP, falloff, c_f0, c_f1),
              inst(OP_FMUL, float_t, attenuation2, attenuation, attenuation),
              inst(OP_VECTOR_TIMES_SCALAR, vec3, color_intensity, col3, intensity),
              inst(OP_VECTOR_TIMES_SCALAR, vec3, contribution, color_intensity, attenuation2),
              inst(OP_FADD, vec3, next_lit, lit, contribution)]
        lit = next_lit

    # Unlit materials mix from computed light toward white using material.x.
    unlit_raw=ids.take(); unlit=ids.take(); lit_factor=ids.take(); lit_scaled=ids.take(); unlit_scaled=ids.take(); lighting_final=ids.take()
    b += [inst(OP_COMPOSITE_EXTRACT, float_t, unlit_raw, material_v, 0),
          inst(OP_EXT_INST, float_t, unlit, glsl, GLSL_STD_450_FCLAMP, unlit_raw, c_f0, c_f1),
          inst(OP_FSUB, float_t, lit_factor, c_f1, unlit), inst(OP_VECTOR_TIMES_SCALAR, vec3, lit_scaled, lit, lit_factor),
          inst(OP_VECTOR_TIMES_SCALAR, vec3, unlit_scaled, c_v3_one, unlit), inst(OP_FADD, vec3, lighting_final, lit_scaled, unlit_scaled)]

    lit_base=ids.take(); b.append(inst(OP_FMUL, vec3, lit_base, base_rgb, lighting_final))
    emission_parts=[]
    for component in range(3):
        e=ids.take(); emission_parts.append(e); b.append(inst(OP_COMPOSITE_EXTRACT, float_t, e, emission_v, component))
    emission3=ids.take(); emission_strength_raw=ids.take(); emission_strength=ids.take(); emitted=ids.take(); final_rgb=ids.take(); final_rgb_clamped=ids.take()
    b += [inst(OP_COMPOSITE_CONSTRUCT, vec3, emission3, *emission_parts), inst(OP_COMPOSITE_EXTRACT, float_t, emission_strength_raw, emission_v, 3),
          inst(OP_EXT_INST, float_t, emission_strength, glsl, GLSL_STD_450_FMAX, emission_strength_raw, c_f0),
          inst(OP_VECTOR_TIMES_SCALAR, vec3, emitted, emission3, emission_strength), inst(OP_FADD, vec3, final_rgb, lit_base, emitted),
          inst(OP_EXT_INST, vec3, final_rgb_clamped, glsl, GLSL_STD_450_FCLAMP, final_rgb, c_v3_zero, c_v3_one)]
    final_parts=[]
    for component in range(3):
        p=ids.take(); final_parts.append(p); b.append(inst(OP_COMPOSITE_EXTRACT, float_t, p, final_rgb_clamped, component))
    result=ids.take(); b += [inst(OP_COMPOSITE_CONSTRUCT, vec4, result, *final_parts, alpha), inst(OP_STORE, out_color, result), inst(OP_RETURN), inst(OP_FUNCTION_END)]
    return module(ids.next, [capabilities, imports, memory, entry, annotations, types, b])


def validate_structure(words: list[int]) -> None:
    if len(words) < 5 or words[0] != MAGIC:
        raise ValueError("invalid SPIR-V header")
    if words[3] <= 1:
        raise ValueError("invalid SPIR-V id bound")
    cursor = 5
    while cursor < len(words):
        count = words[cursor] >> 16
        if count == 0 or cursor + count > len(words):
            raise ValueError(f"invalid instruction at word {cursor}")
        cursor += count
    if cursor != len(words):
        raise ValueError("instruction stream does not terminate cleanly")


def write_spv(path: pathlib.Path, words: list[int]) -> None:
    validate_structure(words)
    path.write_bytes(struct.pack(f"<{len(words)}I", *words))


def format_cpp(words: list[int]) -> str:
    return ",\n".join("    " + ", ".join(f"0x{value:08x}u" for value in words[i:i+8]) for i in range(0, len(words), 8))


def write_header(path: pathlib.Path, vertex: list[int], fragment: list[int]) -> None:
    path.write_text(f'''#pragma once

#include <array>
#include <cstdint>

namespace vespera::vulkan_lighting_shaders {{

// Generated by tools/generate_vulkan_lighting_shaders.py.
// Per-pixel Vulkan point-light/material parity shader used by the 1.1 renderer.
inline constexpr std::array<std::uint32_t, {len(vertex)}> kVertex = {{
{format_cpp(vertex)}
}};

inline constexpr std::array<std::uint32_t, {len(fragment)}> kFragment = {{
{format_cpp(fragment)}
}};

}} // namespace vespera::vulkan_lighting_shaders
''', encoding='utf-8')


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    shader_dir = root / 'engine' / 'shaders' / 'vulkan'
    shader_dir.mkdir(parents=True, exist_ok=True)
    vertex = vertex_module(); fragment = fragment_module()
    write_spv(shader_dir / 'lighting_world.vert.spv', vertex)
    write_spv(shader_dir / 'lighting_world.frag.spv', fragment)
    write_header(root / 'engine' / 'src' / 'render' / 'vulkan' / 'vulkan_lighting_shaders.hpp', vertex, fragment)
    print(f'generated per-pixel Vulkan lighting shaders: {len(vertex)*4} byte vertex, {len(fragment)*4} byte fragment')


if __name__ == '__main__':
    main()
