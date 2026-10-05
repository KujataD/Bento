#pragma once

#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../scene/ObjectRef.h"
#include "../scene/SerializedFieldRegistry.h"
#include "ParticleSystemComponent.h"
#include "SplineRendererComponent.h"
#include <random>
#include <string>
#include <vector>

namespace KujataEngine {

class ModelRendererComponent;
class WaterDropComponent;

/// <summary>
/// 放水(水鉄砲・ホース・ボスの水ブレス)。このオブジェクトの位置(ノズル)から水弾を撃ち出し、
/// 水弾どうしを曲線(SplineRendererComponent)でつないで水流に見せる。
///
/// **付けるだけで使える**: 水流を描く子オブジェクトも、水の見た目(engine:Custom/Water.hlsl)も自分で用意する。
/// マテリアルは要らない(色・泡・うねりはすべてこの Inspector の設定)。
///
/// 出し方(Fire Mode):
///   - Action … Fire Action のアクションを押している間(既定。割り当ては InputActions.json)
///   - Always … Play 中ずっと出す
///   - Code   … ゲームのコードから SetFiring(true/false) で決める
/// 狙い方(Aim Mode):
///   - Forward … このオブジェクトの前(Z+ を回転させた向き)
///   - Target  … Aim Target に入れたオブジェクトの方
///   - Mouse   … Game ビューのマウスの方(**リプレイには残らない**。確かめ用)
///   - Code    … ゲームのコードから SetAimDirection / SetAimPoint で決める
///
/// 当たり判定: 水弾1つ1つを半径 Hit Radius の球として、前の位置から今の位置まで動かして調べる(速くてもすり抜けない)。
/// 相手はトリガーでない Collider を付けた物なら何でもよい。当たった水弾はそこで消えて、しぶきを出す。
///
/// 水弾は実行中だけ GameObject として作られ(Hierarchy の「WaterDrops (名前)」の下)、Play を止めると消える。
/// このオブジェクトに ParticleSystemComponent があれば、しぶき(当たった所・水面・流れに沿った飛沫)を出す。
/// 海(OceanComponent)があれば、その場所の海面で水弾が消えてしぶきを上げる(無ければ Kill Height)。
/// </summary>
class KUJATA_API WaterSprayComponent : public Component {
public:
	enum class FireMode { Action = 0, Always = 1, Code = 2 };
	enum class AimMode { Forward = 0, Target = 1, Mouse = 2, Code = 3 };

