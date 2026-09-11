#pragma once
#include <KujataEngine.h>
#include <string>

namespace KujataEngine {
class ImageComponent;
class RectTransformComponent;
class TextComponent;
}

/// <summary>
/// ローディング画面の進行役。LoadingSceneに1つ置く。
///
/// **なぜ「疑似」非同期なのか**: `ChangeScene` は同期ブロッキング(シーンを丸ごと破棄→再構築)で、
/// 途中経過を取れないうえ、その間フレームが完全に止まる
/// (計測値: BossArenaSceneで Debug 約3.9秒 / Release 約1.1秒)。そこで
///   1. ローディング画面を出す
///   2. 最低表示秒数のあいだ、それらしい進捗(`1 - exp(-t/tau)`)を90%まで進める
///   3. 進捗を100%にして少し見せる
///   4. **ローディング画面を映したまま**シーンを切り替える
/// という順にする。
///
/// **暗転してから読み込んではいけない。** 止まっている数秒が「真っ黒な画面」になり、
/// フリーズと区別が付かなくなる(実際それで black screen 報告が出た)。
/// 止まるのは避けられないので、**止まっている絵をローディング画面にする**のが要点。
/// 遷移先は自前のScreenFaderで黒から明けるので、繋ぎ目は黒1フレームで済む。
///
/// **既定では割合(%)を名乗らない**(Indeterminate)。実際の進捗は取れないので、
/// 数字を出すと「100%のまま数秒固まる」という一番まずい見え方になる。
/// 代わりにバーを繰り返し流し、「読み込み中」の文字と回るマークで作業中だと伝える。
/// 本物の進捗が取れるようになったらOFFにして、そのときだけ割合として見せること。
///
/// 行き先は [GameSession] が持つ。直接Playされた場合は Fallback Scene へ行く。
/// </summary>
class LoadingScreen : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "LoadingScreen"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>疑似進捗[0,1]。</summary>
	float GetProgress() const { return progress_; }

private:
	enum class State {
		FadeIn,   // 黒から明ける
		Hold,     // 最低表示時間を稼ぎつつ進捗を90%まで進める
		Complete, // 進捗100%を見せる(この絵のまま次のシーンを読む)
		Done,     // ChangeScene発行済み
	};

	void UpdateVisuals();

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(minDisplaySeconds_, "Min Display Seconds", 0.05f, 0.0f, 30.0f,
		    "ローディング画面を最低限見せる秒数。**短すぎると一瞬だけ映って消え、かえって不自然**になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(fadeInSeconds_, "Fade In Seconds", 0.05f, 0.0f, 10.0f, "黒から明けるまでの秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(completeHoldSeconds_, "Complete Hold Seconds", 0.05f, 0.0f, 5.0f,
		    "進捗を100%にしてから読み込みを始めるまでの秒数(バーが満ちるのを見せるための間)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(progressTau_, "Progress Tau", 0.05f, 0.05f, 10.0f,
		    "疑似進捗の時定数[s]。小さいほど最初に一気に伸びる。`1 - exp(-t/tau)` で90%まで進む。");
		KUJATA_REGISTER_STRING_NAMED_TIP(fallbackScene_, "Fallback Scene",
		    "行き先が未設定のとき(LoadingSceneを直接Playしたとき)に読み込むシーン。");
		KUJATA_REGISTER_STRING_NAMED_TIP(progressFillName_, "Progress Fill",
		    "進捗バーの中身のGameObject名(Imageのfill Amountを進捗で更新する)。空なら何もしない。");
		KUJATA_REGISTER_STRING_NAMED_TIP(spinnerName_, "Spinner",
		    "回すマークのGameObject名(RectTransformのRotation Zを回す)。空なら何もしない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(spinnerSpeed_, "Spinner Speed", 0.05f, -20.0f, 20.0f,
		    "回すマークの角速度[rad/s]。正で時計回り。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(indeterminate_, "Indeterminate",
		    "**割合を名乗らない。** 実進捗が取れないので、バーは繰り返し流れるだけの「作業中」表示になる。\n"
		    "OFFにすると従来どおり疑似進捗を割合として見せるが、\n"
		    "**読み込みの数秒がまるごと100%表示になる**ので、本物の進捗が取れるまではONのままにすること。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sweepSeconds_, "Sweep Seconds", 0.05f, 0.2f, 5.0f,
		    "Indeterminate のとき、バーが一往復するのにかける秒数。");
		KUJATA_REGISTER_STRING_NAMED_TIP(statusTextName_, "Status Text",
		    "「読み込み中」を出すTextのGameObject名。空なら文字は出さない。");
		KUJATA_REGISTER_STRING_NAMED_TIP(statusLabel_, "Status Label", "状態の文言。末尾に点が増減する。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dotIntervalSeconds_, "Dot Interval", 0.01f, 0.05f, 2.0f,
		    "末尾の点が1つ増えるまでの秒数。**動いていることが分かる最後の砦**なので0にしないこと。");
	}

	KUJATA_FIELD_FLOAT(minDisplaySeconds_, 2.0f);
	KUJATA_FIELD_FLOAT(fadeInSeconds_, 0.4f);
	KUJATA_FIELD_FLOAT(completeHoldSeconds_, 0.35f);
	KUJATA_FIELD_FLOAT(progressTau_, 0.7f);
	KUJATA_FIELD_STRING(fallbackScene_, "TitleScene");
	KUJATA_FIELD_STRING(progressFillName_, "LoadingBarFill");
	KUJATA_FIELD_STRING(spinnerName_, "LoadingRing");
	KUJATA_FIELD_FLOAT(spinnerSpeed_, 2.0f);
	KUJATA_FIELD_BOOL(indeterminate_, true);
	KUJATA_FIELD_FLOAT(sweepSeconds_, 1.2f);
	KUJATA_FIELD_STRING(statusTextName_, "LoadingStatusText");
	KUJATA_FIELD_STRING(statusLabel_, "読み込み中");
	KUJATA_FIELD_FLOAT(dotIntervalSeconds_, 0.45f);

	// --- 実行時状態 ---
	State state_ = State::FadeIn;
	std::string targetScene_;
	float elapsed_ = 0.0f;
	float progress_ = 0.0f;
	float spinnerAngle_ = 0.0f;
	// 画面に出てからの通算[s]。バーの流れと点の増減は、状態が変わっても途切れさせない。
	float totalElapsed_ = 0.0f;
	// 直前に書いた点の数(毎フレーム同じ文字列を流し込まないため)。
	int lastDotCount_ = -1;
	class ScreenFader* fader_ = nullptr;
	KujataEngine::ImageComponent* progressFill_ = nullptr;
	KujataEngine::RectTransformComponent* spinner_ = nullptr;
	class KujataEngine::TextComponent* statusText_ = nullptr;
};
