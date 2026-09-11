#pragma once
#include <KujataEngine.h>

namespace KujataEngine {
class PointLightComponent;
}

/// <summary>
/// 同じGameObjectのPointLightの明るさをゆらす。松明の炎に見せるためのもの。
///
/// 揺れは `PerlinNoise(time * speed, lane)`。**第2引数(レーン)を松明ごとに変える**と、
/// 同じ時間を入れても互いに無関係に揺れる(全部が同時に明滅すると一気に嘘くさくなる)。
/// ランダムではなくノイズなのは、値が連続していてチラつかないため。
///
/// 発光する板(Material側のemissive)は触らない。ここは光量だけを扱う。
/// </summary>
class TorchFlicker : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "TorchFlicker"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(amplitude_, "Amplitude", 0.01f, 0.0f, 1.0f,
		    "揺れ幅(基準の明るさに対する割合)。**0.2を超えると点滅に見えて落ち着かない。**");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(speed_, "Speed", 0.1f, 0.1f, 20.0f, "揺れの速さ。炎らしいのは3〜6あたり。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(lane_, "Lane", 1.0f, -1000.0f, 1000.0f,
		    "ノイズのレーン。**松明ごとに必ず別の値**にする(同じだと全部が揃って明滅する)。");
	}

	KUJATA_FIELD_FLOAT(amplitude_, 0.14f);
	KUJATA_FIELD_FLOAT(speed_, 4.0f);
	KUJATA_FIELD_FLOAT(lane_, 0.0f);

	// --- 実行時状態 ---
	KujataEngine::PointLightComponent* light_ = nullptr;
	// 揺らす前の明るさ。Play開始時に控えて、以後これを基準に上下させる。
	float baseIntensity_ = 0.0f;
	float elapsed_ = 0.0f;
};
