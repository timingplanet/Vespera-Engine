#!/usr/bin/env python3
"""Vespera 1.0.0 direct automation torture smoke.

Run the editor with run.ps1 -Automation first. This script intentionally uses
VAP v1, the same localhost/main-thread boundary consumed by the MCP bridge.
"""
from __future__ import annotations
import argparse, json, socket, subprocess, sys, time, urllib.parse
from pathlib import Path

QA_VERSION = "1.0.0"


def enc(v: object) -> str:
    return urllib.parse.quote(str(v), safe="-_.//:")

def dec(v: str) -> str:
    return urllib.parse.unquote(v)

def call(host: str, port: int, req_id: int, command: str, **args: object) -> tuple[bool,str]:
    parts=[str(req_id),enc(command)]
    for k,v in args.items():
        if v is None: continue
        if isinstance(v,bool): v="true" if v else "false"
        parts.append(f"{enc(k)}={enc(v)}")
    wire="\t".join(parts)+"\n"

    # Keep connection failure detection quick, but do not reuse that timeout for
    # the response. VAP commands run on the editor main thread and QA scenarios
    # may include multiple managed builds or packaging operations.
    response_timeout = {
        "vespera_run_qa_scenario": 300.0,
        "vespera_build_csharp": 180.0,
        "vespera_export_project": 300.0,
    }.get(command, 30.0)
    try:
        with socket.create_connection((host,port),timeout=5.0) as sock:
            sock.settimeout(response_timeout)
            sock.sendall(wire.encode())
            line=sock.makefile("r",encoding="utf-8").readline().rstrip("\r\n")
    except socket.timeout as exc:
        raise TimeoutError(
            f"command '{command}' timed out after {response_timeout:.0f}s waiting for the editor response"
        ) from exc
    tokens=line.split("\t")
    ok=len(tokens)>=2 and tokens[1]=="ok"
    fields={}
    for token in tokens[2:]:
        if "=" in token:
            k,v=token.split("=",1); fields[dec(k)]=dec(v)
    return ok,fields.get("text",line)

def run_mcp_managed_build(host: str, port: int) -> tuple[bool, str]:
    """Exercise the stdio MCP async-build path, not only direct VAP."""
    bridge = Path(__file__).with_name("vespera_mcp_server.py")
    process = subprocess.Popen(
        [sys.executable, str(bridge), "--host", host, "--port", str(port)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1,
    )
    next_id = 1

    def rpc(method: str, params: dict | None = None) -> dict:
        nonlocal next_id
        request = {"jsonrpc":"2.0","id":next_id,"method":method}
        next_id += 1
        if params is not None:
            request["params"] = params
        assert process.stdin is not None and process.stdout is not None
        process.stdin.write(json.dumps(request, separators=(",", ":")) + "\n")
        process.stdin.flush()
        line = process.stdout.readline()
        if not line:
            stderr = process.stderr.read() if process.stderr else ""
            raise RuntimeError("MCP bridge closed without response" + (": " + stderr if stderr else ""))
        response = json.loads(line)
        if "error" in response:
            raise RuntimeError(str(response["error"]))
        return response["result"]

    try:
        rpc("initialize", {"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"vespera-qa","version":QA_VERSION}})
        started = rpc("tools/call", {"name":"vespera_build_csharp","arguments":{}})
        text = started["content"][0]["text"]
        info = json.loads(text)
        operation_id = info.get("operation_id")
        if not operation_id or info.get("status") != "running":
            return False, f"async build did not return a running operation: {text}"
        deadline = time.monotonic() + 300.0
        while time.monotonic() < deadline:
            polled = rpc("tools/call", {"name":"vespera_get_operation","arguments":{"operation_id":operation_id}})
            state = json.loads(polled["content"][0]["text"])
            if state.get("status") == "running":
                time.sleep(0.25)
                continue
            ok = state.get("status") == "succeeded" and state.get("ok") is True
            return ok, json.dumps(state, separators=(",", ":"))
        return False, f"operation {operation_id} did not finish within 300s"
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)



def _json_result(text: str) -> dict:
    try:
        value = json.loads(text)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"expected JSON result, got: {text}") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"expected JSON object result, got: {text}")
    return value


def _lifecycle_order_ok(events: list[dict], first_name: str, second_name: str) -> bool:
    by_instance: dict[tuple[int, str, int], dict[str, int]] = {}
    for event in events:
        key = (int(event.get("entity_id", 0)), str(event.get("component", "")), int(event.get("generation", 0)))
        callback = str(event.get("callback", ""))
        if callback in (first_name, second_name):
            by_instance.setdefault(key, {})[callback] = int(event.get("sequence", 0))
    return any(pair.get(first_name, 10**18) < pair.get(second_name, -1) for pair in by_instance.values())


