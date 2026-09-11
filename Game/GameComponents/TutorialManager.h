#pragma once
#include <KujataEngine.h>
#include <string>
#include <vector>

/// <summary>
/// チュートリアルのポップアップと**課題**を出す係。チュートリアルシーンに1つ置く。
///
/// 流れは「説明を読む → 実際にやってみる → 次の説明」の繰り返し。
/// 説明を閉じると、そのトリガーが指定した課題(Objective)が始まり、
/// 達成するまで画面上部に目標が出たままになる。**達成した数が出口の鍵**で、
/// Required Objectives に届くまで [TutorialTrigger] の出口は開かない
/// (読み飛ばして走り抜けただけでは先へ進めないようにするため)。
///
/// 表示中は `Time::SetTimeScale(0)` でゲームを止める(読ませるため)。
/// 閉じるのはポップアップ上のButtonから `Close` を呼ぶ形にしてあり、
/// **入力の取り合いをしない**(Aボタンは回避と兼用なので、UIナビ側に任せるのが安全)。
/// ポップアップのCanvasはSort Orderを一番大きくしておくこと。
/// パッドのフォーカスは最前面のCanvasだけが受け取るので、それだけでモーダルになる。
///
/// 文面と課題は [TutorialTrigger] が持っていて、通過時にここへ渡される。
/// 課題の達成判定は [[GameEvents]] のカウンタとシーン内の敵の数だけを見る。
/// </summary>
class TutorialManager : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "TutorialManager"; }
	bool AllowMultiple() const override { return false; }

	/// <summary>課題の種類。TutorialTriggerのObjective文字列と対応する。</summary>
	enum class Objective {
		None = 0,
		DefeatEnemies, // 出ている敵を全部倒す
		JustGuard,     // ジャストガードを成立させる
		Critical,      // 致命の一撃を決める
		SwapCharacter, // 操作キャラを入れ替える
		LockOn,        // Z注目で対象を掴む
	};

	/// <summary>Objective名(`DefeatEnemies` など)を enum へ。未知なら None。</summary>
	static Objective ParseObjective(const std::string& name);

	void OnPlayStart() override;
	void Update() override;

	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

	/// <summary>
	/// ポップアップを開く(ゲームは止まる)。既に開いていれば何もしない。
	/// 閉じたあとに objective が始まる(Noneなら課題なしで、その場で完了扱い)。
	/// </summary>
	void Show(const std::string& title, const std::vector<std::string>& lines, Objective objective = Objective::None,
	    int requiredCount = 1);

	/// <summary>ポップアップを閉じる(ゲームが動き出す)。Button.onClickから呼ぶ。</summary>
	void Close();

	bool IsShowing() const { return showing_; }

	/// <summary>
	/// 出口を開けてよいか。**達成した課題の数**が Required Objectives 以上かで決める。
	/// Required Objectives が0なら常に開く(説明だけのコースにしたいとき用)。
	/// </summary>
	bool IsCourseComplete() const { return completedObjectives_ >= requiredObjectives_; }

	/// <summary>まだ課題が残っているのに出口へ来たことを知らせる(目標欄に一言出す)。</summary>
	void NotifyExitBlocked();

	/// <summary>シーン内のTutorialManagerを探す(無ければnullptr)。</summary>
	static TutorialManager* FindInScene(KujataEngine::Scene* scene);

