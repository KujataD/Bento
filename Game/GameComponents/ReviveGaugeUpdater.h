#pragma once

#include <KujataEngine.h>
#include <string>

namespace KujataEngine {
class ImageComponent;
}
class PlayerHealth;

/// <summary>
/// **倒れている間、頭の上に「あと何秒で起きるか」を出すゲージ。** キャラのGameObjectに付ける。
///
/// 蘇生は倒れてからの経過時間だけで進み、`Revive Seconds` に達すると自力で復帰する([[death-and-revive]])。
/// 残り時間が見えないと、待たされている側は**放置されただけなのか復帰できるのか分からない**まま立ち尽くす。
/// 出すのは倒れている間だけで、生きている間は完全に隠す。
///
/// 器は World Space Canvas の Prefab をランタイムに1つ生成して使い回す
/// (レティクル・致命のプロンプト・衝撃刃と同じ流儀。キャラ側のJSONを触らずに済む)。
/// World Space Canvas は自分ではカメラを向かないので、Yawだけコードで向ける。
/// </summary>
class ReviveGaugeUpdater : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "ReviveGaugeUpdater"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;

private:
	/// <summary>器をPrefabから遅延生成する(失敗したらnullptr)。</summary>
	KujataEngine::GameObject* AcquireGauge();
	/// <summary>器を隠す。</summary>
	void HideGauge();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(gaugePrefabPath_, "Gauge Prefab",
		    "蘇生ゲージのPrefab(World Space Canvas)。空なら表示しない。");
		KUJATA_REGISTER_STRING_NAMED_TIP(fillObjectName_, "Fill Object", "伸びる帯の子オブジェクト名(Imageを持つもの)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(height_, "Height", 0.05f, 0.0f, 10.0f,
		    "倒れたキャラの位置からどれだけ上に出すか。**倒れているので低めでよい。**");
		KUJATA_REGISTER_STRING_NAMED_TIP(cameraName_, "Camera Name", "向きを合わせるカメラのGameObject名。");
	}

	// ゲージのPrefab。
	KUJATA_FIELD_STRING(gaugePrefabPath_, "Prefabs/ReviveGauge.prefab.json");
	// 帯の子オブジェクト名。
	KUJATA_FIELD_STRING(fillObjectName_, "ReviveGaugeFill");
	// 表示する高さ。
	KUJATA_FIELD_FLOAT(height_, 1.6f);
	// 向きを合わせるカメラ名。
	KUJATA_FIELD_STRING(cameraName_, "Main Camera");

	PlayerHealth* health_ = nullptr;
	// Prefabから生成した器(Playインスタンス内なので停止で消える)。
	KujataEngine::GameObject* gauge_ = nullptr;
	bool gaugeTried_ = false;
	KujataEngine::ImageComponent* fillImage_ = nullptr;
};
