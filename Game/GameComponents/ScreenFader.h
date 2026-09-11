#pragma once
#include <KujataEngine.h>
#include <string>

namespace KujataEngine {
class ImageComponent;
}

/// <summary>
/// 画面全体の黒フェードと、「暗転しきってからシーンを切り替える」遷移の予約役。
///
/// **覆いのCanvas/Imageは自分で実行時に作る**(`__ScreenFadeCanvas` / `__ScreenFadeImage`)。
/// こうしておくとシーンごとに全画面Imageを手で配置しなくてよく、Sort Orderも必ず最前面になる。
/// 生成はUpdateで行う(Scene::OnPlayStartはgameObjects_を走査中なので、そこで足すと危ない)。
///
/// 時間は必ず**Unscaled**で数える。ポーズ(timeScale=0)中にもフェードが進む必要があるため。
/// </summary>
class ScreenFader : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "ScreenFader"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>暗転させる(alpha→1)。secondsが0以下なら即時。</summary>
	void FadeOut(float seconds);
	/// <summary>明転させる(alpha→0)。secondsが0以下なら即時。</summary>
	void FadeIn(float seconds);
	/// <summary>補間せず即座にalphaを設定する。</summary>
	void SetAlphaImmediate(float alpha);

	/// <summary>
	/// 暗転してから指定シーンへ切り替える。**シーン切り替えはこれを通すこと**
	/// (ChangeSceneを直接呼ぶと、明るい画のまま一瞬固まってから切り替わる)。
	/// </summary>
	/// <param name="viaLoading">trueならローディング画面を挟む(重いシーン用)。</param>
	/// <param name="fadeSeconds">0以下ならTransition Fade Secondsを使う。</param>
	void TransitionTo(const std::string& sceneName, bool viaLoading, float fadeSeconds = -1.0f);

	/// <summary>遷移待ち中か(暗転しきるのを待っている)。</summary>
	bool IsTransitioning() const { return hasPendingTransition_; }

	float GetAlpha() const { return alpha_; }
	/// <summary>完全に暗転しきったか。ここでシーン切り替えを撃つとロードのカクつきが見えない。</summary>
	bool IsOpaque() const { return alpha_ >= 0.999f; }
	/// <summary>完全に明転しきったか。</summary>
	bool IsClear() const { return alpha_ <= 0.001f; }
	/// <summary>まだ目標alphaへ向かって動いている最中か。</summary>
	bool IsFading() const { return alpha_ != targetAlpha_; }

	/// <summary>シーン内のScreenFaderを探す(無ければnullptr)。</summary>
	static ScreenFader* FindInScene(KujataEngine::Scene* scene);

	/// <summary>
	/// シーンのScreenFaderに遷移を頼む。Faderが置かれていないシーンでも動くよう、
	/// 見つからなければ暗転せずそのまま切り替える。
	/// </summary>
	static void RequestTransition(KujataEngine::Scene* scene, const std::string& sceneName, bool viaLoading, float fadeSeconds = -1.0f);

private:
	/// <summary>覆いのCanvasとImageを生成する。Updateからのみ呼ぶこと。</summary>
	void CreateOverlay();
	void ApplyAlpha();

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_BOOL_NAMED_TIP(fadeInOnStart_, "Fade In On Start",
		    "シーン開始時に黒からフェードインするか。ローディング経由で来るシーンは必ずONにする\n"
		    "(前のシーンが暗転しきった状態で切り替わるため、OFFだと一瞬明るい画が挟まる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(fadeInSeconds_, "Fade In Seconds", 0.05f, 0.0f, 10.0f,
		    "開始時フェードインにかける秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(transitionFadeSeconds_, "Transition Fade Seconds", 0.05f, 0.0f, 10.0f,
		    "シーン切り替え時に暗転させる秒数(呼び出し側が指定しなかった場合)。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(fadeColor_, "Fade Color", 0.01f, 0.0f, 1.0f,
		    "覆いの色。αは無視され、フェード量で上書きされる。");
		KUJATA_REGISTER_INT_NAMED_TIP(sortOrder_, "Sort Order", 1.0f, -1000, 10000,
		    "覆いCanvasの描画順。**他のどのCanvasよりも大きく**すること(既定1000)。");
	}

	KUJATA_FIELD_BOOL(fadeInOnStart_, true);
	KUJATA_FIELD_FLOAT(fadeInSeconds_, 0.6f);
	KUJATA_FIELD_FLOAT(transitionFadeSeconds_, 0.5f);
	KUJATA_FIELD_VECTOR4(fadeColor_, (KujataEngine::Vector4{0.0f, 0.0f, 0.0f, 1.0f}));
	KUJATA_FIELD_INT(sortOrder_, 1000);

	// --- 実行時状態(シリアライズしない。Playごとに必ず作り直す) ---
	KujataEngine::ImageComponent* fadeImage_ = nullptr;
	// 覆いのCanvasのGameObject。alphaが0のときは丸ごと非アクティブにする
	// (完全に透明な全画面Imageを毎フレーム描く意味が無いため。描画負荷の面でも無駄)。
	KujataEngine::GameObject* fadeCanvasObject_ = nullptr;
	// **既定は透明にしておく**。OnPlayStartが走らなかった場合に不透明のままだと
	// 画面が黒いまま復帰できない。フェードインの起点(alpha=1)はOnPlayStartで作る。
	float alpha_ = 0.0f;
	float targetAlpha_ = 0.0f;
	// 1秒あたりのalpha変化量。0以下なら即時反映。
	float fadeSpeed_ = 0.0f;
	// 暗転しきったら切り替える予約。
	bool hasPendingTransition_ = false;
	bool pendingViaLoading_ = false;
	// 暗転しきったまま止まっている秒数。黒画面へ取り残されないための保険に使う。
	float pendingElapsed_ = 0.0f;
	std::string pendingScene_;
};
