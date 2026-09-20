#!/usr/bin/env python3
"""Vespera MCP developer bridge (1.1.0).

This process speaks MCP JSON-RPC over stdio and forwards validated tool calls to
an already-running Vespera Editor through the localhost-only VAP v1 transport.
The editor owns all mutation on its main thread; this bridge never edits scene
or asset files directly.
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import threading
import time
import urllib.parse
from typing import Any

SERVER_NAME = "vespera-editor"
SERVER_VERSION = "1.1.0"
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 46787
DEFAULT_RUNTIME_PORT = 46788

TOOLS: list[dict[str, Any]] = [
    {"name":"vespera_get_bridge_info","description":"Read the MCP bridge version/tool count and live editor/runtime endpoints. Call this at the start of QA to detect stale MCP registrations.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_get_state","description":"Read the active Vespera project/scene, edit/play state and selection.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_list_entities","description":"List authored scene entities with IDs, hierarchy and component summary.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_get_entity","description":"Read one entity including hierarchy, transforms and components.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1}},"required":["entity_id"],"additionalProperties":False}},
    {"name":"vespera_list_assets","description":"List project assets with stable IDs, kinds and paths.","inputSchema":{"type":"object","properties":{"kind":{"type":"string"}},"additionalProperties":False}},
    {"name":"vespera_get_build_manifest","description":"Read the deterministic standalone-build asset closure and validation state.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_export_project","description":"Export/package the current project through Vespera's deterministic build closure and optionally launch the packaged runtime.","inputSchema":{"type":"object","properties":{"output_directory":{"type":"string","description":"Optional project-relative or absolute output directory; project Build Settings are used when omitted."},"configuration":{"type":"string","enum":["Debug","Development","Release"],"default":"Development"},"managed_deployment":{"type":"string","enum":["framework-dependent","portable"]},"runtime_executable":{"type":"string"},"managed_directory":{"type":"string"},"clean":{"type":"boolean","default":True},"launch":{"type":"boolean","default":False},"runtime_automation":{"type":"boolean","default":False,"description":"When launch=true, start the packaged reference runtime with its localhost-only QA automation endpoint on port 46788."}},"additionalProperties":False}},
    {"name":"vespera_get_console","description":"Read recent Vespera Editor Console entries for automated QA/failure diagnosis.","inputSchema":{"type":"object","properties":{"limit":{"type":"integer","minimum":1,"maximum":100,"default":50}},"additionalProperties":False}},
    {"name":"vespera_get_command_log","description":"Read recent typed editor command audit records, success state and transaction IDs.","inputSchema":{"type":"object","properties":{"limit":{"type":"integer","minimum":1,"maximum":100,"default":50}},"additionalProperties":False}},
    {"name":"vespera_list_capabilities","description":"Read the editor extension API version and registered automation/extension commands.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_select_entity","description":"Select an authored entity by scene object ID.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1}},"required":["entity_id"],"additionalProperties":False}},
    {"name":"vespera_create_entity","description":"Create an empty, sprite, trigger or point-light entity.","inputSchema":{"type":"object","properties":{"kind":{"type":"string","enum":["empty","sprite","trigger","point_light"],"default":"empty"},"name":{"type":"string"}},"additionalProperties":False}},
    {"name":"vespera_create_primitive","description":"Create an undoable Cube, Plane, Cylinder or Sphere in the edit scene.","inputSchema":{"type":"object","properties":{"primitive":{"type":"string","enum":["cube","plane","cylinder","sphere"]}},"required":["primitive"],"additionalProperties":False}},
    {"name":"vespera_duplicate_entity","description":"Duplicate one entity through the editor transaction path.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1}},"required":["entity_id"],"additionalProperties":False}},
    {"name":"vespera_delete_entity","description":"Delete one entity and its descendants through undoable editor history.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1}},"required":["entity_id"],"additionalProperties":False}},
    {"name":"vespera_reparent_entity","description":"Parent/reparent/unparent an entity while preserving world transform.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"parent_id":{"type":"integer","minimum":0,"default":0}},"required":["entity_id"],"additionalProperties":False}},
    {"name":"vespera_set_transform","description":"Set position/rotation/scale for an entity as one undoable editor transaction.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"space":{"type":"string","enum":["world","local"],"default":"world"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_degrees":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"scale":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["entity_id"],"additionalProperties":False}},
    {"name":"vespera_add_component","description":"Add a built-in component by semantic component key.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"component":{"type":"string"}},"required":["entity_id","component"],"additionalProperties":False}},
    {"name":"vespera_remove_component","description":"Remove a removable built-in component by semantic component key.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"component":{"type":"string"}},"required":["entity_id","component"],"additionalProperties":False}},
    {"name":"vespera_get_component_property","description":"Read one reflected built-in component property.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"component":{"type":"string"},"property":{"type":"string"}},"required":["entity_id","component","property"],"additionalProperties":False}},
    {"name":"vespera_set_component_property","description":"Set one reflected built-in component property through undoable history. Values are strings or numeric arrays and are converted by reflected type.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"component":{"type":"string"},"property":{"type":"string"},"value":{}},"required":["entity_id","component","property","value"],"additionalProperties":False}},
    {"name":"vespera_instantiate_prefab","description":"Instantiate a project prefab by stable asset id or project-relative path.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"additionalProperties":False}},
    {"name":"vespera_assign_material","description":"Assign a project Material to an entity Mesh Renderer.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"asset_id":{"type":"string"},"path":{"type":"string"}},"required":["entity_id"],"additionalProperties":False}},
    {"name":"vespera_create_material","description":"Create a Lit or Unlit .slmat asset inside project Assets.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"shader":{"type":"string","enum":["lit","unlit"],"default":"lit"}},"additionalProperties":False}},
    {"name":"vespera_refresh_assets","description":"Refresh/reimport the project AssetCatalog.","inputSchema":{"type":"object","properties":{"force_rehash":{"type":"boolean","default":False}},"additionalProperties":False}},
    {"name":"vespera_validate_scene","description":"Run Vespera scene validation and return error/warning counts.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_undo","description":"Undo the latest edit-scene transaction.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_redo","description":"Redo the latest edit-scene transaction.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_play","description":"Control Vespera in-editor Play Mode.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["enter","stop","pause","resume","step"]}},"required":["action"],"additionalProperties":False}},
    {"name":"vespera_inject_input_action","description":"Inject/override a semantic named Input action into active Editor Play Mode without OS key synthesis.","inputSchema":{"type":"object","properties":{"action":{"type":"string","minLength":1},"phase":{"type":"string","enum":["set","press","release","clear"],"default":"set"},"value":{"type":"number","minimum":-1,"maximum":1,"default":1}},"required":["action"],"additionalProperties":False}},
    {"name":"vespera_pointer_event","description":"Inject a deterministic runtime-UI pointer event into Editor Play Mode.","inputSchema":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"width":{"type":"integer","minimum":1,"maximum":16384},"height":{"type":"integer","minimum":1,"maximum":16384},"normalized":{"type":"boolean","default":False},"phase":{"type":"string","enum":["move","down","up","click"],"default":"move"}},"required":["x","y"],"additionalProperties":False}},
    {"name":"vespera_ui_navigation","description":"Inject runtime UI focus traversal or activation into Editor Play Mode.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["next","previous","activate"]}},"required":["action"],"additionalProperties":False}},
    {"name":"vespera_text_input","description":"Inject text/backspace/clear through the real runtime TextInput path in Editor Play Mode.","inputSchema":{"type":"object","properties":{"text":{"type":"string"},"backspace":{"type":"boolean","default":False},"clear":{"type":"boolean","default":False}},"additionalProperties":False}},
    {"name":"vespera_get_ui_runtime_state","description":"Read semantic runtime UI hover/press/focus/pending-click state in Editor Play Mode.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_get_runtime_events","description":"Read structured managed lifecycle/trigger events from active Editor Play Mode.","inputSchema":{"type":"object","properties":{"after_sequence":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":500,"default":100}},"additionalProperties":False}},
    {"name":"vespera_clear_runtime_events","description":"Clear the active Editor Play Mode structured runtime-event buffer.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_save_scene","description":"Save the edit scene through Vespera's verified guarded save path.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"additionalProperties":False}},
    {"name":"vespera_build_csharp","description":"Start the editor's guarded C# build pipeline without holding the MCP call open. Returns an operation_id; poll vespera_get_operation until completion. Set wait_for_completion=true only for clients without short tool-call deadlines.","inputSchema":{"type":"object","properties":{"wait_for_completion":{"type":"boolean","default":False}},"additionalProperties":False}},
    {"name":"vespera_get_operation","description":"Poll a long-running Vespera MCP operation such as an asynchronous C# build.","inputSchema":{"type":"object","properties":{"operation_id":{"type":"string","minLength":1}},"required":["operation_id"],"additionalProperties":False}},
    {"name":"vespera_runtime_get_state","description":"Read state from an exported/reference runtime launched with --automation on localhost port 46788.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_runtime_inject_input_action","description":"Inject/override a named Input action into an automation-enabled standalone runtime.","inputSchema":{"type":"object","properties":{"action":{"type":"string","minLength":1},"phase":{"type":"string","enum":["set","press","release","clear"],"default":"set"},"value":{"type":"number","minimum":-1,"maximum":1,"default":1}},"required":["action"],"additionalProperties":False}},
    {"name":"vespera_runtime_pointer_event","description":"Inject a runtime UI pointer event into an automation-enabled standalone runtime.","inputSchema":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"width":{"type":"integer","minimum":1,"maximum":16384},"height":{"type":"integer","minimum":1,"maximum":16384},"normalized":{"type":"boolean","default":False},"phase":{"type":"string","enum":["move","down","up","click"],"default":"move"}},"required":["x","y"],"additionalProperties":False}},
    {"name":"vespera_runtime_ui_navigation","description":"Inject focus traversal/activation into standalone runtime UI.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["next","previous","activate"]}},"required":["action"],"additionalProperties":False}},
    {"name":"vespera_runtime_text_input","description":"Inject text/backspace/clear through standalone runtime TextInput handling.","inputSchema":{"type":"object","properties":{"text":{"type":"string"},"backspace":{"type":"boolean","default":False},"clear":{"type":"boolean","default":False}},"additionalProperties":False}},
    {"name":"vespera_runtime_get_ui_state","description":"Read semantic UI state and node values from an automation-enabled standalone runtime.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_runtime_get_events","description":"Read structured managed lifecycle/trigger events from standalone runtime.","inputSchema":{"type":"object","properties":{"after_sequence":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":500,"default":100}},"additionalProperties":False}},
    {"name":"vespera_runtime_clear_events","description":"Clear the standalone runtime structured event buffer.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_open_scene","description":"Open a Scene asset through Vespera's normal scene-open path.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"}},"additionalProperties":False}},
    {"name":"vespera_get_project_settings","description":"Read project runtime/build/package settings.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_set_project_setting","description":"Set one allow-listed project setting and optionally save the project.","inputSchema":{"type":"object","properties":{"key":{"type":"string","enum":["window_title","window_width","window_height","window_resizable","relative_mouse","escape_quits","vsync","company_name","product_version","package_name","managed_deployment","executable_name","build_output_directory","game_icon","startup_ui","lua_entry","development_diagnostics"]},"value":{"type":["string","number","boolean"]},"save":{"type":"boolean","default":True}},"required":["key","value"],"additionalProperties":False}},
    {"name":"vespera_set_build_include","description":"Add or remove an explicit stable asset root from standalone build closure and persist the project.","inputSchema":{"type":"object","properties":{"operation":{"type":"string","enum":["add","remove"]},"asset_id":{"type":"string"},"path":{"type":"string"},"save":{"type":"boolean","default":True}},"required":["operation"],"additionalProperties":False}},
    {"name":"vespera_check_asset_delete","description":"Run non-destructive asset delete preflight and return blockers.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"}},"additionalProperties":False}},
    {"name":"vespera_move_asset","description":"Move/rename a project asset while preserving its .vmeta stable identity and repairing local RML/RCSS relative references.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"},"destination":{"type":"string"}},"required":["destination"],"additionalProperties":False}},
    {"name":"vespera_repair_asset_fallbacks","description":"Repair stale readable stable-reference fallback paths.","inputSchema":{"type":"object","properties":{"save_project":{"type":"boolean","default":True}},"additionalProperties":False}},
    {"name":"vespera_create_ui_document","description":"Create a .slui v3 UI document under project Assets.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"}},"required":["path"],"additionalProperties":False}},
    {"name":"vespera_get_ui_document","description":"Read one .slui v1/v2/v3 UI document and its 1280x720 resolved state.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"}},"additionalProperties":False}},
    {"name":"vespera_add_ui_node","description":"Add a UI node to a .slui asset.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"},"type":{"type":"string","enum":["canvas","panel","text","image","button","progress_bar","scroll_view","list","grid","tabs","modal","tooltip","text_input"]},"name":{"type":"string"},"parent_id":{"type":"integer","minimum":0}},"required":["type"],"additionalProperties":False}},
    {"name":"vespera_set_ui_node","description":"Set hierarchy, rect, layout, widget, text and asset properties on one .slui node.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"},"node_id":{"type":"integer","minimum":1},"name":{"type":"string"},"parent_id":{"type":"integer","minimum":0},"enabled":{"type":"boolean"},"z_order":{"type":"integer"},"anchor_min":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"anchor_max":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"offset_min":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"offset_max":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"text":{"type":"string"},"font_size":{"type":"number","minimum":1},"font_family":{"type":"string","minLength":1},"font_weight":{"type":"integer","minimum":100,"maximum":900},"italic":{"type":"boolean"},"wrap":{"type":"boolean"},"line_spacing":{"type":"number","minimum":0.5,"maximum":4},"horizontal_alignment":{"type":"string","enum":["left","center","right"]},"vertical_alignment":{"type":"string","enum":["top","middle","bottom"]},"image_asset_id":{"type":"string"},"image_path":{"type":"string"},"font_asset_id":{"type":"string"},"font_path":{"type":"string"},"opacity":{"type":"number","minimum":0,"maximum":1},"corner_radius":{"type":"number","minimum":0},"border_width":{"type":"number","minimum":0},"shadow_offset":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"shadow_softness":{"type":"number","minimum":0,"maximum":32},"text_shadow_offset":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"image_fit":{"type":"string","enum":["stretch","contain","cover"]},"nine_slice":{"type":"array","items":{"type":"number","minimum":0},"minItems":4,"maxItems":4},"interactable":{"type":"boolean"},"progress_value":{"type":"number"},"progress_min":{"type":"number"},"progress_max":{"type":"number"},"layout_mode":{"type":"string","enum":["none","horizontal","vertical","grid"]},"layout_spacing":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"cell_size":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"columns":{"type":"integer","minimum":1},"clip_children":{"type":"boolean"},"scroll_offset":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"active_tab":{"type":"integer","minimum":0},"placeholder":{"type":"string"},"max_length":{"type":"integer","minimum":1},"read_only":{"type":"boolean"},"color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"text_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"border_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"shadow_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"text_shadow_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"button_normal_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"button_hovered_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"button_pressed_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"button_disabled_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"progress_fill_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"progress_background_color":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4}},"required":["node_id"],"additionalProperties":False}},
    {"name":"vespera_apply_ui_batch","description":"Apply many UI add/set/reparent/delete operations through one MCP call. Aliases returned by add operations can be referenced by later operations, reducing agent round trips for substantial interfaces.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"},"operations":{"type":"array","minItems":1,"maxItems":256,"items":{"type":"object","properties":{"op":{"type":"string","enum":["add","set","reparent","delete"]},"alias":{"type":"string"}},"required":["op"],"additionalProperties":True}}},"required":["operations"],"additionalProperties":False}},
    {"name":"vespera_delete_ui_node","description":"Delete one non-Canvas UI node and its descendants.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"},"node_id":{"type":"integer","minimum":1}},"required":["node_id"],"additionalProperties":False}},
    {"name":"vespera_reparent_ui_node","description":"Reparent or unparent a UI node with native cycle/Canvas validation.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"},"node_id":{"type":"integer","minimum":1},"parent_id":{"type":"integer","minimum":0}},"required":["node_id","parent_id"],"additionalProperties":False}},
    {"name":"vespera_validate_ui_document","description":"Validate a UI document at 720p and 1080p and report hierarchy/layout warnings.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"}},"additionalProperties":False}},
    {"name":"vespera_get_ui_layout","description":"Resolve UI rects/clipping at an arbitrary viewport for deterministic QA.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"},"width":{"type":"integer","minimum":1,"maximum":16384,"default":1280},"height":{"type":"integer","minimum":1,"maximum":16384,"default":720}},"additionalProperties":False}},
    {"name":"vespera_set_entity_metadata","description":"Set entity name/tag/layer/enabled through one undoable editor transaction.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"name":{"type":"string"},"tag":{"type":"string"},"layer":{"type":"string"},"enabled":{"type":"boolean"}},"required":["entity_id"],"additionalProperties":False}},
    {"name":"vespera_reorder_entity","description":"Reorder one entity relative to another through the normal Hierarchy transaction path.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"before_entity_id":{"type":"integer","minimum":1}},"required":["entity_id","before_entity_id"],"additionalProperties":False}},
    {"name":"vespera_get_asset_dependencies","description":"Inspect direct dependencies and reverse dependents for one project asset.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"path":{"type":"string"}},"additionalProperties":False}},
    {"name":"vespera_check_project_integrity","description":"Aggregate project, scene, asset, build-manifest and UI validation into one torture-test gate.","inputSchema":{"type":"object","properties":{},"additionalProperties":False}},
    {"name":"vespera_read_csharp_source","description":"Read one .cs file beneath the current project's managed source directory.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":False}},
    {"name":"vespera_write_csharp_source","description":"Create or replace one .cs file beneath the managed source directory using guarded replacement.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},"required":["path","content"],"additionalProperties":False}},
    {"name":"vespera_attach_csharp_script","description":"Attach one C# class to an authored Entity through undoable scene history.","inputSchema":{"type":"object","properties":{"entity_id":{"type":"integer","minimum":1},"class_name":{"type":"string","minLength":1}},"required":["entity_id","class_name"],"additionalProperties":False}},
    {"name":"vespera_run_qa_scenario","description":"Run a bounded built-in torture scenario through public editor command paths.","inputSchema":{"type":"object","properties":{"scenario":{"type":"string","enum":["hierarchy","play_cycle","ui_layout","managed_build_recovery","asset_move_save_reopen"]},"count":{"type":"integer","minimum":2,"maximum":128,"default":24},"cleanup":{"type":"boolean","default":True},"asset_id":{"type":"string"},"path":{"type":"string"},"destination":{"type":"string"}},"required":["scenario"],"additionalProperties":False}},
]


def enc(value: Any) -> str:
    return urllib.parse.quote(str(value), safe="-_.//:")


def dec(value: str) -> str:
    return urllib.parse.unquote(value)


def flatten_arguments(arguments: dict[str, Any]) -> dict[str, str]:
    result: dict[str, str] = {}
    for key, value in arguments.items():
        if isinstance(value, list):
            result[key] = ",".join(str(item) for item in value)
        elif isinstance(value, bool):
            result[key] = "true" if value else "false"
        elif value is not None:
            result[key] = str(value)
    return result


class EditorClient:
    def __init__(self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self.next_id = 1

    def call(self, command: str, arguments: dict[str, Any]) -> tuple[bool, str]:
        request_id = self.next_id
        self.next_id += 1
        fields = [str(request_id), enc(command)]
        for key, value in flatten_arguments(arguments).items():
            fields.append(f"{enc(key)}={enc(value)}")
        wire = "\t".join(fields) + "\n"

        # Connection establishment should fail quickly, but editor commands are
        # dispatched on the main thread and some are intentionally long-running
        # (managed compilation, QA scenarios, packaging). The old 2-second socket
        # timeout applied to both connect and response reads, causing successful
        # commands to be reported as an unreachable editor while they continued
        # executing. Give the response its own command-aware timeout.
        response_timeout = {
            "vespera_build_csharp": 180.0,
            "vespera_export_project": 300.0,
            "vespera_run_qa_scenario": 120.0,
        }.get(command, 30.0)
        try:
            with socket.create_connection((self.host, self.port), timeout=2.0) as sock:
                sock.settimeout(response_timeout)
                sock.sendall(wire.encode("utf-8"))
                file = sock.makefile("r", encoding="utf-8", newline="\n")
                response = file.readline()
        except socket.timeout:
            return False, (
                f"Vespera command '{command}' timed out after {response_timeout:.0f}s waiting for the editor response. "
                "The editor may still be executing the command; inspect vespera_get_command_log and vespera_get_console before retrying."
            )
        except OSError as exc:
            return False, (
                f"Vespera Editor automation connection failed at {self.host}:{self.port}: {exc}. "
                "Start the editor with run.ps1 -Automation or Tools > Automation / MCP > Start Local Automation Server."
            )

        if not response:
            return False, "Vespera Editor closed the automation connection without a response."
        parts = response.rstrip("\r\n").split("\t")
        if len(parts) < 2:
            return False, f"Malformed Vespera automation response: {response!r}"
        ok = parts[1] == "ok"
        parsed: dict[str, str] = {}
        for token in parts[2:]:
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            parsed[dec(key)] = dec(value)
        text = parsed.get("text", "Vespera command completed." if ok else "Vespera command failed.")
        return ok, text


class OperationRegistry:
    """Small in-process registry for long editor commands that outlive one MCP call.

    Some MCP clients can impose a shorter tool-call deadline than a normal
    managed compilation. Starting the editor request on a background thread and
    polling a local operation avoids reporting a false transport timeout while
    preserving the editor's existing synchronous/main-thread command semantics.
    """

    def __init__(self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self._lock = threading.Lock()
        self._next_id = 1
        self._operations: dict[str, dict[str, Any]] = {}

    def start(self, command: str, arguments: dict[str, Any]) -> dict[str, Any]:
        with self._lock:
            operation_id = f"op-{self._next_id:06d}"
            self._next_id += 1
            self._operations[operation_id] = {
                "operation_id": operation_id,
                "command": command,
                "status": "running",
                "started_at": time.time(),
                "completed_at": None,
                "ok": None,
                "text": "",
            }
            self._prune_locked()

        thread = threading.Thread(
            target=self._run, args=(operation_id, command, dict(arguments)),
            name=f"vespera-{operation_id}", daemon=True,
        )
        thread.start()
        return self.get(operation_id) or {"operation_id": operation_id, "status": "running"}

    def _run(self, operation_id: str, command: str, arguments: dict[str, Any]) -> None:
        client = EditorClient(self.host, self.port)
        ok, text = client.call(command, arguments)
        with self._lock:
            operation = self._operations.get(operation_id)
            if operation is None:
                return
            operation["status"] = "succeeded" if ok else "failed"
            operation["ok"] = ok
            operation["text"] = text
            operation["completed_at"] = time.time()

    def get(self, operation_id: str) -> dict[str, Any] | None:
        with self._lock:
            operation = self._operations.get(operation_id)
            if operation is None:
                return None
            result = dict(operation)
        # Keep timestamps useful for diagnostics without exposing excessive precision.
        result["elapsed_seconds"] = round(
            ((result["completed_at"] or time.time()) - result["started_at"]), 3
        )
        return result

    def _prune_locked(self) -> None:
        # Long QA sessions should not grow this bridge forever. Keep the newest
        # 64 operations; active jobs are never pruned.
        completed = [
            (key, value) for key, value in self._operations.items()
            if value.get("status") != "running"
        ]
        if len(self._operations) <= 64:
            return
        completed.sort(key=lambda item: item[1].get("completed_at") or 0.0)
        for key, _ in completed:
            if len(self._operations) <= 64:
                break
            self._operations.pop(key, None)


def send_json(message: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(message, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def result(request_id: Any, value: Any) -> None:
    send_json({"jsonrpc": "2.0", "id": request_id, "result": value})


def error(request_id: Any, code: int, message: str) -> None:
    send_json({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}})


def main() -> int:
    parser = argparse.ArgumentParser(description="Vespera Editor MCP developer bridge")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--runtime-port", type=int, default=DEFAULT_RUNTIME_PORT)
    args = parser.parse_args()
    client = EditorClient(args.host, args.port)
    runtime_client = EditorClient(args.host, args.runtime_port)
    operations = OperationRegistry(args.host, args.port)

    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            message = json.loads(raw)
        except json.JSONDecodeError as exc:
            error(None, -32700, f"Parse error: {exc}")
            continue

        method = message.get("method")
        request_id = message.get("id")
        params = message.get("params") or {}

        # Notifications intentionally produce no response.
        if request_id is None:
            continue

        if method == "initialize":
            requested = params.get("protocolVersion") or "2024-11-05"
            result(request_id, {
                "protocolVersion": requested,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "instructions": (
                    "Vespera 1.1 developer-preview tools route through localhost-only editor/runtime "
                    "automation. Start every QA session with vespera_get_bridge_info and compare bridge/editor versions "
                    "before mutation. Use IDs returned by vespera_list_entities."
                ),
            })
        elif method == "ping":
            result(request_id, {})
        elif method == "tools/list":
            result(request_id, {"tools": TOOLS})
        elif method == "tools/call":
            name = params.get("name")
            arguments = params.get("arguments") or {}
            if not isinstance(name, str) or name not in {tool["name"] for tool in TOOLS}:
                error(request_id, -32602, "Unknown Vespera tool")
                continue
            if not isinstance(arguments, dict):
                error(request_id, -32602, "Tool arguments must be an object")
                continue
            if name == "vespera_get_bridge_info":
                editor_ok, editor_text = client.call("vespera_get_state", {})
                runtime_ok, runtime_text = runtime_client.call("vespera_runtime_get_state", {})
                payload = {
                    "bridge_version": SERVER_VERSION,
                    "mcp_tool_count": len(TOOLS),
                    "editor": {"host": args.host, "port": args.port, "connected": editor_ok},
                    "runtime": {"host": args.host, "port": args.runtime_port, "connected": runtime_ok},
                }
                if editor_ok:
                    try: payload["editor"]["state"] = json.loads(editor_text)
                    except json.JSONDecodeError: payload["editor"]["state_text"] = editor_text
                else:
                    payload["editor"]["error"] = editor_text
                if runtime_ok:
                    try: payload["runtime"]["state"] = json.loads(runtime_text)
                    except json.JSONDecodeError: payload["runtime"]["state_text"] = runtime_text
                result(request_id, {
                    "content": [{"type": "text", "text": json.dumps(payload, separators=(",", ":"))}],
                    "isError": False,
                })
                continue

            if name == "vespera_apply_ui_batch":
                base = {}
                if isinstance(arguments.get("asset_id"), str) and arguments.get("asset_id"):
                    base["asset_id"] = arguments["asset_id"]
                if isinstance(arguments.get("path"), str) and arguments.get("path"):
                    base["path"] = arguments["path"]
                batch_ops = arguments.get("operations")
                if not isinstance(batch_ops, list) or not batch_ops:
                    error(request_id, -32602, "operations must be a non-empty array")
                    continue
                aliases: dict[str, int] = {}
                outcomes: list[dict[str, Any]] = []

                def resolve_batch_id(value: Any) -> Any:
                    if isinstance(value, str) and value in aliases:
                        return aliases[value]
                    return value

                batch_failed = False
                for index, raw_op in enumerate(batch_ops):
                    if not isinstance(raw_op, dict):
                        outcomes.append({"index": index, "ok": False, "error": "operation must be an object"})
                        batch_failed = True
                        break
                    op = raw_op.get("op")
                    forwarded = dict(base)
                    for key, value in raw_op.items():
                        if key in {"op", "alias"}:
                            continue
                        if key in {"node_id", "parent_id"}:
                            value = resolve_batch_id(value)
                        forwarded[key] = value
                    command = {"add":"vespera_add_ui_node","set":"vespera_set_ui_node","reparent":"vespera_reparent_ui_node","delete":"vespera_delete_ui_node"}.get(op)
                    if command is None:
                        outcomes.append({"index": index, "ok": False, "error": "op must be add, set, reparent or delete"})
                        batch_failed = True
                        break
                    ok, text = client.call(command, forwarded)
                    item: dict[str, Any] = {"index": index, "op": op, "ok": ok}
                    try:
                        parsed_text = json.loads(text)
                        item["result"] = parsed_text
                    except json.JSONDecodeError:
                        item["text"] = text
                        parsed_text = None
                    alias = raw_op.get("alias")
                    if ok and op == "add" and isinstance(alias, str) and alias:
                        if isinstance(parsed_text, dict) and isinstance(parsed_text.get("node_id"), int):
                            aliases[alias] = parsed_text["node_id"]
                            item["alias"] = alias
                    outcomes.append(item)
                    if not ok:
                        batch_failed = True
                        break
                payload = {"ok": not batch_failed, "operations_completed": len(outcomes), "aliases": aliases, "results": outcomes}
                result(request_id, {
                    "content": [{"type": "text", "text": json.dumps(payload, separators=(",", ":"))}],
                    "isError": batch_failed,
                })
                continue

            if name.startswith("vespera_runtime_"):
                ok, text = runtime_client.call(name, arguments)
                result(request_id, {
                    "content": [{"type": "text", "text": text}],
                    "isError": not ok,
                })
                continue

            if name == "vespera_get_operation":
                operation_id = arguments.get("operation_id")
                if not isinstance(operation_id, str) or not operation_id:
                    error(request_id, -32602, "operation_id is required")
                    continue
                operation = operations.get(operation_id)
                if operation is None:
                    result(request_id, {
                        "content": [{"type": "text", "text": json.dumps({
                            "operation_id": operation_id, "status": "unknown"
                        }, separators=(",", ":"))}],
                        "isError": True,
                    })
                    continue
                result(request_id, {
                    "content": [{"type": "text", "text": json.dumps(operation, separators=(",", ":"))}],
                    "isError": False,
                })
                continue

            if name == "vespera_build_csharp" and not arguments.get("wait_for_completion", False):
                operation = operations.start(name, {})
                text = json.dumps({
                    "operation_id": operation["operation_id"],
                    "status": operation["status"],
                    "command": name,
                    "poll_with": "vespera_get_operation",
                }, separators=(",", ":"))
                result(request_id, {
                    "content": [{"type": "text", "text": text}],
                    "isError": False,
                })
                continue

            forwarded_arguments = dict(arguments)
            forwarded_arguments.pop("wait_for_completion", None)
            ok, text = client.call(name, forwarded_arguments)
            result(request_id, {
                "content": [{"type": "text", "text": text}],
                "isError": not ok,
            })
        else:
            error(request_id, -32601, f"Method not found: {method}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
