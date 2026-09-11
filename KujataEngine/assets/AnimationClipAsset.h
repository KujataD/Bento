#pragma once

#include "../runtime/KujataApi.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <filesystem>
#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>
/// easings.net準拠のイージング関数種別。Noneはタンジェント(Hermite)補間。
/// </summary>
enum class EasingType {
	None, // Hermiteタンジェント補間(Smooth/Linear/Flat/Constantプリセット)
	EaseInSine,
	EaseOutSine,
	EaseInOutSine,
	EaseInQuad,
	EaseOutQuad,
	EaseInOutQuad,
	EaseInCubic,
	EaseOutCubic,
	EaseInOutCubic,
	EaseInQuart,
	EaseOutQuart,
	EaseInOutQuart,
	EaseInQuint,
	EaseOutQuint,
	EaseInOutQuint,
	EaseInExpo,
	EaseOutExpo,
	EaseInOutExpo,
	EaseInCirc,
	EaseOutCirc,
	EaseInOutCirc,
	EaseInBack,
	EaseOutBack,
	EaseInOutBack,
	EaseInElastic,
	EaseOutElastic,
	EaseInOutElastic,
	EaseInBounce,
	EaseOutBounce,
	EaseInOutBounce,
};

/// <summary>
/// アニメーションカーブの1キー。Unity同様、前後のセグメントをin/outタンジェント(傾き)で制御する。
/// タンジェントが±∞(非有限)のセグメントはステップ(Constant)補間になる。
/// easingがNone以外の場合、このキーから次のキーまでの区間はタンジェントの代わりに
/// イージング関数(値は両端キーで固定)で補間する。
/// </summary>
struct AnimationKeyframe {
	float time = 0.0f;
	float value = 0.0f;
	float inTangent = 0.0f;
	float outTangent = 0.0f;
	EasingType easing = EasingType::None;
};

/// <summary>
/// float 1チャンネル分のアニメーションカーブ(cubic Hermite補間)。
/// </summary>
struct AnimationCurve {
	std::vector<AnimationKeyframe> keys;

	/// <summary>
	/// 指定時刻の値を評価します。範囲外は端キーの値でクランプします。
	/// </summary>
	KUJATA_API float Evaluate(float time) const;

	/// <summary>
	/// キーをtime昇順を保って挿入し、挿入位置を返します。同時刻キーは値を上書きします。
	/// </summary>
	KUJATA_API int AddKey(const AnimationKeyframe& key);

	KUJATA_API float GetDuration() const;
};

/// <summary>
/// 1プロパティ分のトラック。pathは "ComponentTypeName/チャンネル名" 形式
/// (例: "Transform/position.x", "RotatorComponent/speed")。
/// additive=trueのトラックは、カーブ値を絶対値ではなく「再生開始時点の値への差分」として加算する
/// (攻撃中に少し前進する等、現在位置基準のアニメーション用。カーブは0始まりの差分で作る)。
/// </summary>
struct AnimationTrack {
	std::string path;
	bool additive = false;
	AnimationCurve curve;
};

/// <summary>
/// クリップ終端の挙動。Once=停止 / Loop=先頭へ / PingPong=往復。
/// </summary>
enum class AnimationWrapMode {
	Once,
	Loop,
	PingPong,
};

/// <summary>
/// アニメーションクリップ本体。JSONアセット(*.anim.json)としてProject管理する。
/// </summary>
struct AnimationClipData {
	std::string name = "New Animation";
	AnimationWrapMode wrapMode = AnimationWrapMode::Loop;
	std::vector<AnimationTrack> tracks;

	/// <summary>
	/// 経過時間(累積)をwrapModeに従って評価時刻[0,duration]へ変換します。
	/// Onceで終端に達した場合はoutFinishedがtrueになります。
	/// </summary>
	KUJATA_API float WrapTime(float time, bool& outFinished) const;

	/// <summary>
	/// 全トラック中の最終キー時刻を返します。
	/// </summary>
	KUJATA_API float GetDuration() const;

	/// <summary>
	/// 指定pathのトラックを返します。無ければnullptr。
	/// </summary>
	KUJATA_API AnimationTrack* FindTrack(const std::string& path);
	KUJATA_API const AnimationTrack* FindTrack(const std::string& path) const;

	/// <summary>
	/// 指定pathのトラックを返し、無ければ作成して返します。
	/// </summary>
	KUJATA_API AnimationTrack& GetOrAddTrack(const std::string& path);
};

/// <summary>
/// タンジェント計算ユーティリティとクリップJSONの読み書き(MaterialAsset方式)。
/// </summary>
class AnimationClipAsset {
public:
	/// <summary>ステップ補間を表すタンジェント値(∞)。</summary>
	static KUJATA_API float ConstantTangent();

	/// <summary>
	/// 正規化時刻t[0,1]をイージング関数で変換します(easings.net準拠)。
	/// </summary>
	static KUJATA_API float EvaluateEasing(EasingType easing, float t);

	static KUJATA_API const char* ToString(EasingType easing);

	static KUJATA_API bool TryParseEasing(const std::string& name, EasingType& outEasing);

	static KUJATA_API const char* ToString(AnimationWrapMode wrapMode);

	static KUJATA_API bool TryParseWrapMode(const std::string& name, AnimationWrapMode& outWrapMode);

	/// <summary>
	/// keyIndexのキーのin/outタンジェントを隣接キーへの直線の傾きに設定します(Linear)。
	/// </summary>
	static KUJATA_API void SetLinearTangents(AnimationCurve& curve, int keyIndex);

	/// <summary>
	/// keyIndexのキーのin/outタンジェントをCatmull-Rom(前後キーの平均傾き)に設定します(Smooth)。
	/// </summary>
	static KUJATA_API void SetSmoothTangents(AnimationCurve& curve, int keyIndex);

	static KUJATA_API bool IsClipFile(const std::filesystem::path& path);

	static KUJATA_API bool Load(const std::filesystem::path& path, AnimationClipData& outClip, std::string& message);

	static KUJATA_API bool Save(const std::filesystem::path& path, const AnimationClipData& clip, std::string& message);

	static KUJATA_API bool CreateDefaultFile(const std::filesystem::path& path, std::string& message);

	static KUJATA_API AnimationClipData ReadJsonObject(const nlohmann::json& json);

	static KUJATA_API void WriteJsonObject(nlohmann::json& json, const AnimationClipData& clip);
};

} // namespace KujataEngine