def run_editor_runtime_telemetry(host: str, port: int) -> tuple[bool, str]:
    req = 70000
    failures: list[str] = []
    details: dict[str, object] = {}
    entered = False

    def vap(command: str, **args: object) -> dict:
        nonlocal req
        ok, text = call(host, port, req, command, **args)
        req += 1
        if not ok:
            raise RuntimeError(f"{command}: {text}")
        return _json_result(text)

    try:
        vap("vespera_play", action="enter")
        entered = True
        time.sleep(0.25)
        initial = vap("vespera_get_runtime_events", limit=500)
        events = initial.get("events", [])
        details["initial_events"] = len(events)
        details["assembly_generation"] = initial.get("assembly_generation", 0)
        if not _lifecycle_order_ok(events, "Start", "OnEnable"):
            failures.append("no Start -> OnEnable lifecycle pair observed")

        hover = vap("vespera_pointer_event", x=200, y=130, width=1280, height=720, phase="move")
        details["hovered"] = hover.get("hovered")
        if hover.get("hovered") != 5:
            failures.append(f"Continue Button hit test expected node 5, got {hover.get('hovered')}")

        clicked = vap("vespera_pointer_event", x=200, y=130, width=1280, height=720, phase="click")
        details["click_pending"] = clicked.get("pending_clicks", 0)
        if int(clicked.get("pending_clicks", 0)) < 1:
            failures.append("Continue Button click did not queue a runtime UI click")

        focused = vap("vespera_ui_navigation", action="next")
        details["focus_after_next"] = focused.get("focused")
        if focused.get("focused") != 7:
            failures.append(f"focus traversal expected QA Text Input node 7, got {focused.get('focused')}")
        text = vap("vespera_text_input", clear=True, text="vespera-qa")
        details["text"] = text.get("text")
        if text.get("text") != "vespera-qa":
            failures.append("TextInput injection did not round-trip expected text")

        vap("vespera_inject_input_action", action="probe", phase="press", value=1.0)
        time.sleep(0.08)
        vap("vespera_inject_input_action", action="probe", phase="release")
        time.sleep(0.08)
        vap("vespera_inject_input_action", action="probe", phase="clear")
    except Exception as exc:
        failures.append(str(exc))
    finally:
        if entered:
            try:
                vap("vespera_play", action="stop")
                archived = vap("vespera_get_runtime_events", limit=500)
                archived_events = archived.get("events", [])
                details["archived_events"] = len(archived_events)
                if not _lifecycle_order_ok(archived_events, "OnDisable", "OnDestroy"):
                    failures.append("no archived OnDisable -> OnDestroy lifecycle pair observed after Stop")
            except Exception as exc:
                failures.append(f"stop/archive: {exc}")
    return not failures, json.dumps({"passed": not failures, "failures": failures, **details}, separators=(",", ":"))


def _wait_runtime(host: str, port: int, deadline_seconds: float = 20.0) -> dict:
    deadline = time.monotonic() + deadline_seconds
    last_error = "runtime automation did not respond"
    req = 81000
    while time.monotonic() < deadline:
        try:
            ok, text = call(host, port, req, "vespera_runtime_get_state")
            req += 1
            if ok:
                state = _json_result(text)
                if state.get("scene") or state.get("load_failed"):
                    return state
                last_error = f"runtime responded before startup completed: {text}"
            else:
                last_error = text
        except (OSError, TimeoutError) as exc:
            last_error = str(exc)
        time.sleep(0.2)
    raise RuntimeError(last_error)


