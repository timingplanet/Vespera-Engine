#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace vespera::editor {

enum class EditorCommandKind {
    Unknown,
    CreateSector,
    DuplicateSector,
    DeleteSector,
    CreateMaterial,
    DuplicateMaterial,
    DeleteMaterial,
    SetPortalTarget,
    CreateEntity,
    CreateSpriteEntity,
    CreateCubeEntity,
    CreatePlaneEntity,
    CreateCylinderEntity,
    CreateSphereEntity,
    CreateTriggerEntity,
    CreatePointLightEntity,
    DuplicateEntity,
    DeleteEntity,
    SelectEntity,
    ReorderEntity,
    ReparentEntity,
    UnparentEntity,
    MoveEntityTransform,
    RotateEntityTransform,
    ScaleEntityTransform,
    SetEntityTransform,
    SetEntityMetadata,
    AddComponent,
    RemoveComponent,
    SetComponentProperty,
    CreatePrefab,
    ApplyPrefab,
    RevertPrefab,
    UnpackPrefab,
    InstantiatePrefab,
    CreateSpriteClip,
    DuplicateSpriteClip,
    DeleteSpriteClip,
    EnterPlayMode,
    ExitPlayMode,
    PausePlayMode,
    ResumePlayMode,
    StepPlayMode,
    DropPrefabIntoScene,
    AssignTextureAsset,
    CreateMaterialAsset,
    SaveMaterialAsset,
    AssignMaterialAsset,
    SaveScene,
    OpenScene,
    Undo,
    Redo,
    RefreshAssets,
    MoveAsset,
    RepairAssetFallbacks,
    SetProjectSetting,
    CreateUiDocument,
    AddUiNode,
    SetUiNode,
    ReparentUiNode,
    DeleteUiNode,
    AttachManagedScript,
    WriteManagedSource,
    RunQaScenario,
    InjectRuntimeInput,
    InjectRuntimeUi,
    ClearRuntimeEvents,
    BuildManagedScripts,
    ExportProject,
};

struct EditorCommandTarget {
    std::uint64_t entity_id = 0;
    std::string asset_id;
};

struct EditorCommandRequest {
    EditorCommandKind kind = EditorCommandKind::Unknown;
    std::string label;
    EditorCommandTarget target{};
    bool undoable = true;
};

struct EditorCommandRecord {
    std::uint64_t sequence = 0;
    EditorCommandKind kind = EditorCommandKind::Unknown;
    std::string label;
    bool succeeded = false;
    std::uint64_t before_state_id = 0;
    std::uint64_t after_state_id = 0;
    std::uint64_t target_entity_id = 0;
    std::string target_asset_id;
};

