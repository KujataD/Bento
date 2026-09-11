#include "MistBackdrop.h"

#include <assets/NoiseTexture.h>
#include <components/ImageComponent.h>
#include <cmath>

using namespace KujataEngine;

void MistBackdrop::OnPlayStart() {
	// Playインスタンスはコンポーネントを使い回すので、非シリアライズの状態は必ず戻す。
	image_ = nullptr;
	generated_ = false;
	elapsed_ = 0.0f;
	offsetX_ = 0.0f;
	offsetY_ = 0.0f;
	baseAlphaCaptured_ = false;
}

void MistBackdrop::OnPlayStop() {
	// αはImageのシリアライズ対象。ゆらぎで書き換えた値のまま止めると、
	// **そのときの濃さがシーンの保存値になってしまう。** 必ず基準へ戻す。
	if (image_ && baseAlphaCaptured_) {
		Vector4 color = image_->GetColor();
		color.w = baseAlpha_;
		image_->SetColor(color);
	}
	baseAlphaCaptured_ = false;
}

void MistBackdrop::RegisterInvokableMethods(InvokableMethodRegistry& registry) {
	registry.Add("Regenerate", [this]() { Regenerate(); });
}

void MistBackdrop::Regenerate() {
	generated_ = false;
}

void MistBackdrop::Update() {
	if (!image_) {
		image_ = GetComponent<ImageComponent>();
		if (!image_) {
			return; // Imageの無いGameObjectに付いている。何もしない。
		}
	}

	if (!generated_) {
		// **テクスチャ生成はコマンドリスト実行を伴うので描画パス外(=ここ)で行う。**
		// 同じ設定なら NoiseTexture 側がキャッシュを返すので、作り直しても重くならない。
		const uint32_t size = static_cast<uint32_t>((textureSize_ < 32) ? 32 : textureSize_);
		image_->SetTextureIndexDirect(NoiseTexture::GenerateMistTexture(size, seed_, frequency_, contrast_));
		image_->SetUVScale({uvScale_, uvScale_});
		generated_ = true;
	}

	if (!baseAlphaCaptured_) {
		baseAlpha_ = image_->GetColor().w;
		baseAlphaCaptured_ = true;
	}

	// 靄はゲーム内時間に左右させない(ポーズ中も止まらない方が背景として自然)。
	const float deltaTime = Time::GetUnscaledDeltaTime();
	elapsed_ += deltaTime;
	offsetX_ += scrollX_ * deltaTime;
	offsetY_ += scrollY_ * deltaTime;
	// **1周ぶんで折り返す。** 放っておくとfloatの桁が溢れて模様がガタつく。
	offsetX_ -= std::floor(offsetX_);
	offsetY_ -= std::floor(offsetY_);
	image_->SetUVOffset({offsetX_, offsetY_});

	// 濃さのゆらぎ。**横に流すだけでは「動いている」と気付かれにくい**ので、
	// 濃くなったり薄くなったりを重ねて、動きそのものを目立たせる。
	// ランダムではなくノイズなのは、値が連続していてチラつかないため(実測範囲は概ね±0.8)。
	if (pulseAmplitude_ > 0.0f) {
		const float noise = PerlinNoise(elapsed_ * pulseSpeed_, pulseLane_);
		Vector4 color = image_->GetColor();
		color.w = baseAlpha_ * (1.0f + noise * pulseAmplitude_);
		color.w = (color.w < 0.0f) ? 0.0f : ((color.w > 1.0f) ? 1.0f : color.w);
		image_->SetColor(color);
	}
}