def run_exported_runtime_automation(host: str, editor_port: int, runtime_port: int = 46788, configuration: str = "Debug") -> tuple[bool, str]:
    failures: list[str] = []
    details: dict[str, object] = {}
    process: subprocess.Popen | None = None
    try:
        ok, text = call(host, editor_port, 80000, "vespera_export_project", configuration=configuration, clean=True, launch=False)
        if not ok:
            raise RuntimeError(f"{configuration} export failed: {text}")
        package = _json_result(text)
        runtime = package.get("runtime")
        details["assets"] = package.get("assets")
        details["package_warnings"] = package.get("warnings")
        if not runtime:
            raise RuntimeError(f"{configuration} export did not include a runnable runtime; build that configuration before RuntimeExportAutomation")
        runtime_path = Path(str(runtime))
        if not runtime_path.exists():
            raise RuntimeError(f"packaged runtime does not exist: {runtime_path}")
        process = subprocess.Popen([str(runtime_path), "--automation"], cwd=str(runtime_path.parent))
        state = _wait_runtime(host, runtime_port)
        details["runtime_state"] = state
        if state.get("load_failed"):
            failures.append("exported runtime reported load_failed=true")
        if not state.get("ui_loaded"):
            failures.append("exported runtime did not load runtime UI")
        if not state.get("watcher_clip_available"):
            failures.append("exported runtime AssetCatalog is missing watcher_walk.slspriteclip")
        if not state.get("lua_ready"):
            failures.append("exported runtime did not start the configured project Lua entry")

        req = 82000
        def runtime_call(command: str, **args: object) -> dict:
            nonlocal req
            ok, text = call(host, runtime_port, req, command, **args)
            req += 1
            if not ok:
                raise RuntimeError(f"{command}: {text}")
            return _json_result(text)

        initial = runtime_call("vespera_runtime_get_events", limit=500)
        events = initial.get("events", [])
        details["runtime_events"] = len(events)
        if not _lifecycle_order_ok(events, "Start", "OnEnable"):
            failures.append("exported runtime has no Start -> OnEnable lifecycle pair")

        hover = runtime_call("vespera_runtime_pointer_event", x=200, y=130, width=1280, height=720, phase="move")
        if hover.get("hovered") != 5:
            failures.append(f"exported runtime Continue hit test expected node 5, got {hover.get('hovered')}")
        clicked = runtime_call("vespera_runtime_pointer_event", x=200, y=130, width=1280, height=720, phase="click")
        if int(clicked.get("pending_clicks", 0)) < 1:
            failures.append("exported runtime Continue click did not queue a click")
        focus = runtime_call("vespera_runtime_ui_navigation", action="next")
        if focus.get("focused") != 7:
            failures.append(f"exported runtime focus traversal expected node 7, got {focus.get('focused')}")
        typed = runtime_call("vespera_runtime_text_input", clear=True, text="vespera-runtime-qa")
        details["runtime_text"] = typed.get("text")
        if typed.get("text") != "vespera-runtime-qa":
            failures.append("exported runtime TextInput injection did not round-trip")
        runtime_call("vespera_runtime_inject_input_action", action="probe", phase="press", value=1.0)
        time.sleep(0.08)
        runtime_call("vespera_runtime_inject_input_action", action="probe", phase="release")
        runtime_call("vespera_runtime_inject_input_action", action="probe", phase="clear")
    except Exception as exc:
        failures.append(str(exc))
    finally:
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
    return not failures, json.dumps({"passed": not failures, "failures": failures, **details}, separators=(",", ":"))


def run_scene_switch_stress(host: str, port: int, count: int = 12) -> tuple[bool, str]:
    """Repeatedly cross the saved-scene boundary through the public editor API."""
    req = 86000
    failures: list[str] = []
    switches = 0
    original: str | None = None

    def vap(command: str, **args: object):
        nonlocal req
        ok, text = call(host, port, req, command, **args)
        req += 1
        if not ok:
            raise RuntimeError(f"{command}: {text}")
        return json.loads(text)

    try:
        initial = vap("vespera_get_state")
        if bool(initial.get("dirty")):
            raise RuntimeError("scene-switch stress requires a clean saved edit scene")
        initial_scene = str(initial.get("scene", "")).replace("\\", "/").lower()
        if not initial_scene:
            raise RuntimeError("scene-switch stress requires an open scene")
        assets = vap("vespera_list_assets", kind="Scene")
        if not isinstance(assets, list) or len(assets) < 2:
            raise RuntimeError("scene-switch stress requires at least two project Scene assets")
        scene_paths = [str(asset.get("path", "")) for asset in assets if asset.get("path")]
        original = next((path for path in scene_paths if initial_scene.endswith(path.replace("\\", "/").lower())), None)
        if not original:
            raise RuntimeError(f"could not map current scene to AssetCatalog path: {initial.get('scene')}")
        alternates = [path for path in scene_paths if path != original]
        for i in range(max(1, count)):
            target = alternates[i % len(alternates)]
            vap("vespera_open_scene", path=target)
            switches += 1
            validated = vap("vespera_validate_scene")
            if int(validated.get("errors", 0)) != 0:
                failures.append(f"{target} reported {validated.get('errors')} validation error(s)")
                break
            state = vap("vespera_get_state")
            if bool(state.get("dirty")):
                failures.append(f"opening {target} unexpectedly marked the scene dirty")
                break
            vap("vespera_open_scene", path=original)
            switches += 1
            restored = vap("vespera_get_state")
            if bool(restored.get("dirty")) or not str(restored.get("scene", "")).replace("\\", "/").lower().endswith(original.replace("\\", "/").lower()):
                failures.append("scene-switch stress did not restore the original clean scene")
                break
    except Exception as exc:
        failures.append(str(exc))
    finally:
        # Leave the editor exactly where the scenario started even when a target
        # scene exposes a failure, so later RC checks do not inherit test damage.
        if original:
            try:
                current = vap("vespera_get_state")
                current_scene = str(current.get("scene", "")).replace("\\", "/").lower()
                if not current_scene.endswith(original.replace("\\", "/").lower()):
                    vap("vespera_open_scene", path=original)
                    switches += 1
                restored = vap("vespera_get_state")
                if bool(restored.get("dirty")):
                    failures.append("scene-switch cleanup left the restored scene dirty")
                final_validation = vap("vespera_validate_scene")
                if int(final_validation.get("errors", 0)) != 0:
                    failures.append(f"final restored scene reported {final_validation.get('errors')} validation error(s)")
            except Exception as cleanup_exc:
                failures.append(f"scene-switch cleanup failed: {cleanup_exc}")
    return not failures, json.dumps({"passed": not failures, "failures": failures, "switches": switches}, separators=(",", ":"))

