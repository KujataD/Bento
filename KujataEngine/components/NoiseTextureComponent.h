#pragma once

#include "../runtime/KujataApi.h"
#include "../scene/Component.h"

namespace KujataEngine {

/// <summary>
/// FractalNoise(math/Noise.h)から岩肌/土埃のテクスチャを起動時にメモリ生成し、
/// 同じGameObjectのModelRendererComponentまたはParticleSystemComponentへ直接差し込む。
/// ファイルには保存しない一回限りの手続きテクスチャ(NoiseTexture.h参照)。
///
/// **プリミティブ専用ではない。** ModelRendererComponentが持つModelのテクスチャを丸ごと
/// 差し替えるので、.obj/.gltfで読み込んだモデルにもそのまま貼れる(サブメッシュ全部に同じ物が乗る)。
///
/// **貼り方はワールド座標のタイリング(トライプラナー)。** メッシュのUVは使わない。
/// プリミティブのUVは面ごとに0..1固定なので、Transformで引き伸ばした箱にUVで貼ると
/// **面の実寸に関係なく同じ枚数**が乗り、長い面ほど模様が伸びる(scale(3,10,54)の壁で5倍以上)。
/// ワールド座標で貼れば、どの面でもどのオブジェクトでも Tile Size ぶんの密度に揃う。
///
/// 岩肌/土は決め打ちの色で焼かれるため、そのままだと何に貼っても同じ灰色になる。
/// **Tintを使えば1種類の模様を色違いで使い回せる**(苔むした石・赤茶けた岩など)。
/// </summary>
class KUJATA_API NoiseTextureComponent : public Component {
public:
	enum class Kind {
		// ModelRendererComponentのBaseColorへ適用する岩肌テクスチャ。
		RockAlbedo = 0,
		// ParticleSystemComponentの粒テクスチャへ適用する土埃スプライト。
		DustSprite = 1,
		// ModelRendererComponentのBaseColorへ適用する土(地面)テクスチャ。
		SoilAlbedo = 2,
		// ParticleSystemComponentの粒テクスチャへ適用する炎スプライト(RGB白・αのみ)。
		FlameSprite = 3,
		// ModelRendererComponentへ適用する苔むした岩。窪みにだけ苔が乗る。
		MossyRock = 4,
		// ModelRendererComponentへ適用する砂地(砂丘のうねり+風紋)。
		Sand = 5,
		// ModelRendererComponentへ適用する錆びた金属(鉄+錆の塊と流れた筋)。
		RustedMetal = 6,
		// ModelRendererComponentへ適用するひび割れた岩(岩肌 + セル状の割れ目)。
		CrackedRock = 7,
	};

	/// <summary>kindがModelRendererComponent向け(=地形/物体の表面)か。</summary>
	static bool IsModelKind(int kind) {
		return kind == static_cast<int>(Kind::RockAlbedo) || kind == static_cast<int>(Kind::SoilAlbedo) || kind == static_cast<int>(Kind::MossyRock) ||
		       kind == static_cast<int>(Kind::Sand) || kind == static_cast<int>(Kind::RustedMetal) ||
		       kind == static_cast<int>(Kind::CrackedRock);
	}

	const char* GetTypeName() const override { return "NoiseTextureComponent"; }
	bool AllowMultiple() const override { return false; }

	void Initialize() override;

	/// <summary>
	/// プレハブ経由でInstantiateされた場合、Initialize()はAddComponent直後(=まだJSONの
	/// 値が反映される前)に走ってしまう。デフォルト値(kind=RockAlbedo等)で一度実行され、
	/// 対応するコンポーネントが無いと何も適用されないまま終わる。
	/// JSON反映後に必ず呼ばれるこのフックでも実行し直すことで取りこぼしを防ぐ。
	/// </summary>
	void OnAfterReadJson() override { RegenerateAndApply(); }

	/// <summary>
	/// 今の設定でテクスチャを作り直し、同じGameObjectのレンダラーへ適用し直す。
	/// InspectorのRegenerateボタン、または外部から見た目を変えたい時に呼ぶ。
	/// </summary>
	void RegenerateAndApply();

	void RegisterInvokableMethods(InvokableMethodRegistry& registry) override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_INT_NAMED_TIP(kind_, "Kind", 1.0f, 0, 6,
		    "**モデル用(ModelRendererComponent)**\n"
		    "  0 = 岩肌 / 2 = 土 / 4 = 苔むした岩 / 5 = 砂 / 6 = 錆びた金属\n"
		    "**パーティクル用(ParticleSystemComponent)**\n"
		    "  1 = 土埃 / 3 = 炎\n"
		    "対応するコンポーネントが同じGameObjectに無ければ**何も起きない**(警告も出ない)。");
		KUJATA_REGISTER_INT_NAMED_TIP(textureSize_, "Texture Size", 1.0f, 8, 1024,
		    "生成するテクスチャの一辺のピクセル数。大きいほど高精細だが生成が重くなる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(seedLane_, "Seed", 1.0f, -1000.0f, 1000.0f,
		    "ノイズを読み出す位置をずらす値。同じ設定のまま模様だけ変えたい時に使う。\n"
		    "**同じ設定+同じSeedなら生成は1回だけ**なので、壁を何枚並べても重くならない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(tileSize_, "Tile Size", 0.1f, 0.05f, 200.0f,
		    "モデル用のkindにのみ効く。**テクスチャ1枚が何ワールドユニットぶんになるか。**\n"
		    "ワールド座標で貼る(トライプラナー)ので、Transformでどう引き伸ばしても模様は伸びず、\n"
		    "面ごと・オブジェクトごとに密度が揃う。小さいほど細かく敷き詰められる。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(tint_, "Tint", 0.01f, 0.0f, 4.0f,
		    "岩肌/土(ModelRendererComponent)にのみ効く色の掛け算(RGBA)。**白(1,1,1,1)なら何もしない。**\n"
		    "生成されるテクスチャは色まで焼き込まれているので、そのままだと何に貼っても同じ灰色になる。\n"
		    "ここを変えると**同じ模様のまま別の素材に見せられる**(緑寄りで苔、赤寄りで赤土)。");
	}

	KUJATA_FIELD_INT(kind_, 0);
	KUJATA_FIELD_INT(textureSize_, 256);
	KUJATA_FIELD_FLOAT(seedLane_, 0.0f);
	KUJATA_FIELD_FLOAT(tileSize_, 4.0f);
	KUJATA_FIELD_VECTOR4(tint_, (KujataEngine::Vector4{1.0f, 1.0f, 1.0f, 1.0f}));
};

} // namespace KujataEngine
