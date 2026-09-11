#pragma once
#include <KujataEngine.h>
#include <string>

/// <summary>
/// 音量とカメラの設定画面。値は [[GameSettings]] に持たせるのでシーンを跨いで残り、
/// **閉じたときにファイルへ書き出すのでアプリを終了しても残る**
/// (保存に失敗しても既定値で起動できる)。
///
/// 行の選択は既存のCanvasナビゲーション(上下)に任せ、こちらは
/// **選択中の行に対する左右入力**だけを見る。こうするとボタンを1つ足すだけで項目を増やせる。
///
/// タイトルからもポーズからも同じ画面を使えるよう、閉じ方は
/// `On Close` に積んだ呼び出し(Buttonのイベント)に委ねている。
/// </summary>
class SettingsMenu : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "SettingsMenu"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

	/// <summary>設定画面を開く(呼び出し元のメニューがあれば伏せる)。</summary>
	void Open();
	/// <summary>設定画面を閉じて、呼び出し元のメニューを出し直す。</summary>
	void Close();
	bool IsOpen() const { return open_; }

	/// <summary>シーン内のSettingsMenuを探す(無ければnullptr)。</summary>
	static SettingsMenu* FindInScene(KujataEngine::Scene* scene);

private:
	/// <summary>1行ぶんの結び付け。</summary>
	enum class Row {
		BgmVolume,
		SeVolume,
		Sensitivity,
		InvertY,
		Count,
	};

	/// <summary>今フォーカスされている行(該当なしならCount)。</summary>
	Row FindFocusedRow() const;
	/// <summary>行の値を増減する(InvertYは符号だけ見てトグルする)。</summary>
	void Adjust(Row row, float direction);
	/// <summary>4行ぶんの表示テキストを現在値で書き直す。</summary>
	void RefreshTexts();
	void SetText(const std::string& objectName, const std::string& text);
	/// <summary>カメラ設定をエンジン側(OrbitCameraComponentのstatic)へ押し込む。</summary>
	void ApplyToEngine();
	/// <summary>音量を「0〜100%」の見た目にする。数値の生値より読みやすい。</summary>
	static std::string FormatPercent(float value01);

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(menuObjectName_, "Menu Object",
		    "設定画面のCanvasのGameObject名。**このコンポーネント自体は常にアクティブな\n"
		    "オブジェクトへ付けること**(伏せた画面の上に置くと、開くための呼び出しが届かない)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(returnMenuName_, "Return Menu",
		    "閉じたときに出し直すメニューのGameObject名(ポーズメニュー等)。空なら何もしない。\n"
		    "開くときは逆に伏せる。**出しっぱなしだと裏のボタンが選択表示のまま残る。**");
		KUJATA_REGISTER_STRING_NAMED_TIP(bgmRowName_, "Bgm Row", "BGM音量の行のButtonのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(seRowName_, "Se Row", "効果音量の行のButtonのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(sensitivityRowName_, "Sensitivity Row", "カメラ感度の行のButtonのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(invertRowName_, "Invert Row", "上下反転の行のButtonのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(bgmValueName_, "Bgm Value", "BGM音量の数値を出すTextのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(seValueName_, "Se Value", "効果音量の数値を出すTextのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(sensitivityValueName_, "Sensitivity Value", "カメラ感度の数値を出すTextのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(invertValueName_, "Invert Value", "上下反転のON/OFFを出すTextのGameObject名。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(volumeStep_, "Volume Step", 0.01f, 0.01f, 0.5f, "左右1回あたりの音量の変化量。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sensitivityStep_, "Sensitivity Step", 0.01f, 0.01f, 0.5f, "左右1回あたりの感度の変化量。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(repeatDelay_, "Repeat Delay", 0.01f, 0.05f, 1.0f,
		    "押しっぱなしで連続変化が始まるまでの秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(repeatInterval_, "Repeat Interval", 0.01f, 0.02f, 1.0f,
		    "連続変化の間隔[秒]。**小さすぎると一気に端まで飛ぶ**。");
	}

	KUJATA_FIELD_STRING(menuObjectName_, "SettingsMenu");
	KUJATA_FIELD_STRING(returnMenuName_, "");
	KUJATA_FIELD_STRING(bgmRowName_, "SettingsBgmRow");
	KUJATA_FIELD_STRING(seRowName_, "SettingsSeRow");
	KUJATA_FIELD_STRING(sensitivityRowName_, "SettingsSensRow");
	KUJATA_FIELD_STRING(invertRowName_, "SettingsInvertRow");
	KUJATA_FIELD_STRING(bgmValueName_, "SettingsBgmValue");
	KUJATA_FIELD_STRING(seValueName_, "SettingsSeValue");
	KUJATA_FIELD_STRING(sensitivityValueName_, "SettingsSensValue");
	KUJATA_FIELD_STRING(invertValueName_, "SettingsInvertValue");
	KUJATA_FIELD_FLOAT(volumeStep_, 0.05f);
	KUJATA_FIELD_FLOAT(sensitivityStep_, 0.1f);
	KUJATA_FIELD_FLOAT(repeatDelay_, 0.4f);
	KUJATA_FIELD_FLOAT(repeatInterval_, 0.12f);

	KujataEngine::GameObject* SetObjectActive(const std::string& name, bool active);

	// --- 実行時状態(シリアライズしない) ---
	bool open_ = false;
	// 左右の押しっぱなし判定。0なら離している。
	float heldDirection_ = 0.0f;
	float heldSeconds_ = 0.0f;
	float repeatTimer_ = 0.0f;
};
