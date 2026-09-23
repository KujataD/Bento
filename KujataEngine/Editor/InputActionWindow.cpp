#include "InputActionWindow.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#include "../runtime/InputActionSystem.h"
#include "EditorApplication.h"
#include <string>
#endif // USE_IMGUI

namespace KujataEngine {

void InputActionWindow::Draw(bool* pOpen) {
#ifdef USE_IMGUI
	if (!ImGui::Begin("Input Actions", pOpen)) {
		ImGui::End();
		return;
	}

	InputActionSystem* system = InputActionSystem::GetInstance();

	ImGui::TextDisabled("割り当ては Data/ProjectSettings/InputActions.json で決めます");
	ImGui::TextWrapped("%s", system->GetSourceDescription().c_str());
	if (ImGui::Button("Reload")) {
		system->Reload();
	}
	ImGui::SameLine();
	bool deviceInput = system->IsDeviceInputEnabled();
	if (ImGui::Checkbox("Device Input", &deviceInput)) {
		system->SetDeviceInputEnabled(deviceInput);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("OFF にすると、キーボード・パッドを読みません(下のボタンや CUI の action.set、AI だけで動かせます)。");
	}
	if (!EditorApplication::GetInstance()->IsPlaying()) {
		ImGui::TextDisabled("Play 中だけ値が動きます(コマンドは Play の更新の頭で実行されます)");
	}

	ActionState& actions = system->GetActions();
	if (ImGui::BeginTable("actions", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Action");
		ImGui::TableSetupColumn("Type");
		ImGui::TableSetupColumn("Bindings");
		ImGui::TableSetupColumn("Value");
		ImGui::TableHeadersRow();

		for (const InputActionDef& action : system->GetDefinitions()) {
			ImGui::TableNextRow();
			ImGui::PushID(action.name.c_str());

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(action.name.c_str());

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(InputActionAsset::ToString(action.type));

			ImGui::TableNextColumn();
			std::string bindings;
			for (const std::string& binding : action.bindings) {
				bindings += (bindings.empty() ? "" : ", ") + binding;
			}
			ImGui::TextUnformatted(bindings.c_str());

			ImGui::TableNextColumn();
			if (action.type == InputActionType::Axis2D) {
				const Vector2 value = actions.Axis2D(action.name);
				ImGui::Text("%.2f, %.2f", value.x, value.y);
			} else {
				ImGui::Text("%.2f", actions.Axis1D(action.name));
			}
			// 手で流して確かめる(CUI の action.set と同じ入口)。
			ImGui::SameLine();
			if (action.type == InputActionType::Button) {
				if (ImGui::SmallButton("Press")) {
					system->PushAction(action.name, 1.0f, 0.0f, kCommandSourceEditor);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Release")) {
					system->PushAction(action.name, 0.0f, 0.0f, kCommandSourceEditor);
				}
			} else {
				if (ImGui::SmallButton("+1")) {
					system->PushAction(action.name, 1.0f, 0.0f, kCommandSourceEditor);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("-1")) {
					system->PushAction(action.name, -1.0f, 0.0f, kCommandSourceEditor);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("0")) {
					system->PushAction(action.name, 0.0f, 0.0f, kCommandSourceEditor);
				}
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Separator();
	ImGui::Text("待っているコマンド: %zu / 前の更新で実行: %zu", system->GetQueue().GetPendingCount(), system->GetQueue().GetLastExecutedCount());

	ImGui::End();
#else
	(void)pOpen;
#endif // USE_IMGUI
}

} // namespace KujataEngine
