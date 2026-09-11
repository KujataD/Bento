#include "../runtime/InspectorUI.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#include "../runtime/AnimationRecordingState.h"
#include "EditorImGuiUtil.h"
#include <cstring>
#include <string>
#endif

namespace KujataEngine::InspectorUI {

bool DragFloat(const char* label, float* value, float speed, float minValue, float maxValue, const char* format) {
#ifdef USE_IMGUI
	return ImGui::DragFloat(label, value, speed, minValue, maxValue, format);
#else
	(void)label;
	(void)value;
	(void)speed;
	(void)minValue;
	(void)maxValue;
	(void)format;
	return false;
#endif
}

bool DragFloat2(const char* label, float* values, float speed, float minValue, float maxValue, const char* format) {
#ifdef USE_IMGUI
	return ImGui::DragFloat2(label, values, speed, minValue, maxValue, format);
#else
	(void)label;
	(void)values;
	(void)speed;
	(void)minValue;
	(void)maxValue;
	(void)format;
	return false;
#endif
}

bool DragFloat3(const char* label, float* values, float speed, float minValue, float maxValue, const char* format) {
#ifdef USE_IMGUI
	return ImGui::DragFloat3(label, values, speed, minValue, maxValue, format);
#else
	(void)label;
	(void)values;
	(void)speed;
	(void)minValue;
	(void)maxValue;
	(void)format;
	return false;
#endif
}

bool DragInt(const char* label, int* value, float speed, int minValue, int maxValue) {
#ifdef USE_IMGUI
	return ImGui::DragInt(label, value, speed, minValue, maxValue);
#else
	(void)label;
	(void)value;
	(void)speed;
	(void)minValue;
	(void)maxValue;
	return false;
#endif
}

bool Checkbox(const char* label, bool* value) {
#ifdef USE_IMGUI
	return ImGui::Checkbox(label, value);
#else
	(void)label;
	(void)value;
	return false;
#endif
}

bool ColorEdit4(const char* label, float* values) {
#ifdef USE_IMGUI
	return ImGui::ColorEdit4(label, values);
#else
	(void)label;
	(void)values;
	return false;
#endif
}

bool Combo(const char* label, int* currentItem, const char* const* items, int itemCount) {
#ifdef USE_IMGUI
	return ImGui::Combo(label, currentItem, items, itemCount);
#else
	(void)label;
	(void)currentItem;
	(void)items;
	(void)itemCount;
	return false;
#endif
}

bool InputText(const char* label, char* buffer, std::size_t bufferSize) {
#ifdef USE_IMGUI
	return ImGui::InputText(label, buffer, bufferSize);
#else
	(void)label;
	(void)buffer;
	(void)bufferSize;
	return false;
#endif
}

bool Button(const char* label) {
#ifdef USE_IMGUI
	return ImGui::Button(label);
#else
	(void)label;
	return false;
#endif
}

bool ButtonSized(const char* label, float width, float height) {
#ifdef USE_IMGUI
	return ImGui::Button(label, ImVec2(width, height));
#else
	(void)label;
	(void)width;
	(void)height;
	return false;
#endif
}

#ifdef USE_IMGUI
namespace {

// 参照フィールド(ObjectField/ModelAssetField)共通:参照ボタンの右側に並べる要素
// (Clearボタン/ラベル)の幅を実測して確保する。固定幅だとラベル(例: "Target")が
// 右へ押し出されて見切れるため。
float CalcReferenceFieldReservedWidth(const char* label, bool withClearButton) {
	const ImGuiStyle& style = ImGui::GetStyle();
	float reservedWidth = 0.0f;
	if (withClearButton) {
		reservedWidth += ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
	}
	const float labelWidth = (label && label[0] != '\0') ? ImGui::CalcTextSize(label).x : 0.0f;
	reservedWidth += labelWidth + style.ItemSpacing.x;
	return reservedWidth;
}

} // namespace
#endif

bool ObjectField(const char* label, const char* currentName, void** outDroppedObject, bool* outCleared) {
#ifdef USE_IMGUI
	bool changed = false;
	ImGui::PushID(label);
	const char* display = (currentName && currentName[0] != '\0') ? currentName : "None";

	// 参照表示ボタン(ここへHierarchyのGameObjectをドロップする)。
	ImGui::Button(display, ImVec2(-CalcReferenceFieldReservedWidth(label, true), 0.0f));
	if (ImGui::BeginDragDropTarget()) {
		// Hierarchyのドラッグペイロード(GameObject*)を受け取る。
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KujataHierarchyGameObject");
		if (payload && payload->DataSize == sizeof(void*)) {
			if (outDroppedObject) {
				*outDroppedObject = *static_cast<void* const*>(payload->Data);
			}
			changed = true;
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear")) {
		if (outCleared) {
			*outCleared = true;
		}
		changed = true;
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(label);
	ImGui::PopID();
	return changed;
#else
	(void)label;
	(void)currentName;
	(void)outDroppedObject;
	(void)outCleared;
	return false;
#endif
}

bool ModelAssetField(const char* label, const char* currentDisplay, char* outBuffer, std::size_t outBufferSize) {
#ifdef USE_IMGUI
	bool changed = false;
	ImGui::PushID(label);
	const char* display = (currentDisplay && currentDisplay[0] != '\0') ? currentDisplay : "None";
	// 参照表示ボタン(ここへProjectのModelファイルをドロップする)。
	ImGui::Button(display, ImVec2(-CalcReferenceFieldReservedWidth(label, false), 0.0f));
	if (ImGui::BeginDragDropTarget()) {
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectModelDragPayloadType);
		if (payload && payload->DataSize > 0 && outBuffer && outBufferSize > 0) {
			strncpy_s(outBuffer, outBufferSize, static_cast<const char*>(payload->Data), _TRUNCATE);
			changed = true;
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(label);
	ImGui::PopID();
	return changed;
#else
	(void)label;
	(void)currentDisplay;
	(void)outBuffer;
	(void)outBufferSize;
	return false;
#endif
}

void AnimationFieldHook(const char* channelKey, const float* values, int count, bool changedByWidget) {
#ifdef USE_IMGUI
	if (!IsAnimationKeyContextEnabled() || !GetAnimationRecordingComponentContext()) {
		return;
	}
	if (!channelKey || !values || count < 1 || count > 4) {
		return;
	}

	bool shouldReport = changedByWidget && IsAnimationRecording();

	// 直前のフィールドの右クリックで"Add Keyframe"(録画中でなくてもキーを打てる)。
	std::string popupId = std::string("##AnimKey_") + channelKey;
	if (ImGui::BeginPopupContextItem(popupId.c_str())) {
		if (ImGui::MenuItem("Add Keyframe")) {
			shouldReport = true;
		}
		ImGui::EndPopup();
	}

	if (shouldReport) {
		static constexpr const char* kSuffixes[4] = {".x", ".y", ".z", ".w"};
		if (count == 1) {
			ReportAnimationChannelChange(channelKey, values[0]);
		} else {
			for (int i = 0; i < count; ++i) {
				ReportAnimationChannelChange(std::string(channelKey) + kSuffixes[i], values[i]);
			}
		}
	}
#else
	(void)channelKey;
	(void)values;
	(void)count;
	(void)changedByWidget;
#endif
}

void TextUnformatted(const char* text) {
#ifdef USE_IMGUI
	ImGui::TextUnformatted(text);
#else
	(void)text;
#endif
}

void TextDisabled(const char* text) {
#ifdef USE_IMGUI
	ImGui::TextDisabled("%s", text);
#else
	(void)text;
#endif
}

void SameLine() {
#ifdef USE_IMGUI
	ImGui::SameLine();
#endif
}

void PushId(const char* id) {
#ifdef USE_IMGUI
	ImGui::PushID(id);
#else
	(void)id;
#endif
}

void PopId() {
#ifdef USE_IMGUI
	ImGui::PopID();
#endif
}

void ItemTooltip(const char* text) {
#ifdef USE_IMGUI
	if (!text || text[0] == '\0') {
		return;
	}
	// 少し待ってから出す。ドラッグ操作の邪魔にならないようにするため。
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
		ImGui::SetTooltip("%s", text);
	}
#else
	(void)text;
#endif
}

} // namespace KujataEngine::InspectorUI
