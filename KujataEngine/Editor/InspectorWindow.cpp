#include "InspectorWindow.h"

#include "../../externals/imgui/imgui.h"
#include "../../externals/imsearch/imsearch.h"
#include "../components/AnimatorComponent.h"
#include "../scene/Component.h"
#include "../scene/ComponentFactory.h"
#include "../scene/IMaterialTarget.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "../runtime/AnimationRecordingState.h"
#include "../runtime/TagRegistry.h"
#include "EditorApplication.h"
#include "EditorConsole.h"
#include "EditorImGuiUtil.h"
#include "EditorSelection.h"
#include "MaterialInspector.h"
#include "PrefabAsset.h"
#include "ProjectWindow.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace KujataEngine {
namespace {

void AcceptMaterialDropForComponent(Component* component, GameObject* owner) {
	if (!component) {
		return;
	}
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}

	const ImGuiPayload* materialPayload = ImGui::AcceptDragDropPayload(kProjectMaterialDragPayloadType);
	if (materialPayload && materialPayload->DataSize > 0) {
		const char* materialPathText = static_cast<const char*>(materialPayload->Data);
		IMaterialTarget* materialTarget = dynamic_cast<IMaterialTarget*>(component);
		if (materialTarget && materialTarget->ApplyMaterialAsset(materialPathText)) {
			if (owner) {
				EditorSelection::GetInstance()->SetSelectedGameObject(owner);
			}
			EditorConsole::GetInstance()->AddLog("[Material] Assigned: " + std::string(materialPathText));
		}
	}

	// Animation Clip(*.anim.json)はAnimatorComponentへ追加する。
	const ImGuiPayload* animClipPayload = ImGui::AcceptDragDropPayload(kProjectAnimClipDragPayloadType);
	if (animClipPayload && animClipPayload->DataSize > 0) {
		const char* clipPathText = static_cast<const char*>(animClipPayload->Data);
		AnimatorComponent* animator = dynamic_cast<AnimatorComponent*>(component);
		if (animator) {
			animator->SetClipPath(clipPathText);
			EditorConsole::GetInstance()->AddLog("[Animation] Clip added: " + std::string(clipPathText));
		}
	}

	ImGui::EndDragDropTarget();
}

} // namespace

