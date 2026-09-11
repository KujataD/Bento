#include "TorchFlicker.h"

#include <components/PointLightComponent.h>

using namespace KujataEngine;

void TorchFlicker::OnPlayStart() {
	// Playインスタンスは使い回されるので、基準値を取り直す。
	// **ここで戻さないと、前回Playで揺らした後の値を基準にしてしまい、Playのたびに明るさがずれていく。**
	light_ = nullptr;
	baseIntensity_ = 0.0f;
	elapsed_ = 0.0f;
}

void TorchFlicker::Update() {
	if (!light_) {
		light_ = GetComponent<PointLightComponent>();
		if (!light_) {
			return;
		}
		baseIntensity_ = light_->GetData().intensity;
	}

	// ポーズ中に炎が止まると作り物に見えるので実時間で進める。
	elapsed_ += Time::GetUnscaledDeltaTime();

	// 第2引数(レーン)を松明ごとに変えることで、同じ時間でも別々に揺れる。
	// PerlinNoiseの実測範囲は概ね±0.8なので、振幅はそのつもりで掛ける。
	const float noise = PerlinNoise(elapsed_ * speed_, lane_);
	light_->GetData().intensity = baseIntensity_ * (1.0f + noise * amplitude_);
}
