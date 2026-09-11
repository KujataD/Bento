#pragma once
#include <KujataEngine.h>

namespace KujataEngine {
class ImageComponent;
}

/// <summary>
/// 同じGameObjectのImageへ、パーリンノイズ(fBm)で作った靄のテクスチャを差し込み、
/// UVをスクロールさせてゆっくり流す。タイトルなどの背景に使う。
///
/// テクスチャは**RGB白・αだけノイズ**なので、濃さも色も **Image の Color** で決める。
/// 配色を変えてもテクスチャは作り直さなくてよい。
///
/// **1枚だけだと平坦に見える。** 粗いノイズを遅く、細かいノイズを速く、の2層を重ねると
/// 奥行きが出る(TitleSceneはこの構成)。
///
/// 生成はUpdateの初回に行う(テクスチャ生成はコマンドリスト実行を伴うため描画パス外で行う必要があり、
/// かつOnPlayStartはScene側がgameObjects_を走査中で安全に触れないタイミングがあるため)。
/// </summary>
class MistBackdrop : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "MistBackdrop"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;

	/// <summary>今の設定でテクスチャを作り直す(Inspectorから種や細かさを変えたとき用)。</summary>
	void Regenerate();
	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_INT_NAMED_TIP(textureSize_, "Texture Size", 1.0f, 32, 1024,
		    "生成するノイズテクスチャの一辺[px]。背景は引き伸ばして使うので256で十分。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(seed_, "Seed", 1.0f, -1000.0f, 1000.0f,
		    "ノイズを読み出す位置。2層重ねるときは**必ず別の値**にする(同じだと同じ模様が動くだけになる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(frequency_, "Frequency", 0.1f, 0.5f, 32.0f,
		    "模様の細かさ。小さいほど大きなうねり、大きいほど細かい筋。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(contrast_, "Contrast", 0.05f, 0.5f, 6.0f,
		    "濃淡の締まり。大きいほど濃い所と薄い所の差が出る。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(uvScale_, "UV Scale", 0.05f, 0.1f, 8.0f,
		    "画面に何回敷き詰めるか。大きいほど模様が細かく並ぶ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(scrollX_, "Scroll X", 0.005f, -1.0f, 1.0f,
		    "横方向の流れる速さ[UV/秒]。**0.01前後が「ゆっくり流れる」の目安**。大きいと落ち着かない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(scrollY_, "Scroll Y", 0.005f, -1.0f, 1.0f, "縦方向の流れる速さ[UV/秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(pulseAmplitude_, "Pulse Amount", 0.01f, 0.0f, 1.0f,
		    "濃さのゆらぎ幅(ImageのColorのαに対する割合)。0で一定。\n"
		    "**流すだけだと「動いている」と気付かれにくい。** 濃さが呼吸すると一目で分かるようになる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(pulseSpeed_, "Pulse Speed", 0.05f, 0.0f, 10.0f,
		    "ゆらぎの速さ。**遅いほど「呼吸」に見える**(0.3〜1.0あたり)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(pulseLane_, "Pulse Lane", 1.0f, -1000.0f, 1000.0f,
		    "ゆらぎのノイズのレーン。層ごとに別の値にする(揃うと2枚が同時に濃くなる)。");
	}

	KUJATA_FIELD_INT(textureSize_, 256);
	KUJATA_FIELD_FLOAT(seed_, 200.0f);
	KUJATA_FIELD_FLOAT(frequency_, 3.0f);
	KUJATA_FIELD_FLOAT(contrast_, 1.6f);
	KUJATA_FIELD_FLOAT(uvScale_, 1.0f);
	KUJATA_FIELD_FLOAT(scrollX_, 0.012f);
	KUJATA_FIELD_FLOAT(scrollY_, 0.0f);
	KUJATA_FIELD_FLOAT(pulseAmplitude_, 0.0f);
	KUJATA_FIELD_FLOAT(pulseSpeed_, 0.5f);
	KUJATA_FIELD_FLOAT(pulseLane_, 0.0f);

	// --- 実行時状態 ---
	KujataEngine::ImageComponent* image_ = nullptr;
	bool generated_ = false;
	float elapsed_ = 0.0f;
	float offsetX_ = 0.0f;
	float offsetY_ = 0.0f;
	// ゆらぎの基準になる、シリアライズされている方のα。Play中に書き換えるので控えておく。
	float baseAlpha_ = 1.0f;
	bool baseAlphaCaptured_ = false;
};