constexpr std::string_view command_name(EditorCommandKind kind) {
    switch (kind) {
        case EditorCommandKind::CreateSector: return "scene.create_sector";
        case EditorCommandKind::DuplicateSector: return "scene.duplicate_sector";
        case EditorCommandKind::DeleteSector: return "scene.delete_sector";
        case EditorCommandKind::CreateMaterial: return "scene.create_material";
        case EditorCommandKind::DuplicateMaterial: return "scene.duplicate_material";
        case EditorCommandKind::DeleteMaterial: return "scene.delete_material";
        case EditorCommandKind::SetPortalTarget: return "scene.set_portal_target";
        case EditorCommandKind::CreateEntity: return "entity.create";
        case EditorCommandKind::CreateSpriteEntity: return "entity.create_sprite";
        case EditorCommandKind::CreateCubeEntity: return "entity.create_primitive.cube";
        case EditorCommandKind::CreatePlaneEntity: return "entity.create_primitive.plane";
        case EditorCommandKind::CreateCylinderEntity: return "entity.create_primitive.cylinder";
        case EditorCommandKind::CreateSphereEntity: return "entity.create_primitive.sphere";
        case EditorCommandKind::CreateTriggerEntity: return "entity.create_trigger";
        case EditorCommandKind::CreatePointLightEntity: return "entity.create_point_light";
        case EditorCommandKind::DuplicateEntity: return "entity.duplicate";
        case EditorCommandKind::DeleteEntity: return "entity.delete";
        case EditorCommandKind::SelectEntity: return "entity.select";
        case EditorCommandKind::ReorderEntity: return "entity.reorder";
        case EditorCommandKind::ReparentEntity: return "entity.reparent";
        case EditorCommandKind::UnparentEntity: return "entity.unparent";
        case EditorCommandKind::MoveEntityTransform: return "entity.transform.move";
        case EditorCommandKind::RotateEntityTransform: return "entity.transform.rotate";
        case EditorCommandKind::ScaleEntityTransform: return "entity.transform.scale";
        case EditorCommandKind::SetEntityTransform: return "entity.transform.set";
        case EditorCommandKind::SetEntityMetadata: return "entity.metadata.set";
        case EditorCommandKind::AddComponent: return "entity.component.add";
        case EditorCommandKind::RemoveComponent: return "entity.component.remove";
        case EditorCommandKind::SetComponentProperty: return "entity.component.property.set";
        case EditorCommandKind::CreatePrefab: return "prefab.create_from_entity";
        case EditorCommandKind::ApplyPrefab: return "prefab.apply";
        case EditorCommandKind::RevertPrefab: return "prefab.revert";
        case EditorCommandKind::UnpackPrefab: return "prefab.unpack";
        case EditorCommandKind::InstantiatePrefab: return "prefab.instantiate";
        case EditorCommandKind::CreateSpriteClip: return "sprite_clip.create";
        case EditorCommandKind::DuplicateSpriteClip: return "sprite_clip.duplicate";
        case EditorCommandKind::DeleteSpriteClip: return "sprite_clip.delete";
        case EditorCommandKind::EnterPlayMode: return "play.enter";
        case EditorCommandKind::ExitPlayMode: return "play.exit";
        case EditorCommandKind::PausePlayMode: return "play.pause";
        case EditorCommandKind::ResumePlayMode: return "play.resume";
        case EditorCommandKind::StepPlayMode: return "play.step";
        case EditorCommandKind::DropPrefabIntoScene: return "asset.drop.prefab_into_scene";
        case EditorCommandKind::AssignTextureAsset: return "asset.assign.texture";
        case EditorCommandKind::CreateMaterialAsset: return "asset.material.create";
        case EditorCommandKind::SaveMaterialAsset: return "asset.material.save";
        case EditorCommandKind::AssignMaterialAsset: return "asset.assign.material";
        case EditorCommandKind::SaveScene: return "scene.save";
        case EditorCommandKind::OpenScene: return "scene.open";
        case EditorCommandKind::Undo: return "editor.undo";
        case EditorCommandKind::Redo: return "editor.redo";
        case EditorCommandKind::RefreshAssets: return "asset.refresh";
        case EditorCommandKind::MoveAsset: return "asset.move";
        case EditorCommandKind::RepairAssetFallbacks: return "asset.repair_fallbacks";
        case EditorCommandKind::SetProjectSetting: return "project.setting.set";
        case EditorCommandKind::CreateUiDocument: return "ui.document.create";
        case EditorCommandKind::AddUiNode: return "ui.node.add";
        case EditorCommandKind::SetUiNode: return "ui.node.set";
        case EditorCommandKind::ReparentUiNode: return "ui.node.reparent";
        case EditorCommandKind::DeleteUiNode: return "ui.node.delete";
        case EditorCommandKind::AttachManagedScript: return "managed.script.attach";
        case EditorCommandKind::WriteManagedSource: return "managed.source.write";
        case EditorCommandKind::RunQaScenario: return "qa.scenario.run";
        case EditorCommandKind::InjectRuntimeInput: return "runtime.input.inject";
        case EditorCommandKind::InjectRuntimeUi: return "runtime.ui.inject";
        case EditorCommandKind::ClearRuntimeEvents: return "runtime.events.clear";
        case EditorCommandKind::BuildManagedScripts: return "managed.build";
        case EditorCommandKind::ExportProject: return "project.export";
        case EditorCommandKind::Unknown: break;
    }
    return "editor.unknown";
}

} // namespace vespera::editor
