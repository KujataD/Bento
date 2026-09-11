#include "TextComponent.h"

#include "../2d/FontAtlas.h"
#include "../runtime/InspectorUI.h"
#include <cstring>

namespace KujataEngine {
namespace {

Vector4 ReadVector4(const nlohmann::json& json, const char* key, const Vector4& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	const nlohmann::json& value = json.at(key);
	if (!value.is_array() || value.size() < 4) {
		return defaultValue;
	}
	for (int i = 0; i < 4; ++i) {
		if (!value[i].is_number()) {
			return defaultValue;
		}
	}
	return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

std::string ReadString(const nlohmann::json& json, const char* key, const std::string& defaultValue) {
	if (!json.contains(key) || !json.at(key).is_string()) {
		return defaultValue;
	}
	return json.at(key).get<std::string>();
}

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key) || !json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

} // namespace

void TextComponent::SyncTextBuffer() {
	std::memset(textBuffer_.data(), 0, textBuffer_.size());
	strncpy_s(textBuffer_.data(), textBuffer_.size(), text_.c_str(), _TRUNCATE);
}

void TextComponent::SyncFontBuffer() {
	std::memset(fontBuffer_.data(), 0, fontBuffer_.size());
	strncpy_s(fontBuffer_.data(), fontBuffer_.size(), fontPath_.c_str(), _TRUNCATE);
}

void TextComponent::SetText(const std::string& text) {
	text_ = text;
	textBufferSynced_ = false; // Inspectorのバッファは次回同期。
}

void TextComponent::Prepare() {
	// フォントアトラスの取得と、表示文字のグリフのベイク(コマンドリスト実行を伴う)は描画パス外のここで行う。
	fontAtlas_ = FontAtlas::GetOrCreate(fontPath_, fontSize_);
	if (fontAtlas_) {
		// 表示する文字(日本語含む)のグリフを必要に応じて動的にベイクしてアトラスを更新する。
		fontAtlas_->EnsureGlyphsForText(text_);
	}
}

void TextComponent::DrawUI(const UIRect& canvasRect, float scaleFactor) {
	FontAtlas* atlas = fontAtlas_; // Prepareで取得済みのものを使う(描画パス中に新規ベイクしない)。
	if (!atlas || text_.empty()) {
		return;
	}

	float totalWidth = 0.0f;
	std::vector<FontAtlas::GlyphQuad> glyphs = atlas->BuildText(text_, fontSize_, totalWidth);
	if (glyphs.empty()) {
		return;
	}

	// 水平方向の揃え、垂直方向は中央寄せ。すべてキャンバス単位で計算。
	float originX = canvasRect.x;
	if (align_ == Align::Center) {
		originX = canvasRect.x + (canvasRect.width - totalWidth) * 0.5f;
	} else if (align_ == Align::Right) {
		originX = canvasRect.x + (canvasRect.width - totalWidth);
	}
	const float originY = canvasRect.y + (canvasRect.height - fontSize_) * 0.5f;

	// プールを必要数まで拡張(unique_ptrなので既存要素のアドレスは動かない)。
	while (glyphQuads_.size() < glyphs.size()) {
		glyphQuads_.push_back(std::make_unique<UIQuad>());
	}

	for (size_t i = 0; i < glyphs.size(); ++i) {
		const FontAtlas::GlyphQuad& glyph = glyphs[i];
		UIQuad& quad = *glyphQuads_[i];
		quad.Initialize(); // 冪等
		const float cx = originX + glyph.rect.x;
		const float cy = originY + glyph.rect.y;
		quad.SetRect(cx * scaleFactor, cy * scaleFactor, glyph.rect.width * scaleFactor, glyph.rect.height * scaleFactor);
		quad.SetUV({glyph.u0, glyph.v0}, {glyph.u1, glyph.v1});
		quad.SetColor(color_);
		quad.SetTexture(atlas->GetTextureIndex());
		// フォントはアトラスのαのみをカバレッジに使い色はcolor_から取る(ストレートαブレンド)。
		quad.SetAlphaAsCoverage(true);
		quad.SetBlendMode(BlendMode::kNormal);
		quad.Draw();
	}
}

void TextComponent::DrawInspector() {
#ifdef USE_IMGUI
	if (!textBufferSynced_) {
		SyncTextBuffer();
		SyncFontBuffer();
		textBufferSynced_ = true;
	}
	// ラベルはComponentヘッダ"Text"とID衝突するため##で一意なIDにしつつ表示は"Text"。
	if (InspectorUI::InputText("Text##content", textBuffer_.data(), textBuffer_.size())) {
		text_ = textBuffer_.data();
	}
	InspectorUI::ColorEdit4("Color", &color_.x);
	InspectorUI::DragFloat("Font Size", &fontSize_, 0.5f, 1.0f, 256.0f);
	int alignIndex = static_cast<int>(align_);
	const char* alignItems[] = {"Left", "Center", "Right"};
	if (InspectorUI::Combo("Align", &alignIndex, alignItems, 3)) {
		align_ = static_cast<Align>(alignIndex);
	}

	// フォント選択: **Data配下の論理名**のプリセット + 任意パス入力。
	// 絶対パスを選ばせないのが要点で、`Data/Fonts/` に実体を置けばそれが使われ、
	// 無ければ見た目の近いシステムフォントへ落ちる([[FontAtlas]]::ResolveFontPath)。
	static const char* kFontNames[] = {"日本語 ゴシック", "日本語 ゴシック太", "日本語 明朝"};
	static const char* kFontPaths[] = {
	    "Fonts/JapaneseSans.ttf",
	    "Fonts/JapaneseSansBold.ttf",
	    "Fonts/JapaneseSerif.ttf",
	};
	constexpr int kFontCount = 3;
	int fontIndex = -1;
	for (int i = 0; i < kFontCount; ++i) {
		if (fontPath_ == kFontPaths[i]) {
			fontIndex = i;
			break;
		}
	}
	if (InspectorUI::Combo("Font", &fontIndex, kFontNames, kFontCount)) {
		if (fontIndex >= 0 && fontIndex < kFontCount) {
			fontPath_ = kFontPaths[fontIndex];
			SyncFontBuffer();
		}
	}
	if (InspectorUI::InputText("Font Path", fontBuffer_.data(), fontBuffer_.size())) {
		fontPath_ = fontBuffer_.data();
	}
#endif // USE_IMGUI
}

void TextComponent::WriteJson(nlohmann::json& json) const {
	json["text"] = text_;
	json["fontPath"] = fontPath_;
	json["fontSize"] = fontSize_;
	json["color"] = {color_.x, color_.y, color_.z, color_.w};
	json["align"] = static_cast<int>(align_);
}

void TextComponent::ReadJson(const nlohmann::json& json) {
	text_ = ReadString(json, "text", text_);
	fontPath_ = ReadString(json, "fontPath", fontPath_);
	fontSize_ = ReadFloat(json, "fontSize", fontSize_);
	color_ = ReadVector4(json, "color", color_);
	if (json.contains("align") && json.at("align").is_number_integer()) {
		int alignIndex = json.at("align").get<int>();
		if (alignIndex >= 0 && alignIndex <= 2) {
			align_ = static_cast<Align>(alignIndex);
		}
	}
}

void TextComponent::OnAfterReadJson() {
	SyncTextBuffer();
	SyncFontBuffer();
	textBufferSynced_ = true;
}

} // namespace KujataEngine
