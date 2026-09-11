#pragma once

#include "../2d/UIQuad.h"
#include "../2d/UIRect.h"
#include "../math/Vector2.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include <array>
#include <cstdint>
#include <string>

namespace KujataEngine {

/// <summary>
/// UIのSprite描画(UnityのImage相当)。RectTransformの矩形にテクスチャを色付きで描く。
/// </summary>
class KUJATA_API ImageComponent : public Component {
public:
	const char* GetTypeName() const override { return "Image"; }
	bool AllowMultiple() const override { return false; }

	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;
	void OnAfterReadJson() override;

	/// <summary>描画パス外で呼ぶ。テクスチャの読み込み(コマンドリスト実行を伴う)を行う。</summary>
	void Prepare();

	/// <summary>UICanvasRendererから呼ばれる。キャンバス単位の矩形をscaleFactorで実ピクセル化して描画。</summary>
	void DrawUI(const UIRect& canvasRect, float scaleFactor);

	bool IsRaycastTarget() const { return raycastTarget_; }

	void SetTexture(const std::string& assetId, const std::string& path);

	/// <summary>
	/// 手続き生成テクスチャ(NoiseTextureなど、ファイルに無いもの)のSRV番号を直接差し込む。
	/// 以後このImageはパスからの読み込みを行わない(Prepareが上書きしない)。
	/// </summary>
	void SetTextureIndexDirect(uint32_t textureIndex);
	void SetColor(const Vector4& color) { color_ = color; }
	const Vector4& GetColor() const { return color_; }

	/// <summary>横方向Fill率[0,1]。1未満で左端固定のまま右から欠ける(UnityのImage.fillAmount相当)。</summary>
	void SetFillAmount(float fillAmount);
	float GetFillAmount() const { return fillAmount_; }

	/// <summary>UVトランスフォーム(スクロール演出やアトラスの部分表示用)。既定はオフセット0・スケール1・回転0。</summary>
	void SetUVOffset(const Vector2& offset) { uvOffset_ = offset; }
	const Vector2& GetUVOffset() const { return uvOffset_; }
	void SetUVScale(const Vector2& scale) { uvScale_ = scale; }
	const Vector2& GetUVScale() const { return uvScale_; }
	/// <summary>UV回転(ラジアン)。</summary>
	void SetUVRotation(float rotation) { uvRotation_ = rotation; }
	float GetUVRotation() const { return uvRotation_; }

private:
	void EnsureTextureLoaded();
	void SyncPathBuffer();

	std::string textureAssetId_;
	std::string texturePath_ = "Resources/white1x1.png";
	Vector4 color_ = {1.0f, 1.0f, 1.0f, 1.0f};
	bool raycastTarget_ = true;
	float fillAmount_ = 1.0f;
	Vector2 uvOffset_ = {0.0f, 0.0f};
	Vector2 uvScale_ = {1.0f, 1.0f};
	float uvRotation_ = 0.0f;

	std::array<char, 256> pathBuffer_{};
	UIQuad quad_;
	bool quadInitialized_ = false;
	uint32_t textureIndex_ = 0;
	std::string loadedPath_;
	// 手続き生成テクスチャを差し込まれたか。trueの間はEnsureTextureLoadedを走らせない。
	bool textureIndexOverridden_ = false;
	bool textureResolved_ = false;
};

} // namespace KujataEngine
