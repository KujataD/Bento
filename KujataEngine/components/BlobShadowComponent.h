#pragma once

#include "../3d/Model.h"
#include "../3d/WorldTransform.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../scene/SerializedFieldRegistry.h"
#include <memory>
#include <vector>

namespace KujataEngine {

class Camera;

/// <summary>
/// 丸影(ブロブシャドウ)。付けた物の真下の地面・海面に、丸い影を落とす(ローポリ・ドット絵向け。風のタクト / A Short Hike の影)。
/// ジャンプ中や空中の敵でも「どこの真上にいるか」が分かるようにするための影。
///
/// 仕組み:
///   1. 真下へ線を伸ばして、いちばん近い地面を探す(Collider を持つ物は SphereCast、海は OceanComponent::TryGetSurfaceHeight)
///   2. 影の形は小さな格子(Resolution × Resolution マス)。格子の点ごとに真下の地面を調べて置くので、波・斜面・段差に沿って曲がる
///      中心の地面より大きく下がる点(崖・箱の縁の外)は描かない(影が縁で切れる)
///   3. 描くのは半透明(深度を書かない)。円の外は捨て、縁は段で薄くする(EngineData/shader/Custom/BlobShadow.hlsl)
///   4. 地面から離れるほど、小さく・薄くなる(Max Distance で消える)
///
/// 影を受けるのは **Collider を持つ物(トリガーは除く)と海**だけ。見た目だけのモデルには落ちない。
/// </summary>
class KUJATA_API BlobShadowComponent : public Component {
public:
	const char* GetTypeName() const override { return "BlobShadowComponent"; }
	bool AllowMultiple() const override { return false; }

	void Draw() override;

	bool IsTransparentDraw() const override { return true; }
	// 海(-100)の後、ほかの半透明(しぶき等)より先に描く。
	int GetTransparentQueue() const override { return -50; }

	/// <summary>描画に使うカメラ。Sceneが各ビューの描画前に配る(ModelRendererComponentと同じ扱い)。</summary>
	void SetCamera(const Camera* camera) { camera_ = camera; }

	/// <summary>今フレーム、影を落とす地面が見つかったか。</summary>
	bool HasGround() const { return hasGround_; }
	/// <summary>影を落とした地面の高さ(ワールドの Y)。HasGround が false なら意味がない。</summary>
	float GetGroundHeight() const { return groundHeight_; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(size_, "Size", 0.01f, 0.01f, 100.0f, "地面に接しているときの影の直径[m]。付けた物の横幅くらいにする(小さいと体に隠れて見えない)。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(color_, "Color", 0.01f, 0.0f, 1.0f, "影の色(A は使わない。濃さは Opacity)。暗い海・地面の上でも見えるよう、既定は黒。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(opacity_, "Opacity", 0.01f, 0.0f, 1.0f, "地面に接しているときの影の濃さ。");
		KUJATA_REGISTER_INT_NAMED_TIP(steps_, "Steps", 1.0f, 1, 8, "縁の段の数。1=くっきりした円、2=濃い芯+薄い縁、多いほど縁がなだらか。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(maxDistance_, "Max Distance", 0.1f, 0.1f, 1000.0f, "この高さ[m]より地面から離れると影が消える。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sizeAtMax_, "Size At Max", 0.01f, 0.0f, 10.0f, "Max Distance の高さでの大きさの倍率(離れるほど小さくする)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(opacityAtMax_, "Opacity At Max", 0.01f, 0.0f, 1.0f, "Max Distance の高さでの濃さの倍率(0で消えていく)。高く跳んでも影で位置が分かるよう、既定は少し残す。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(startHeight_, "Start Height", 0.01f, -100.0f, 100.0f,
		    "地面を探し始める高さ(このオブジェクトの位置からの上下[m])。足元が原点の物は少し上(0.1など)にする。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(lift_, "Lift", 0.001f, 0.0f, 1.0f, "地面からどれだけ浮かせて描くか[m](地面とのちらつき防止)。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(receiveColliders_, "On Colliders", "Collider を持つ物(トリガーは除く)に影を落とす。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(receiveOcean_, "On Ocean", "海(OceanComponent)に影を落とす(波に沿って曲がる)。");
		KUJATA_REGISTER_INT_NAMED_TIP(resolution_, "Resolution", 1.0f, 1, 16,
		    "影の格子の細かさ(1辺のマスの数)。波・斜面・縁にきれいに沿わせたいほど多く(点ごとに地面を調べるので重くなる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(edgeDrop_, "Edge Drop", 0.01f, 0.0f, 100.0f,
		    "中心の地面よりこれ[m]以上低い場所には影を描かない(箱や崖の縁の外へ影がはみ出して垂れ下がらないように)。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(showDebug_, "Show Debug", "地面を探す線(黄=見つかった、赤=見つからない)を描く。");
	}

	KUJATA_FIELD_FLOAT(size_, 1.2f);
	Vector4 color_ = {0.0f, 0.0f, 0.0f, 1.0f};
	KUJATA_FIELD_FLOAT(opacity_, 0.6f);
	KUJATA_FIELD_INT(steps_, 1);
	KUJATA_FIELD_FLOAT(maxDistance_, 20.0f);
	KUJATA_FIELD_FLOAT(sizeAtMax_, 0.6f);
	KUJATA_FIELD_FLOAT(opacityAtMax_, 0.4f);
	KUJATA_FIELD_FLOAT(startHeight_, 0.1f);
	KUJATA_FIELD_FLOAT(lift_, 0.02f);
	KUJATA_FIELD_BOOL(receiveColliders_, true);
	KUJATA_FIELD_BOOL(receiveOcean_, true);
	KUJATA_FIELD_INT(resolution_, 6);
	KUJATA_FIELD_FLOAT(edgeDrop_, 0.5f);
	KUJATA_FIELD_BOOL(showDebug_, false);

	/// <summary>真下の地面を探し、影の頂点を組み立てる(フレームに1回)。</summary>
	void Build();
	/// <summary>(x, z) の真下の地面の高さ。Collider と海面の高い方(地面を探し始める高さより下のものだけ)。</summary>
	bool GroundHeightAt(float x, float z, float& outHeight) const;

	// --- 実行時 ---
	const Camera* camera_ = nullptr;
	std::unique_ptr<Model> model_;
	std::vector<VertexData> vertices_;
	uint64_t builtFrame_ = ~0ull;
	bool hasGround_ = false;
	float groundHeight_ = 0.0f;
	float strength_ = 0.0f; // 高さによる薄まりを掛けた濃さ
	// 地面を探す線(デバッグ表示用)。
	Vector3 rayStart_ = {};
	Vector3 rayEnd_ = {};
	// 地面を探し始める高さ(ワールドの Y)。
	float startY_ = 0.0f;
	// 格子の点ごとの高さと、影を描くか(縁の外は描かない)。
	std::vector<float> pointHeights_;
	std::vector<bool> pointValid_;
	// 頂点はワールド座標で作るので、描画は常に単位行列で行う。
	WorldTransform identityTransform_;
	bool identityReady_ = false;
};

} // namespace KujataEngine