def run_scale_stress(host: str, port: int, count: int = 150) -> tuple[bool, str]:
    """Author a large public-path scene workload, then restore by reopening the clean saved scene.

    The editor intentionally bounds undo history. A 150-entity stress pass creates more
    authoring commands than that history can retain, so cleanup must not assume every
    stress mutation is individually undoable. Reopening the original clean Scene asset
    crosses the same persistence boundary a developer would use and restores the exact
    saved scene without increasing the product undo-history limit just for QA.
    """
    req = 90000
    failures: list[str] = []
    created: list[int] = []
    started = time.monotonic()
    original_scene: str | None = None
    before_entities: int | None = None
    playing: dict = {}

    def vap(command: str, **args: object) -> dict:
        nonlocal req
        ok, text = call(host, port, req, command, **args)
        req += 1
        if not ok:
            raise RuntimeError(f"{command}: {text}")
        return _json_result(text)

    def restore_original_scene() -> None:
        if not original_scene:
            return
        current = vap("vespera_get_state")
        current_scene = str(current.get("scene", "")).replace("\\", "/").lower()
        expected_scene = original_scene.replace("\\", "/").lower()
        if bool(current.get("dirty")) or not current_scene.endswith(expected_scene):
            vap("vespera_open_scene", path=original_scene)
        restored = vap("vespera_get_state")
        if bool(restored.get("dirty")):
            raise RuntimeError("stress cleanup left the restored scene dirty")
        if before_entities is not None and int(restored.get("entities", -1)) != before_entities:
            raise RuntimeError(
                f"stress cleanup expected {before_entities} entities after reopen, got {restored.get('entities')}"
            )
        final_validation = vap("vespera_validate_scene")
        if int(final_validation.get("errors", 0)) != 0:
            raise RuntimeError(
                f"stress cleanup restored scene with {final_validation.get('errors')} validation error(s)"
            )

    try:
        before = vap("vespera_get_state")
        if bool(before.get("dirty")):
            raise RuntimeError("scale stress requires a clean saved edit scene")
        before_entities = int(before.get("entities", 0))
        initial_scene = str(before.get("scene", "")).replace("\\", "/").lower()
        if not initial_scene:
            raise RuntimeError("scale stress requires an open saved scene")

        ok, assets_text = call(host, port, req, "vespera_list_assets", kind="Scene")
        req += 1
        if not ok:
            raise RuntimeError(f"vespera_list_assets: {assets_text}")
        assets = json.loads(assets_text)
        if not isinstance(assets, list):
            raise RuntimeError("scale stress could not enumerate project Scene assets")
        scene_paths = [
            str(asset.get("path", ""))
            for asset in assets
            if isinstance(asset, dict) and asset.get("path")
        ]
        original_scene = next(
            (path for path in scene_paths if initial_scene.endswith(path.replace("\\", "/").lower())),
            None,
        )
        if not original_scene:
            raise RuntimeError(f"could not map current scene to AssetCatalog path: {before.get('scene')}")

        for i in range(count):
            created_entity = vap("vespera_create_primitive", primitive="cube")
            entity_id = int(created_entity["entity_id"])
            created.append(entity_id)
            x = (i % 15) * 1.5
            z = (i // 15) * 1.5
            vap("vespera_set_transform", entity_id=entity_id, position=f"{x},0,{z}")
            if i % 3 == 0:
                vap("vespera_add_component", entity_id=entity_id, component="sectorline.cylinder_collider")
            if i % 12 == 0:
                vap("vespera_add_component", entity_id=entity_id, component="sectorline.point_light")

        authored = vap("vespera_get_state")
        expected = before_entities + count
        if int(authored.get("entities", 0)) != expected:
            failures.append(f"stress authoring expected {expected} entities, got {authored.get('entities')}")

        vap("vespera_play", action="enter")
        time.sleep(1.0)
        playing = vap("vespera_get_state")
        perf = playing.get("performance", {})
        if int(perf.get("frame_index", 0)) <= 0:
            failures.append("performance telemetry did not advance during stress Play Mode")
        vap("vespera_play", action="stop")

        restore_original_scene()

        elapsed = time.monotonic() - started
        return not failures, json.dumps({
            "passed": not failures,
            "failures": failures,
            "created": count,
            "elapsed_seconds": round(elapsed, 3),
            "play_performance": playing.get("performance", {}),
            "scene_stats": playing.get("scene_stats", {}),
        }, separators=(",", ":"))
    except Exception as exc:
        failures.append(str(exc))
        try:
            try:
                vap("vespera_play", action="stop")
            except Exception:
                pass
            restore_original_scene()
        except Exception as cleanup_exc:
            failures.append(f"stress cleanup: {cleanup_exc}")
        return False, json.dumps(
            {"passed": False, "failures": failures, "created": len(created)},
            separators=(",", ":"),
        )


def run(host: str, port: int, include_export: bool, include_managed_recovery: bool,
        include_mcp_managed_build: bool, include_asset_move_save: bool,
        include_runtime_telemetry: bool, include_runtime_export_automation: bool,
        include_stress: bool, play_cycles: int, scene_switches: int,
        runtime_export_configuration: str) -> int:
    req=1; failed=[]
    try:
        ok, text = call(host, port, req, "vespera_get_state")
        req += 1
        if not ok:
            failed.append("version-fingerprint")
            print(f"[FAIL] version-fingerprint: {text}")
        else:
            state = _json_result(text)
            live_version = str(state.get("engine_version", ""))
            version_ok = live_version == QA_VERSION
            print(f"[{ 'PASS' if version_ok else 'FAIL' }] version-fingerprint: expected={QA_VERSION} editor={live_version}")
            if not version_ok: failed.append("version-fingerprint")
    except Exception as exc:
        failed.append("version-fingerprint")
        print(f"[FAIL] version-fingerprint: {exc}")
    if failed:
        print(json.dumps({"passed":False,"failed":failed},separators=(",",":")))
        return 1
    steps=[
        ("integrity-before","vespera_check_project_integrity",{}),
        ("hierarchy","vespera_run_qa_scenario",{"scenario":"hierarchy","count":24,"cleanup":True}),
        ("play-cycle","vespera_run_qa_scenario",{"scenario":"play_cycle","count":play_cycles}),
        ("ui-layout","vespera_run_qa_scenario",{"scenario":"ui_layout","path":"ui/reference_hud.slui"}),
    ]
    if include_managed_recovery:
        steps.append(("managed-build-recovery","vespera_run_qa_scenario",{"scenario":"managed_build_recovery","path":"TriggerReporter.cs"}))
    if include_asset_move_save:
        steps.append(("asset-move-save-reopen","vespera_run_qa_scenario",{"scenario":"asset_move_save_reopen","path":"prefabs/watcher.slprefab","cleanup":True}))
    # Runtime telemetry is run separately below because it performs a multi-call
    # Play session rather than one editor command.
    # This is intentionally after every state-mutating scenario so a green
    # integrity-after result proves the optional torture work cleaned up too.
    steps.append(("integrity-after","vespera_check_project_integrity",{}))
    if include_export:
        steps.append(("development-export","vespera_export_project",{"configuration":"Development","clean":True,"launch":False}))
    # Keep the final integrity gate after the multi-call Play/runtime telemetry
    # test as well as the single-command destructive scenarios.
    integrity_after = [step for step in steps if step[0] == "integrity-after"]
    steps = [step for step in steps if step[0] != "integrity-after"]
    for label,cmd,args in steps:
        try: ok,text=call(host,port,req,cmd,**args)
        except TimeoutError as exc:
            ok,text=False,str(exc)
        except OSError as exc:
            ok,text=False,f"connection failed: {exc}"
        req+=1
        print(f"[{ 'PASS' if ok else 'FAIL' }] {label}: {text}")
        if not ok: failed.append(label)
    if scene_switches > 0:
        ok, text = run_scene_switch_stress(host, port, scene_switches)
        print(f"[{ 'PASS' if ok else 'FAIL' }] scene-switch-stress: {text}")
        if not ok: failed.append("scene-switch-stress")
    if include_runtime_telemetry:
        ok, text = run_editor_runtime_telemetry(host, port)
        print(f"[{ 'PASS' if ok else 'FAIL' }] runtime-telemetry: {text}")
        if not ok: failed.append("runtime-telemetry")
    for label,cmd,args in integrity_after:
        try: ok,text=call(host,port,req,cmd,**args)
        except TimeoutError as exc:
            ok,text=False,str(exc)
        except OSError as exc:
            ok,text=False,f"connection failed: {exc}"
        req+=1
        print(f"[{ 'PASS' if ok else 'FAIL' }] {label}: {text}")
        if not ok: failed.append(label)
    if include_mcp_managed_build:
        try:
            ok, text = run_mcp_managed_build(host, port)
        except Exception as exc:
            ok, text = False, f"MCP managed-build smoke failed: {exc}"
        print(f"[{ 'PASS' if ok else 'FAIL' }] mcp-managed-build: {text}")
        if not ok: failed.append("mcp-managed-build")
    if include_runtime_export_automation:
        ok, text = run_exported_runtime_automation(host, port, configuration=runtime_export_configuration)
        print(f"[{ 'PASS' if ok else 'FAIL' }] runtime-export-automation: {text}")
        if not ok: failed.append("runtime-export-automation")
    if include_stress:
        ok, text = run_scale_stress(host, port)
        print(f"[{ 'PASS' if ok else 'FAIL' }] scale-stress: {text}")
        if not ok: failed.append("scale-stress")
    print(json.dumps({"passed":not failed,"failed":failed},separators=(",",":")))
    return 0 if not failed else 1

if __name__=="__main__":
    ap=argparse.ArgumentParser(description=f"Run Vespera {QA_VERSION} torture smoke through VAP v1")
    ap.add_argument("--host",default="127.0.0.1")
    ap.add_argument("--port",type=int,default=46787)
    ap.add_argument("--export",action="store_true",help="also package a Development build")
    ap.add_argument("--managed-recovery",action="store_true",help="also inject/fix an intentional C# error and verify the restored source builds")
    ap.add_argument("--mcp-managed-build",action="store_true",help="also verify the stdio MCP async C# build returns an operation id and polls to completion")
    ap.add_argument("--asset-move-save",action="store_true",help="also move the reference Prefab, save/reopen the open scene, verify canonical fallback repair, then restore it")
    ap.add_argument("--runtime-telemetry",action="store_true",help="also exercise Editor Play input/UI injection and managed lifecycle telemetry")
    ap.add_argument("--runtime-export-automation",action="store_true",help="also package the selected built runtime configuration, launch it with localhost QA automation, and exercise the same UI/input/event surface")
    ap.add_argument("--stress",action="store_true",help="also author 150 primitive entities through VAP, sample Play performance telemetry, then clean them up")
    ap.add_argument("--play-cycles",type=int,default=4,help="Play/Pause/Step/Stop repetitions (2..64)")
    ap.add_argument("--scene-switches",type=int,default=0,help="saved-scene switch/restore cycles through the public editor API")
    ap.add_argument("--runtime-export-configuration",choices=["Debug","Development","Release"],default="Debug",help="configuration used by exported-runtime automation")
    a=ap.parse_args()
    if not 2 <= a.play_cycles <= 64: ap.error("--play-cycles must be 2..64")
    if not 0 <= a.scene_switches <= 64: ap.error("--scene-switches must be 0..64")
    raise SystemExit(run(a.host,a.port,a.export,a.managed_recovery,a.mcp_managed_build,a.asset_move_save,a.runtime_telemetry,a.runtime_export_automation,a.stress,a.play_cycles,a.scene_switches,a.runtime_export_configuration))
