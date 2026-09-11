#pragma once
#include <KujataEngine.h>

namespace KujataEngine {
class ModelRendererComponent;
}

/// <summary>
/// エルデンリングの「霧の扉」。同じGameObjectのModelRenderer(Plane推奨)へ
/// パーリンノイズ(fBm)で作った霧のテクスチャを差し込み、UVを流して立ち昇らせる。
///
/// **加算合成のマテリアルとセットで使う**(Blend Mode = 2 / Depth Write = off / Shader Model = 0)。
/// Object3dのPixelShaderは `textureColor.a <= 0.5` でdiscardするアルファテストを持っているため、
/// **α勾配の霧テクスチャをモデルへ貼ると、霧ではなく穴だらけの型抜きになる。**
/// そこで濃さをRGBに入れ(NoiseTexture::GenerateFogSheetTexture)、「黒=見えない」として加算する。
///
/// **1枚だけだと平坦に見える。** 粗い層を遅く、細かい層を速く、の2枚を少しずらして重ねると
/// 奥行きが出る(FogGate.prefabはこの構成)。
///
/// 発光はエミッションマップに同じ霧テクスチャを入れて出す。こうしないと面全体が一様に光り、
/// せっかくの模様が白飛びで潰れる。
///
/// 生成はUpdateの初回に行う(テクスチャ生成はコマンドリスト実行を伴うため描画パス外で行う必要がある)。
/// </summary>
class FogGate : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "FogGate"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>今の設定でテクスチャを作り直す(Inspectorから種や細かさを変えたとき用)。</summary>
	void Regenerate();
	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_INT_NAMED_TIP(textureSize_, "Texture Size", 1.0f, 32, 1024,
		    "生成するノイズテクスチャの一辺[px]。扉1枚なら256で十分。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(seed_, "Seed", 1.0f, -2000.0f, 2000.0f,
		    "ノイズを読み出す位置。2枚重ねるときは**必ず別の値**にする(同じだと同じ模様が動くだけになる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(frequency_, "Frequency", 0.1f, 0.5f, 32.0f,
		    "模様の細かさ。小さいほど大きなうねり、大きいほど細かい筋。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(contrast_, "Contrast", 0.05f, 0.5f, 6.0f,
		    "濃淡の締まり。大きいほど濃い所と薄い所の差が出て、霧が「渦」に見える。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(uvScale_, "UV Scale", 0.05f, 0.1f, 8.0f,
		    "扉1枚に何回敷き詰めるか。大きいほど模様が細かく並ぶ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(scrollX_, "Scroll X", 0.005f, -2.0f, 2.0f, "横方向の流れる速さ[UV/秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(scrollY_, "Scroll Y", 0.005f, -2.0f, 2.0f,
		    "縦方向の流れる速さ[UV/秒]。**負で上向きに立ち昇る**(UVのvは下向きが正)。");
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(glowColor_, "Glow Color", 0.01f, 0.0f, 1.0f,
		    "発光の色。エルデンリングの霧は淡い金色(1, 0.85, 0.55あたり)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(glowIntensity_, "Glow Intensity", 0.05f, 0.0f, 10.0f,
		    "発光の強さ。**1を超えるとHDR輝度になりブルームが乗る**(扉が滲んで見える)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(pulseAmplitude_, "Pulse Amount", 0.01f, 0.0f, 1.0f,
		    "明るさのゆらぎ幅(Glow Intensityに対する割合)。0で一定。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(pulseSpeed_, "Pulse Speed", 0.05f, 0.0f, 10.0f,
		    "ゆらぎの速さ。**遅いほど「呼吸」に見える**(0.5〜1.5あたり)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(lane_, "Lane", 1.0f, -1000.0f, 1000.0f,
		    "ゆらぎのノイズのレーン。重ねる層ごとに別の値にする(揃うと2枚が同時に明滅する)。");
	}

	KUJATA_FIELD_INT(textureSize_, 256);
	KUJATA_FIELD_FLOAT(seed_, 900.0f);
	KUJATA_FIELD_FLOAT(frequency_, 3.0f);
	KUJATA_FIELD_FLOAT(contrast_, 1.6f);
	KUJATA_FIELD_FLOAT(uvScale_, 1.5f);
	KUJATA_FIELD_FLOAT(scrollX_, 0.03f);
	KUJATA_FIELD_FLOAT(scrollY_, -0.09f);
	KUJATA_FIELD_VECTOR3(glowColor_, (KujataEngine::Vector3{1.0f, 0.85f, 0.55f}));
	KUJATA_FIELD_FLOAT(glowIntensity_, 1.5f);
	KUJATA_FIELD_FLOAT(pulseAmplitude_, 0.3f);
	KUJATA_FIELD_FLOAT(pulseSpeed_, 0.8f);
	KUJATA_FIELD_FLOAT(lane_, 0.0f);

	// --- 実行時状態(シリアライズしない。Playごとに戻す) ---
	KujataEngine::ModelRendererComponent* renderer_ = nullptr;
	bool generated_ = false;
	float elapsed_ = 0.0f;
	float offsetX_ = 0.0f;
	float offsetY_ = 0.0f;
};
