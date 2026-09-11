#include "RectTransformComponent.h"

#include "../runtime/InspectorUI.h"

namespace KujataEngine {
namespace {

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key) || !json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

Vector2 ReadVector2(const nlohmann::json& json, const char* key, const Vector2& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	const nlohmann::json& value = json.at(key);
	if (!value.is_array() || value.size() < 2 || !value[0].is_number() || !value[1].is_number()) {
		return defaultValue;
	}
	return {value[0].get<float>(), value[1].get<float>()};
}

} // namespace

UIRect RectTransformComponent::ComputeRect(const UIRect& parentRect) const {
	// 親矩形内のアンカー基準点(左上/右下)。
	const float aMinX = parentRect.x + anchorMin_.x * parentRect.width;
	const float aMinY = parentRect.y + anchorMin_.y * parentRect.height;
	const float aMaxX = parentRect.x + anchorMax_.x * parentRect.width;
	const float aMaxY = parentRect.y + anchorMax_.y * parentRect.height;

	// サイズ = アンカー間距離 + sizeDelta(点アンカーならsizeDeltaがそのままサイズ)。
	const float width = (aMaxX - aMinX) + sizeDelta_.x;
	const float height = (aMaxY - aMinY) + sizeDelta_.y;

	// ピボット位置 = アンカー矩形上のピボット割合の点 + anchoredPosition。
	const float anchorRefX = aMinX + (aMaxX - aMinX) * pivot_.x;
	const float anchorRefY = aMinY + (aMaxY - aMinY) * pivot_.y;
	const float pivotPosX = anchorRefX + anchoredPosition_.x;
	const float pivotPosY = anchorRefY + anchoredPosition_.y;

	UIRect rect;
	rect.x = pivotPosX - pivot_.x * width;
	rect.y = pivotPosY - pivot_.y * height;
	rect.width = width;
	rect.height = height;
	return rect;
}

void RectTransformComponent::DrawInspector() {
#ifdef USE_IMGUI
	// Vector2はx,y連続なので&x をfloat[2]として扱える。
	InspectorUI::DragFloat2("Anchor Min", &anchorMin_.x, 0.01f, 0.0f, 1.0f);
	InspectorUI::DragFloat2("Anchor Max", &anchorMax_.x, 0.01f, 0.0f, 1.0f);
	InspectorUI::DragFloat2("Pivot", &pivot_.x, 0.01f, 0.0f, 1.0f);
	InspectorUI::DragFloat2("Pos (X,Y)", &anchoredPosition_.x, 1.0f);
	InspectorUI::DragFloat2("Width/Height", &sizeDelta_.x, 1.0f);
	InspectorUI::DragFloat("Rotation Z (rad)", &rotationZ_, 0.01f);
	InspectorUI::ItemTooltip("この要素の矩形中心まわりの回転[rad]。**子には伝播しない**(現状Imageのみ反映)。");

	// アンカープリセット: 3x3の四角ボタングリッド(Unity風)。押すとアンカー/ピボットを対応位置へ。
	InspectorUI::TextUnformatted("Anchor Presets:");
	const float cellSize = 26.0f;
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			char id[8];
			id[0] = '#';
			id[1] = '#';
			id[2] = 'p';
			id[3] = static_cast<char>('0' + row * 3 + col);
			id[4] = '\0';
			if (InspectorUI::ButtonSized(id, cellSize, cellSize)) {
				const float ax = static_cast<float>(col) * 0.5f;
				const float ay = static_cast<float>(row) * 0.5f;
				anchorMin_ = {ax, ay};
				anchorMax_ = {ax, ay};
				pivot_ = {ax, ay};
			}
			if (col < 2) {
				InspectorUI::SameLine();
			}
		}
	}
	if (InspectorUI::Button("Stretch All")) {
		anchorMin_ = {0.0f, 0.0f};
		anchorMax_ = {1.0f, 1.0f};
		pivot_ = {0.5f, 0.5f};
		anchoredPosition_ = {0.0f, 0.0f};
		sizeDelta_ = {0.0f, 0.0f};
	}
#endif // USE_IMGUI
}

void RectTransformComponent::WriteJson(nlohmann::json& json) const {
	json["anchorMin"] = {anchorMin_.x, anchorMin_.y};
	json["anchorMax"] = {anchorMax_.x, anchorMax_.y};
	json["pivot"] = {pivot_.x, pivot_.y};
	json["anchoredPosition"] = {anchoredPosition_.x, anchoredPosition_.y};
	json["sizeDelta"] = {sizeDelta_.x, sizeDelta_.y};
	json["rotationZ"] = rotationZ_;
	json["scale"] = {scale_.x, scale_.y};
}

void RectTransformComponent::ReadJson(const nlohmann::json& json) {
	anchorMin_ = ReadVector2(json, "anchorMin", anchorMin_);
	anchorMax_ = ReadVector2(json, "anchorMax", anchorMax_);
	pivot_ = ReadVector2(json, "pivot", pivot_);
	anchoredPosition_ = ReadVector2(json, "anchoredPosition", anchoredPosition_);
	sizeDelta_ = ReadVector2(json, "sizeDelta", sizeDelta_);
	rotationZ_ = ReadFloat(json, "rotationZ", rotationZ_);
	scale_ = ReadVector2(json, "scale", scale_);
}

} // namespace KujataEngine