private:
	/// <summary>名前で探したGameObjectのTextを差し替える(無ければ何もしない)。</summary>
	void SetText(const std::string& objectName, const std::string& text);
	/// <summary>名前で探したGameObjectの表示/非表示を切り替える。</summary>
	void SetObjectActive(const std::string& objectName, bool active);

	/// <summary>今の課題が達成されたか。</summary>
	bool IsObjectiveSatisfied() const;
	/// <summary>目標欄の文言を作る(残り回数入り)。</summary>
	std::string BuildObjectiveText() const;
	/// <summary>課題を始める(目標欄を出し、達成の基準値を控える)。</summary>
	void BeginObjective();
	/// <summary>課題を達成として畳む。</summary>
	void CompleteObjective();

	/// <summary>
	/// **操作キャラの近く**に残っている「狙える敵」の数。
	/// コース上の敵は最初から全部立っているので、範囲(Enemy Count Radius)で今の区間だけに絞る。
	/// </summary>
	int CountLivingEnemies() const;

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(popupName_, "Popup Root",
		    "ポップアップのCanvasのGameObject名。**HUDより大きいSort Order**にしておくこと。");
		KUJATA_REGISTER_STRING_NAMED_TIP(titleName_, "Title Object", "見出しのTextを持つGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(bodyPrefix_, "Body Prefix",
		    "本文のTextを持つGameObject名の接頭辞。`<接頭辞>0` から順に埋める。");
		KUJATA_REGISTER_INT_NAMED_TIP(bodyLineCount_, "Body Lines", 1.0f, 1, 8,
		    "本文の行数。足りない行は空文字で消す。**この数だけ Body Prefix のGameObjectを置くこと。**");
		KUJATA_REGISTER_STRING_NAMED_TIP(objectiveRootName_, "Objective Root",
		    "課題中に出しておく帯のGameObject名。空なら帯は使わず、文字だけ差し替える。");
		KUJATA_REGISTER_STRING_NAMED_TIP(objectiveTextName_, "Objective Text", "課題の文言を流し込むTextのGameObject名。");
		KUJATA_REGISTER_INT_NAMED_TIP(requiredObjectives_, "Required Objectives", 1.0f, 0, 16,
		    "**出口が開くまでに達成しないといけない課題の数。**\n"
		    "0にすると説明を読むだけで抜けられる。置いた課題つきトリガーの数に合わせること。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(clearBannerSeconds_, "Clear Banner Seconds", 0.05f, 0.0f, 6.0f,
		    "課題を達成したときに「達成」を出しておく秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(blockedBannerSeconds_, "Blocked Banner Seconds", 0.05f, 0.0f, 6.0f,
		    "課題を残したまま出口へ来たときに、引き返しを促す文を出しておく秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(enemyCountRadius_, "Enemy Count Radius", 0.5f, 5.0f, 200.0f,
		    "`DefeatEnemies` で数える敵の範囲[m]。**操作キャラからの水平距離**で見る。\n"
		    "敵はコースの各所に最初から立っているので、範囲を切らないと\n"
		    "「洞窟中の敵を全部倒す」という課題になってしまう。区間の間隔より小さくすること。");
	}

	KUJATA_FIELD_STRING(popupName_, "TutorialPopup");
	KUJATA_FIELD_STRING(titleName_, "PopupTitle");
	KUJATA_FIELD_STRING(bodyPrefix_, "PopupBody");
	KUJATA_FIELD_INT(bodyLineCount_, 6);
	KUJATA_FIELD_STRING(objectiveRootName_, "TutorialObjective");
	KUJATA_FIELD_STRING(objectiveTextName_, "ObjectiveText");
	KUJATA_FIELD_INT(requiredObjectives_, 1);
	KUJATA_FIELD_FLOAT(clearBannerSeconds_, 2.0f);
	KUJATA_FIELD_FLOAT(blockedBannerSeconds_, 3.0f);
	KUJATA_FIELD_FLOAT(enemyCountRadius_, 28.0f);

	// --- 実行時状態(シリアライズしない。OnPlayStartで必ず戻す) ---
	bool showing_ = false;
	// ポップアップを閉じたあとに始める課題(閉じるまで待たせる)。
	Objective pendingObjective_ = Objective::None;
	int pendingRequiredCount_ = 1;
	// 進行中の課題。
	Objective activeObjective_ = Objective::None;
	int objectiveRequiredCount_ = 1;
	// 課題を始めた時点のカウンタ値。**差分で数える**ので、前の課題ぶんが持ち越されない。
	int objectiveBaseCount_ = 0;
	// 達成した課題の数(出口の鍵)。
	int completedObjectives_ = 0;
	// 帯に一時的な文言(達成・引き返し)を出しておく残り時間[s]。0で通常の目標へ戻る。
	float bannerTimer_ = 0.0f;
	std::string bannerText_;
};