	const char* GetTypeName() const override { return "WaterSprayComponent"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>ゲームのコードから出す/止める(Fire Mode が Code のときに使う)。</summary>
	void SetFiring(bool firing) { codeFiring_ = firing; }
	bool IsFiring() const { return firing_; }

	/// <summary>狙う向き(Aim Mode が Code のとき)。長さは見ない。</summary>
	void SetAimDirection(const Vector3& direction);
	/// <summary>狙う点(Aim Mode が Code のとき)。ノズルからその点へ向けて撃つ。</summary>
	void SetAimPoint(const Vector3& point);

	/// <summary>今フレームに水弾が当たった点(当たっていなければ false)。当てた所に何かを出すときに使う。</summary>
	bool TryGetLastHitPoint(Vector3& outPoint) const;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		// --- 出し方・狙い方 ---
		KUJATA_REGISTER_INT_NAMED_TIP(fireMode_, "Fire Mode", 1.0f, 0, 2, "0=Action(アクションを押している間) / 1=Always(ずっと) / 2=Code(コードから SetFiring)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(fireAction_, "Fire Action", "Fire Mode が Action のときに見るアクション名(割り当ては Data/ProjectSettings/InputActions.json)。");
		KUJATA_REGISTER_INT_NAMED_TIP(aimMode_, "Aim Mode", 1.0f, 0, 3,
		    "0=Forward(このオブジェクトの前) / 1=Target(Aim Target の方) / 2=Mouse(Game ビューのマウス。リプレイに残らない) / 3=Code。");
		KUJATA_REGISTER_OBJECT_REF_NAMED(aimTarget_, "Aim Target");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(aimSpreadDeg_, "Aim Spread", 0.1f, 0.0f, 90.0f, "狙いのばらつき[度]。0でまっすぐ。");
		// --- 水弾 ---
		KUJATA_REGISTER_FLOAT_NAMED_TIP(speed_, "Speed", 0.1f, 0.0f, 200.0f, "水弾の初速[m/s]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(gravity_, "Gravity", 0.1f, 0.0f, 200.0f, "下向きの加速度[m/s^2]。0で落ちない(まっすぐ飛ぶ)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(emitInterval_, "Emit Interval", 0.001f, 0.005f, 1.0f, "水弾を出す間隔[秒]。短いほど水流がなめらか(そのぶん重い)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(lifetime_, "Lifetime", 0.01f, 0.05f, 10.0f, "水弾が消えるまでの秒数(水流の長さを決める)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(killHeight_, "Kill Height", 0.1f, -1000.0f, 1000.0f,
		    "この高さより下へ落ちた水弾は消す。海(OceanComponent)の上では、代わりにその場所の海面で消えてしぶきを上げる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(hitRadius_, "Hit Radius", 0.01f, 0.0f, 10.0f,
		    "水弾の当たり判定の半径。見た目より少し大きくすると当てやすい。0で当たり判定なし(すり抜ける)。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(hitFlash_, "Hit Flash", "当たった物(ModelRenderer を持つもの)を点滅させる。");
		// --- 水流の形 ---
		KUJATA_REGISTER_INT_NAMED_TIP(streamCount_, "Stream Count", 1.0f, 1, 16,
		    "同時に飛んでいられる水流の本数(押し直すと次の1本を使う)。子に SplineRenderer が無ければ、この数だけ自分で作る。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(width_, "Width", 0.01f, 0.01f, 10.0f, "水流の太さ[m](根元)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(spreadGrowth_, "Spread Growth", 0.01f, 0.0f, 10.0f, "1秒あたりの太さの増え方(先へ行くほど水が広がる)。");
		KUJATA_REGISTER_INT_NAMED_TIP(sides_, "Sides", 1.0f, 3, 32, "水流の断面の角の数。少ないほどローポリ(6前後)。");
		// --- しぶき(ParticleSystemComponent があるとき) ---
		KUJATA_REGISTER_INT_NAMED_TIP(splashCount_, "Splash Count", 1.0f, 0, 100, "物・水面に当たった水弾1つから跳ねるしぶきの数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dropletRate_, "Droplet Rate", 0.5f, 0.0f, 500.0f, "水流1本から1秒あたりに飛び散るしぶきの数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dropletInherit_, "Droplet Inherit", 0.01f, 0.0f, 2.0f, "しぶきが水弾の勢いをどれだけ引き継ぐか(1で同じ速さ)。");
		// --- 見た目(engine:Custom/Water.hlsl へ渡す) ---
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(waterColor_, "Water Color", 0.01f, 0.0f, 1.0f, "水の色(A は不透明度)。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(foamColor_, "Foam Color", 0.01f, 0.0f, 1.0f, "流れる泡の筋の色。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(rimColor_, "Rim Color", 0.01f, 0.0f, 1.0f, "輪郭の縁取りの色。");
		KUJATA_REGISTER_INT_NAMED_TIP(toonSteps_, "Toon Steps", 1.0f, 2, 8, "陰の段の数(少ないほどアニメ調)。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(flatShading_, "Flat Shading", "面ごとに平らな陰にする(ローポリの角をはっきり見せる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(rimWidth_, "Rim Width", 0.01f, 0.0f, 1.0f, "縁取りの太さ(0で縁取りなし)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(wobbleAmount_, "Wobble", 0.01f, 0.0f, 5.0f, "太さのうねりの大きさ[m](0でまっすぐなホースになる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(wobblePerMeter_, "Wobble Per Meter", 0.01f, 0.0f, 10.0f, "うねりの間隔(1mあたりの数)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(wobbleSpeed_, "Wobble Speed", 0.1f, -100.0f, 100.0f, "うねりが流れる速さ[m/秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stripeLength_, "Stripe Length", 0.01f, 0.0f, 1.0f, "流れる泡の筋の長さ(0で筋なし)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stripesPerMeter_, "Stripes Per Meter", 0.01f, 0.0f, 10.0f, "泡の筋の間隔(1mあたりの数)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stripeSpeed_, "Stripe Speed", 0.1f, -100.0f, 100.0f, "泡の筋が流れる速さ[m/秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dissolveStart_, "Tip Dissolve", 0.01f, 0.0f, 1.0f,
		    "先端が面ごとに欠け始める位置(0.7 なら先端から3割。0で欠けさせない)。物に当たっている間は欠けない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dissolveCells_, "Tip Dissolve Cells", 0.05f, 0.1f, 20.0f, "欠けるブロックの細かさ(1mあたりの数)。");
	}

	KUJATA_FIELD_INT(fireMode_, 0);
	KUJATA_FIELD_STRING(fireAction_, "Attack");
	KUJATA_FIELD_INT(aimMode_, 0);
	KUJATA_FIELD_OBJECT_REF(aimTarget_);
	KUJATA_FIELD_FLOAT(aimSpreadDeg_, 0.0f);
	KUJATA_FIELD_FLOAT(speed_, 14.0f);
	KUJATA_FIELD_FLOAT(gravity_, 12.0f);
	KUJATA_FIELD_FLOAT(emitInterval_, 0.025f);
	KUJATA_FIELD_FLOAT(lifetime_, 1.6f);
	KUJATA_FIELD_FLOAT(killHeight_, -50.0f);
	KUJATA_FIELD_FLOAT(hitRadius_, 0.35f);
	KUJATA_FIELD_BOOL(hitFlash_, true);
	KUJATA_FIELD_INT(streamCount_, 4);
	KUJATA_FIELD_FLOAT(width_, 0.35f);
	KUJATA_FIELD_FLOAT(spreadGrowth_, 1.2f);
	KUJATA_FIELD_INT(sides_, 6);
	KUJATA_FIELD_INT(splashCount_, 3);
	KUJATA_FIELD_FLOAT(dropletRate_, 40.0f);
	KUJATA_FIELD_FLOAT(dropletInherit_, 0.6f);
	Vector4 waterColor_ = {0.35f, 0.75f, 0.95f, 1.0f};
	Vector4 foamColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
	Vector4 rimColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
	KUJATA_FIELD_INT(toonSteps_, 2);
	KUJATA_FIELD_BOOL(flatShading_, true);
	KUJATA_FIELD_FLOAT(rimWidth_, 0.18f);
	KUJATA_FIELD_FLOAT(wobbleAmount_, 0.2f);
	KUJATA_FIELD_FLOAT(wobblePerMeter_, 0.25f);
	KUJATA_FIELD_FLOAT(wobbleSpeed_, 3.0f);
	KUJATA_FIELD_FLOAT(stripeLength_, 0.0f);
	KUJATA_FIELD_FLOAT(stripesPerMeter_, 0.3f);
	KUJATA_FIELD_FLOAT(stripeSpeed_, 8.0f);
	KUJATA_FIELD_FLOAT(dissolveStart_, 0.72f);
	KUJATA_FIELD_FLOAT(dissolveCells_, 1.2f);

	// 水弾1つ(GameObject と、その状態を持つコンポーネント)。
	struct Drop {
		GameObject* object = nullptr;
		WaterDropComponent* data = nullptr;
	};

	// 1本の水流(出し始めてから止めるまで)。描くのは SplineRendererComponent。
	struct Stream {
		SplineRendererComponent* spline = nullptr;
		GameObject* dropFolder = nullptr; // 水弾を並べる入れ物(Hierarchy で見やすくするため)
		std::vector<Drop> drops;          // 古い順
		// 最後に当たった点と、そこを先端として使い続ける残り時間。当たり続けている間は先端がここに留まる
		// (当たった水弾をしまうたびに先端が1つ前の水弾まで飛び退いて、がくがくして見えるのを防ぐ)。
		Vector3 hitPoint = {0.0f, 0.0f, 0.0f};
		float hitHold = 0.0f;
	};

	/// <summary>水流を描く子(SplineRenderer)を集める。無ければ Stream Count の数だけ自分で作る。</summary>
	void EnsureStreams();
	/// <summary>水弾を並べる入れ物を用意する。</summary>
	void EnsureDropFolders();
	/// <summary>水流の見た目(色・泡・うねり)をシェーダーへ渡す。</summary>
	void ApplyLook(SplineRendererComponent& spline) const;
	/// <summary>水弾を1つ出す(しまってある水弾があれば使い回す)。</summary>
	void SpawnDrop(Stream& stream, const Vector3& position, const Vector3& velocity, float age);
	/// <summary>水弾をしまう(非アクティブにして、次に使い回す)。</summary>
	void ReleaseDrop(const Drop& drop);
	/// <summary>今フレームの狙う向き。求められなければ false。</summary>
	bool GetAimDirection(const Vector3& nozzle, Vector3& outDirection);
	/// <summary>マウスの指す点(ノズルと同じ奥行きの、カメラを向いた平面上)。</summary>
	bool GetMouseAimPoint(const Vector3& nozzle, Vector3& outPoint) const;
	/// <summary>今フレームに出すかどうか(Fire Mode で決まる)。</summary>
	bool ShouldFire() const;
	void UpdateStream(Stream& stream, float deltaTime, bool isActive, const Vector3& nozzle);
	/// <summary>当たった物を光らせる(Hit Flash)。</summary>
	void StartFlash(GameObject* target);
	/// <summary>光らせている物の時間を進め、終わったら元に戻す。</summary>
	void UpdateFlashes(float deltaTime);

	// --- 実行時 ---
	std::vector<Stream> streams_;
	GameObject* dropRoot_ = nullptr; // 「WaterDrops (ノズルの名前)」
	GameObject* streamRoot_ = nullptr;
	std::vector<Drop> freeDrops_; // しまってある水弾(非アクティブ)
	int dropSerial_ = 0;
	int activeStream_ = -1; // 今出している水流(出していなければ -1)
	int nextStream_ = 0;
	float emitTimer_ = 0.0f;
	bool firing_ = false;
	bool codeFiring_ = false;
	Vector3 codeAimDirection_ = {0.0f, 0.0f, 1.0f};
	bool hasLastHit_ = false;
	Vector3 lastHitPoint_ = {0.0f, 0.0f, 0.0f};
	ParticleSystemComponent* particles_ = nullptr;
	float dropletAccumulator_ = 0.0f;
	std::mt19937 random_{20260930u};
	// 光らせている物(Hit Flash)。当て続けても光りっぱなしにならないよう、「光る → 休む」をくり返す(点滅)。
	struct Flash {
		ModelRendererComponent* renderer = nullptr;
		float onTime = 0.0f;
		float cooldown = 0.0f;
	};
	std::vector<Flash> flashes_;
	// 一時的な配列(毎フレームの確保を避ける)
	std::vector<Vector3> points_;
	std::vector<float> widths_;
};

} // namespace KujataEngine
