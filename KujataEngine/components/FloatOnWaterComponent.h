#pragma once

#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../scene/SerializedFieldRegistry.h"

namespace KujataEngine {

/// <summary>
/// 付けた物を海面(OceanComponent)に浮かべる。毎フレーム、その位置の海面の高さへ Y を合わせ、波の傾きに合わせて少し傾ける。
/// 海の外に出たら何もしない(その場に留まる)。Play 中だけ動く。
///
/// 高さは OceanComponent::TryGetSurfaceHeight(描かれている海面と同じ高さ)。
/// 親がある場合は、親が拡大・回転していないこと(Y の差をそのまま自分の位置に足すため)。
/// </summary>
class KUJATA_API FloatOnWaterComponent : public Component {
public:
	const char* GetTypeName() const override { return "FloatOnWaterComponent"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>今フレーム海の上にいたか(海の外なら false)。</summary>
	bool IsOnWater() const { return onWater_; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(heightOffset_, "Height Offset", 0.01f, -100.0f, 100.0f,
		    "海面からどれだけ上に置くか[m]。負にすると沈む(船の喫水など)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(followSpeed_, "Follow Speed", 0.1f, 0.0f, 100.0f,
		    "海面へ近づく速さ。0でぴったり張り付く。小さいほどゆったり遅れて揺れる(重い物)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(tilt_, "Tilt", 0.01f, 0.0f, 1.0f, "波の傾きにどれだけ合わせて傾くか。0=傾かない、1=海面と同じ傾き。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(tiltSampleDistance_, "Tilt Sample Distance", 0.01f, 0.05f, 100.0f,
		    "傾きを調べる幅[m](物の大きさの半分くらい)。大きいほど細かい波で揺れにくい。");
	}

	KUJATA_FIELD_FLOAT(heightOffset_, 0.0f);
	KUJATA_FIELD_FLOAT(followSpeed_, 0.0f);
	KUJATA_FIELD_FLOAT(tilt_, 0.5f);
	KUJATA_FIELD_FLOAT(tiltSampleDistance_, 0.5f);

	// --- 実行時 ---
	// Play を始めたときの回転(傾きはこれに足す)。
	Vector3 baseRotation_ = {};
	bool onWater_ = false;
};

} // namespace KujataEngine
