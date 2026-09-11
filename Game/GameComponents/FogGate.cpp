#include "FogGate.h"

#include <assets/NoiseTexture.h>
#include <components/ModelRendererComponent.h>
#include <cmath>

using namespace KujataEngine;

void FogGate::OnPlayStart() {
	// Playインスタンスはコンポーネントを使い回すので、非シリアライズの状態は必ず戻す。
	renderer_ = nullptr;
	generated_ = false;
	elapsed_ = 0.0f;
	offsetX_ = 0.0f;
	offsetY_ = 0.0f;
}

void FogGate::RegisterInvokableMethods(InvokableMethodRegistry& registry) { registry.Add("Regenerate", [this]() { Regenerate(); }); }

void FogGate::Regenerate() { generated_ = false; }

void FogGate::Update() {
	if (!renderer_) {
		renderer_ = GetComponent<ModelRendererComponent>();
		if (!renderer_) {
			return; // ModelRendererの無いGameObjectに付いている。何もしない。
		}
	}

	if (!generated_) {
		// **テクスチャ生成はコマンドリスト実行を伴うので描画パス外(=ここ)で行う。**
		// 同じ設定なら NoiseTexture 側がキャッシュを返すので、扉が何枚あっても生成は1回で済む。
		const uint32_t size = static_cast<uint32_t>((textureSize_ < 32) ? 32 : textureSize_);
		const uint32_t textureIndex = NoiseTexture::GenerateFogSheetTexture(size, seed_, frequency_, contrast_);
		renderer_->SetTextureOverride(textureIndex);
		// エミッションマップにも同じ物を入れる。入れないと面全体が一様に光り、模様が白飛びで潰れる。
		renderer_->SetEmissiveTextureOverride(textureIndex);
		generated_ = true;
	}

	// 霧はゲーム内時間に左右させない。チュートリアルのポップアップ表示中(timeScale=0)に
	// 扉だけ凍りつくと、止まっているのが背景ではなく世界の方だと分かってしまう。
	const float deltaTime = Time::GetUnscaledDeltaTime();
	elapsed_ += deltaTime;

	offsetX_ += scrollX_ * deltaTime;
	offsetY_ += scrollY_ * deltaTime;
	// **1周ぶんで折り返す。** 放っておくとfloatの桁が溢れて模様がガタつく。
	offsetX_ -= std::floor(offsetX_);
	offsetY_ -= std::floor(offsetY_);
	renderer_->SetUVTransformOverride({offsetX_, offsetY_}, {uvScale_, uvScale_}, 0.0f);

	// 明るさのゆらぎ。ランダムではなくノイズなのは、値が連続していてチラつかないため。
	// PerlinNoiseの実測範囲は概ね±0.8なので、振幅はそのつもりで掛ける。
	const float noise = PerlinNoise(elapsed_ * pulseSpeed_, lane_);
	const float intensity = glowIntensity_ * (1.0f + noise * pulseAmplitude_);
	renderer_->SetEmissiveOverride(glowColor_, (intensity > 0.0f) ? intensity : 0.0f);
}
