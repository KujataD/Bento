#pragma once

#include "../runtime/KujataApi.h"
#include "../2d/UIQuad.h"
#include "../2d/UIRect.h"
#include "../math/Vector4.h"
#include "../scene/Component.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace KujataEngine {

class FontAtlas;

/// <summary>
/// UIのテキスト描画(UnityのText相当)。stb_truetypeのフォントアトラスからグリフ矩形を並べて描く。
/// </summary>
class KUJATA_API TextComponent : public Component {
public:
	// クラス全体をdllexportすると暗黙のコピー代入まで実体化されるため、
	// unique_ptrメンバを持つこのクラスでは明示的にコピーを禁止する(元々コピーしない設計)。
	TextComponent() = default;
	TextComponent(const TextComponent&) = delete;
	TextComponent& operator=(const TextComponent&) = delete;

	enum class Align {
		Left,
		Center,
		Right,
	};

	const char* GetTypeName() const override { return "Text"; }
	bool AllowMultiple() const override { return false; }

	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;
	void OnAfterReadJson() override;

	/// <summary>描画パス外で呼ぶ。フォントアトラスのベイク(コマンドリスト実行を伴う)を行う。</summary>
	void Prepare();

	void DrawUI(const UIRect& canvasRect, float scaleFactor);

	/// <summary>表示文字列を設定/取得(ゲームロジックからスコア表示等に使う)。</summary>
	void SetText(const std::string& text);
	const std::string& GetText() const { return text_; }
	void SetFontSize(float size) { fontSize_ = size; }

	/// <summary>文字色。演出でフェードさせるときはαを動かす。</summary>
	void SetColor(const Vector4& color) { color_ = color; }
	const Vector4& GetColor() const { return color_; }

private:
	void SyncTextBuffer();
	void SyncFontBuffer();

	std::string text_ = "Text";
	// 既定は**Data配下の論理名**。実体は `Data/Fonts/` に置き、無ければ見た目の近い
	// システムフォントへ落ちる([[FontAtlas]]::ResolveFontPath)。
	// **絶対パスを既定にしない**のは、その環境にしか無いフォントを配布物へ持ち込まないため。
	std::string fontPath_ = "Fonts/JapaneseSans.ttf";
	float fontSize_ = 28.0f;
	Vector4 color_ = {1.0f, 1.0f, 1.0f, 1.0f};
	Align align_ = Align::Left;

	std::array<char, 256> textBuffer_{};
	std::array<char, 256> fontBuffer_{};
	bool textBufferSynced_ = false;
	FontAtlas* fontAtlas_ = nullptr; // Prepareでベイク/取得したアトラス(描画時はこれを使う)
	// グリフ描画用の再利用プール。UIQuadはGPUバッファの生ポインタを持つため、
	// vector再確保でムーブ/コピーされないようunique_ptrでアドレスを固定する。
	std::vector<std::unique_ptr<UIQuad>> glyphQuads_;
};

} // namespace KujataEngine