void InspectorWindow::Draw(ProjectWindow& projectWindow, bool* pOpen) {
#ifdef USE_IMGUI
	ImGui::Begin("Inspector", pOpen);

	EditorSelection* selection = EditorSelection::GetInstance();
	if (selection->GetSelectedAssetType() == AssetType::Material) {
		DrawMaterialAssetInspector(projectWindow);
		ImGui::End();
		return;
	}

	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		EditorSelection::GetInstance()->Clear();
		ImGui::TextDisabled("No Scene.");
		ImGui::End();
		return;
	}

	GameObject* selected = selection->GetSelectedGameObject();
	if (!selected) {
		ImGui::TextDisabled("No GameObject selected.");
		ImGui::End();
		return;
	}

	std::array<char, 128> nameBuffer{};
	std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", selected->GetName().c_str());
	if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size())) {
		selected->SetName(nameBuffer.data());
	}
	if (ImGui::IsItemDeactivatedAfterEdit() && !inspectorEditing_) {
		CaptureUndo(*scene, "Inspector");
		inspectorEditing_ = true;
	}

	// Tag: 登録リストから選択(Unity風)。末尾の "Add Tag..." で新規追加できる。
	TagRegistry& tagRegistry = TagRegistry::GetInstance();
	tagRegistry.EnsureTag(selected->GetTag()); // Scene由来の未登録タグも選択肢に含める
	bool openAddTagPopup = false;
	ImGui::SetNextItemWidth(125.0f);
	if (ImGui::BeginCombo("Tag", selected->GetTag().c_str())) {
		for (const std::string& tag : tagRegistry.GetTags()) {
			bool isSelected = (tag == selected->GetTag());
			if (ImGui::Selectable(tag.c_str(), isSelected)) {
				selected->SetTag(tag);
				if (!inspectorEditing_) {
					CaptureUndo(*scene, "Inspector");
					inspectorEditing_ = true;
				}
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::Separator();
		if (ImGui::Selectable("Add Tag...")) {
			openAddTagPopup = true;
		}
		ImGui::EndCombo();
	}
	if (openAddTagPopup) {
		ImGui::OpenPopup("Add Tag");
	}
	if (ImGui::BeginPopup("Add Tag")) {
		static std::array<char, 64> newTagBuffer{};
		ImGui::SetNextItemWidth(160.0f);
		bool submitted = ImGui::InputText("##NewTag", newTagBuffer.data(), newTagBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if ((ImGui::Button("Add") || submitted) && newTagBuffer[0] != '\0') {
			std::string newTag = newTagBuffer.data();
			tagRegistry.AddTag(newTag);
			selected->SetTag(newTag);
			newTagBuffer.fill('\0');
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	int layer = static_cast<int>(selected->GetLayer());
	ImGui::SetNextItemWidth(24.0f);
	if (ImGui::DragInt("Layer", &layer, 1.0f, 0, 31)) {
		if (layer < 0) {
			layer = 0;
		}
		if (layer > 31) {
			layer = 31;
		}
		selected->SetLayer(static_cast<uint32_t>(layer));
	}
	if (ImGui::IsItemDeactivatedAfterEdit() && !inspectorEditing_) {
		CaptureUndo(*scene, "Inspector");
		inspectorEditing_ = true;
	}

	bool active = selected->IsActive();
	if (ImGui::Checkbox("Active", &active)) {
		selected->SetActive(active);
	}
	if (ImGui::IsItemDeactivatedAfterEdit() && !inspectorEditing_) {
		CaptureUndo(*scene, "Inspector");
		inspectorEditing_ = true;
	}

	if (selected->IsPrefabInstance()) {
		ImGui::Separator();
		GameObject* prefabRoot = PrefabAsset::FindPrefabInstanceRoot(*scene, *selected);
		ImGui::TextColored(ImVec4(0.47f, 0.74f, 0.68f, 1.0f), "Prefab Instance");
		ImGui::TextWrapped("%s", selected->GetPrefabAssetPath().c_str());

		if (prefabRoot && prefabRoot != selected) {
			if (ImGui::Button("Select Prefab Root")) {
				EditorSelection::GetInstance()->SetSelectedGameObject(prefabRoot);
			}
		}

		if (ImGui::Button("Open Prefab")) {
			GameObject* openRoot = prefabRoot;
			if (!openRoot) {
				openRoot = selected;
			}
			EditorApplication::GetInstance()->OpenPrefabEditMode(openRoot->GetPrefabAssetPath());
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply")) {
			PrefabAsset::SaveResult result = PrefabAsset::ApplyPrefabInstance(*scene, *selected);
			if (result.succeeded) {
				EditorConsole::GetInstance()->AddLog("[Prefab] Applied: " + result.outputPath.string());
				projectWindow.Refresh();
			} else {
				EditorConsole::GetInstance()->AddLog("[Prefab] Apply failed: " + result.message);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Revert")) {
			PrefabAsset::InstantiateResult result = PrefabAsset::RevertPrefabInstance(*scene, *selected);
			if (result.succeeded) {
				EditorSelection::GetInstance()->SetSelectedGameObject(result.rootObject);
				EditorConsole::GetInstance()->AddLog("[Prefab] Reverted.");
			} else {
				EditorConsole::GetInstance()->AddLog("[Prefab] Revert failed: " + result.message);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Unpack")) {
			if (PrefabAsset::UnpackPrefabInstance(*scene, *selected)) {
				EditorConsole::GetInstance()->AddLog("[Prefab] Unpacked.");
			}
		}
	}

	ImGui::Separator();
	std::vector<std::unique_ptr<Component>>& components = selected->GetComponents();
	if (components.empty()) {
		ImGui::TextDisabled("No Components.");
	}

	Component* removeTarget = nullptr;
	for (const std::unique_ptr<Component>& component : components) {
		if (!component) {
			continue;
		}

		ImGui::PushID(component.get());
		bool headerOpen = ImGui::CollapsingHeader(component->GetTypeName(), ImGuiTreeNodeFlags_DefaultOpen);

		// コンポーネントヘッダーの右クリックでRemove Componentを提供(常時ボタン表示をやめて隠蔽)。
		if (ImGui::BeginPopupContextItem("ComponentContextMenu")) {
			if (component->CanRemove()) {
				if (ImGui::MenuItem("Remove Component")) {
					removeTarget = component.get();
				}
			} else {
				ImGui::TextDisabled("Required Component");
			}
			ImGui::EndPopup();
		}

		if (headerOpen) {
			bool enabled = component->IsEnabled();
			if (ImGui::Checkbox("Enabled", &enabled)) {
				component->SetEnabled(enabled);
			}

			// 録画対象のComponent文脈を設定する(Animator自身は記録対象外)。
			// 録画中はUnityの赤ティントのようにフィールド背景を赤くする。
			bool recordable = std::strcmp(component->GetTypeName(), "AnimatorComponent") != 0;
			SetAnimationRecordingComponentContext(recordable ? component->GetTypeName() : nullptr);
			bool tintRecording = recordable && IsAnimationRecording() && IsAnimationKeyContextEnabled();
			if (tintRecording) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.55f, 0.16f, 0.16f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.65f, 0.22f, 0.22f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.75f, 0.28f, 0.28f, 0.95f));
			}

			component->DrawInspector();

			if (tintRecording) {
				ImGui::PopStyleColor(3);
			}
			SetAnimationRecordingComponentContext(nullptr);

			AcceptMaterialDropForComponent(component.get(), selected);
		}
		ImGui::PopID();
	}

	if (removeTarget) {
		selected->RemoveComponent(removeTarget);
	}

	ImGui::Separator();
	if (ImGui::Button("Add Component")) {
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup")) {
		const std::vector<std::string>& typeNames = ComponentFactory::GetInstance().GetRegisteredTypeNames();
		if (typeNames.empty()) {
			ImGui::TextDisabled("No registered Components.");
		} else if (ImSearch::BeginSearch()) {
			// ImSearchの検索バーで登録Componentを絞り込み、MenuItemで追加する。
			ImSearch::SearchBar("Search Component");
			for (const std::string& typeName : typeNames) {
				ImSearch::SearchableItem(typeName.c_str(), [&](const char* name) {
					if (ImGui::MenuItem(name)) {
						Component* added = selected->AddComponent(ComponentFactory::GetInstance().Create(name));
						scene->OnEditorComponentAdded(selected, added);
						ImGui::CloseCurrentPopup();
					}
				});
			}
			ImSearch::EndSearch();
		}

		ImGui::EndPopup();
	}

	if (!ImGui::IsAnyItemActive()) {
		inspectorEditing_ = false;
	}

	ImGui::End();
#else
	(void)projectWindow;
	(void)pOpen;
#endif // USE_IMGUI
}

} // namespace KujataEngine
