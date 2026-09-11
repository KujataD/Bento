#include "AnimationClipAsset.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace KujataEngine {

namespace {

// 拡張子は .anim.json(2重拡張)。filename末尾で判定する。
constexpr const char* kClipFileSuffix = ".anim.json";

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key) || !json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

// JSONはInfinityを表現できないため、Constantタンジェントは十分大きい番兵値で保存する。
constexpr float kTangentSentinel = 1.0e30f;

float SanitizeTangentForSave(float tangent) {
	if (!std::isfinite(tangent)) {
		return (tangent > 0.0f) ? kTangentSentinel : -kTangentSentinel;
	}
	return tangent;
}

float RestoreTangentFromLoad(float tangent) {
	if (std::abs(tangent) >= kTangentSentinel) {
		return (tangent > 0.0f) ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity();
	}
	return tangent;
}

constexpr float kEasePi = 3.14159265358979323846f;

// easings.netのeaseOutBounce。In/InOutはこれを鏡映して作る。
float EaseOutBounceImpl(float t) {
	constexpr float n1 = 7.5625f;
	constexpr float d1 = 2.75f;
	if (t < 1.0f / d1) {
		return n1 * t * t;
	}
	if (t < 2.0f / d1) {
		t -= 1.5f / d1;
		return n1 * t * t + 0.75f;
	}
	if (t < 2.5f / d1) {
		t -= 2.25f / d1;
		return n1 * t * t + 0.9375f;
	}
	t -= 2.625f / d1;
	return n1 * t * t + 0.984375f;
}

// EasingTypeとJSON保存名の対応表(enum宣言順)。
struct EasingName {
	EasingType easing;
	const char* name;
};

constexpr EasingName kEasingNames[] = {
    {EasingType::None, "None"},
    {EasingType::EaseInSine, "EaseInSine"},
    {EasingType::EaseOutSine, "EaseOutSine"},
    {EasingType::EaseInOutSine, "EaseInOutSine"},
    {EasingType::EaseInQuad, "EaseInQuad"},
    {EasingType::EaseOutQuad, "EaseOutQuad"},
    {EasingType::EaseInOutQuad, "EaseInOutQuad"},
    {EasingType::EaseInCubic, "EaseInCubic"},
    {EasingType::EaseOutCubic, "EaseOutCubic"},
    {EasingType::EaseInOutCubic, "EaseInOutCubic"},
    {EasingType::EaseInQuart, "EaseInQuart"},
    {EasingType::EaseOutQuart, "EaseOutQuart"},
    {EasingType::EaseInOutQuart, "EaseInOutQuart"},
    {EasingType::EaseInQuint, "EaseInQuint"},
    {EasingType::EaseOutQuint, "EaseOutQuint"},
    {EasingType::EaseInOutQuint, "EaseInOutQuint"},
    {EasingType::EaseInExpo, "EaseInExpo"},
    {EasingType::EaseOutExpo, "EaseOutExpo"},
    {EasingType::EaseInOutExpo, "EaseInOutExpo"},
    {EasingType::EaseInCirc, "EaseInCirc"},
    {EasingType::EaseOutCirc, "EaseOutCirc"},
    {EasingType::EaseInOutCirc, "EaseInOutCirc"},
    {EasingType::EaseInBack, "EaseInBack"},
    {EasingType::EaseOutBack, "EaseOutBack"},
    {EasingType::EaseInOutBack, "EaseInOutBack"},
    {EasingType::EaseInElastic, "EaseInElastic"},
    {EasingType::EaseOutElastic, "EaseOutElastic"},
    {EasingType::EaseInOutElastic, "EaseInOutElastic"},
    {EasingType::EaseInBounce, "EaseInBounce"},
    {EasingType::EaseOutBounce, "EaseOutBounce"},
    {EasingType::EaseInOutBounce, "EaseInOutBounce"},
};

} // namespace

float AnimationCurve::Evaluate(float time) const {
	if (keys.empty()) {
		return 0.0f;
	}
	if (keys.size() == 1 || time <= keys.front().time) {
		return keys.front().value;
	}
	if (time >= keys.back().time) {
		return keys.back().value;
	}

	// time昇順前提でセグメントを2分探索する(k0.time <= time < k1.time)。
	size_t low = 0;
	size_t high = keys.size() - 1;
	while (high - low > 1) {
		size_t mid = (low + high) / 2;
		if (keys[mid].time <= time) {
			low = mid;
		} else {
			high = mid;
		}
	}

	const AnimationKeyframe& k0 = keys[low];
	const AnimationKeyframe& k1 = keys[high];

	float dt = k1.time - k0.time;
	if (dt <= 1.0e-6f) {
		return k1.value;
	}

	// この区間にイージング指定があれば、値は両端キー固定でイージング関数補間する。
	if (k0.easing != EasingType::None) {
		float normalized = (time - k0.time) / dt;
		return k0.value + (k1.value - k0.value) * AnimationClipAsset::EvaluateEasing(k0.easing, normalized);
	}

	// どちらかのタンジェントが非有限ならステップ補間(Unity準拠)。
	if (!std::isfinite(k0.outTangent) || !std::isfinite(k1.inTangent)) {
		return k0.value;
	}

	float t = (time - k0.time) / dt;
	float t2 = t * t;
	float t3 = t2 * t;

	// cubic Hermite基底。タンジェントは「値/秒」なのでdtでスケールする。
	float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
	float h10 = t3 - 2.0f * t2 + t;
	float h01 = -2.0f * t3 + 3.0f * t2;
	float h11 = t3 - t2;

	return h00 * k0.value + h10 * dt * k0.outTangent + h01 * k1.value + h11 * dt * k1.inTangent;
}

int AnimationCurve::AddKey(const AnimationKeyframe& key) {
	// 同時刻(誤差内)の既存キーは上書きする。
	constexpr float kSameTimeEpsilon = 1.0e-5f;
	for (size_t i = 0; i < keys.size(); ++i) {
		if (std::abs(keys[i].time - key.time) <= kSameTimeEpsilon) {
			keys[i] = key;
			return static_cast<int>(i);
		}
	}

	auto insertPosition = std::upper_bound(keys.begin(), keys.end(), key, [](const AnimationKeyframe& a, const AnimationKeyframe& b) { return a.time < b.time; });
	auto inserted = keys.insert(insertPosition, key);
	return static_cast<int>(inserted - keys.begin());
}

float AnimationCurve::GetDuration() const {
	if (keys.empty()) {
		return 0.0f;
	}
	return keys.back().time;
}

float AnimationClipData::WrapTime(float time, bool& outFinished) const {
	outFinished = false;
	float duration = GetDuration();
	if (duration <= 0.0f) {
		return 0.0f;
	}
	if (time <= 0.0f) {
		return 0.0f;
	}

	switch (wrapMode) {
	case AnimationWrapMode::Once:
		if (time >= duration) {
			outFinished = true;
			return duration;
		}
		return time;
	case AnimationWrapMode::Loop:
		return std::fmod(time, duration);
	case AnimationWrapMode::PingPong: {
		// 周期2*durationの三角波: 0→d→0→d…
		float cycle = std::fmod(time, duration * 2.0f);
		return duration - std::abs(cycle - duration);
	}
	default:
		return time;
	}
}

float AnimationClipData::GetDuration() const {
	float duration = 0.0f;
	for (const AnimationTrack& track : tracks) {
		duration = (std::max)(duration, track.curve.GetDuration());
	}
	return duration;
}

AnimationTrack* AnimationClipData::FindTrack(const std::string& path) {
	for (AnimationTrack& track : tracks) {
		if (track.path == path) {
			return &track;
		}
	}
	return nullptr;
}

const AnimationTrack* AnimationClipData::FindTrack(const std::string& path) const {
	for (const AnimationTrack& track : tracks) {
		if (track.path == path) {
			return &track;
		}
	}
	return nullptr;
}

AnimationTrack& AnimationClipData::GetOrAddTrack(const std::string& path) {
	if (AnimationTrack* existing = FindTrack(path)) {
		return *existing;
	}
	AnimationTrack track;
	track.path = path;
	tracks.push_back(std::move(track));
	return tracks.back();
}

float AnimationClipAsset::ConstantTangent() {
	return std::numeric_limits<float>::infinity();
}

float AnimationClipAsset::EvaluateEasing(EasingType easing, float t) {
	t = std::clamp(t, 0.0f, 1.0f);

	switch (easing) {
	case EasingType::None:
		return t;

	case EasingType::EaseInSine:
		return 1.0f - std::cos(t * kEasePi / 2.0f);
	case EasingType::EaseOutSine:
		return std::sin(t * kEasePi / 2.0f);
	case EasingType::EaseInOutSine:
		return -(std::cos(kEasePi * t) - 1.0f) / 2.0f;

	case EasingType::EaseInQuad:
		return t * t;
	case EasingType::EaseOutQuad:
		return 1.0f - (1.0f - t) * (1.0f - t);
	case EasingType::EaseInOutQuad:
		return (t < 0.5f) ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;

	case EasingType::EaseInCubic:
		return t * t * t;
	case EasingType::EaseOutCubic:
		return 1.0f - std::pow(1.0f - t, 3.0f);
	case EasingType::EaseInOutCubic:
		return (t < 0.5f) ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;

	case EasingType::EaseInQuart:
		return t * t * t * t;
	case EasingType::EaseOutQuart:
		return 1.0f - std::pow(1.0f - t, 4.0f);
	case EasingType::EaseInOutQuart:
		return (t < 0.5f) ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;

	case EasingType::EaseInQuint:
		return t * t * t * t * t;
	case EasingType::EaseOutQuint:
		return 1.0f - std::pow(1.0f - t, 5.0f);
	case EasingType::EaseInOutQuint:
		return (t < 0.5f) ? 16.0f * t * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;

	case EasingType::EaseInExpo:
		return (t <= 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
	case EasingType::EaseOutExpo:
		return (t >= 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
	case EasingType::EaseInOutExpo:
		if (t <= 0.0f) {
			return 0.0f;
		}
		if (t >= 1.0f) {
			return 1.0f;
		}
		return (t < 0.5f) ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;

	case EasingType::EaseInCirc:
		return 1.0f - std::sqrt(1.0f - t * t);
	case EasingType::EaseOutCirc:
		return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
	case EasingType::EaseInOutCirc:
		return (t < 0.5f) ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;

	case EasingType::EaseInBack: {
		constexpr float c1 = 1.70158f;
		constexpr float c3 = c1 + 1.0f;
		return c3 * t * t * t - c1 * t * t;
	}
	case EasingType::EaseOutBack: {
		constexpr float c1 = 1.70158f;
		constexpr float c3 = c1 + 1.0f;
		float u = t - 1.0f;
		return 1.0f + c3 * u * u * u + c1 * u * u;
	}
	case EasingType::EaseInOutBack: {
		constexpr float c1 = 1.70158f;
		constexpr float c2 = c1 * 1.525f;
		if (t < 0.5f) {
			float u = 2.0f * t;
			return (u * u * ((c2 + 1.0f) * u - c2)) / 2.0f;
		}
		float u = 2.0f * t - 2.0f;
		return (u * u * ((c2 + 1.0f) * u + c2) + 2.0f) / 2.0f;
	}

	case EasingType::EaseInElastic: {
		constexpr float c4 = 2.0f * kEasePi / 3.0f;
		if (t <= 0.0f) {
			return 0.0f;
		}
		if (t >= 1.0f) {
			return 1.0f;
		}
		return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
	}
	case EasingType::EaseOutElastic: {
		constexpr float c4 = 2.0f * kEasePi / 3.0f;
		if (t <= 0.0f) {
			return 0.0f;
		}
		if (t >= 1.0f) {
			return 1.0f;
		}
		return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
	}
	case EasingType::EaseInOutElastic: {
		constexpr float c5 = 2.0f * kEasePi / 4.5f;
		if (t <= 0.0f) {
			return 0.0f;
		}
		if (t >= 1.0f) {
			return 1.0f;
		}
		if (t < 0.5f) {
			return -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f;
		}
		return (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
	}

	case EasingType::EaseInBounce:
		return 1.0f - EaseOutBounceImpl(1.0f - t);
	case EasingType::EaseOutBounce:
		return EaseOutBounceImpl(t);
	case EasingType::EaseInOutBounce:
		return (t < 0.5f) ? (1.0f - EaseOutBounceImpl(1.0f - 2.0f * t)) / 2.0f : (1.0f + EaseOutBounceImpl(2.0f * t - 1.0f)) / 2.0f;

	default:
		return t;
	}
}

const char* AnimationClipAsset::ToString(EasingType easing) {
	for (const EasingName& entry : kEasingNames) {
		if (entry.easing == easing) {
			return entry.name;
		}
	}
	return "None";
}

bool AnimationClipAsset::TryParseEasing(const std::string& name, EasingType& outEasing) {
	for (const EasingName& entry : kEasingNames) {
		if (name == entry.name) {
			outEasing = entry.easing;
			return true;
		}
	}
	return false;
}

const char* AnimationClipAsset::ToString(AnimationWrapMode wrapMode) {
	switch (wrapMode) {
	case AnimationWrapMode::Once:
		return "Once";
	case AnimationWrapMode::PingPong:
		return "PingPong";
	case AnimationWrapMode::Loop:
	default:
		return "Loop";
	}
}

bool AnimationClipAsset::TryParseWrapMode(const std::string& name, AnimationWrapMode& outWrapMode) {
	if (name == "Once") {
		outWrapMode = AnimationWrapMode::Once;
		return true;
	}
	if (name == "Loop") {
		outWrapMode = AnimationWrapMode::Loop;
		return true;
	}
	if (name == "PingPong") {
		outWrapMode = AnimationWrapMode::PingPong;
		return true;
	}
	return false;
}

void AnimationClipAsset::SetLinearTangents(AnimationCurve& curve, int keyIndex) {
	if (keyIndex < 0 || keyIndex >= static_cast<int>(curve.keys.size())) {
		return;
	}
	AnimationKeyframe& key = curve.keys[keyIndex];

	if (keyIndex > 0) {
		const AnimationKeyframe& prev = curve.keys[keyIndex - 1];
		float dt = key.time - prev.time;
		key.inTangent = (dt > 1.0e-6f) ? (key.value - prev.value) / dt : 0.0f;
	} else {
		key.inTangent = 0.0f;
	}

	if (keyIndex + 1 < static_cast<int>(curve.keys.size())) {
		const AnimationKeyframe& next = curve.keys[keyIndex + 1];
		float dt = next.time - key.time;
		key.outTangent = (dt > 1.0e-6f) ? (next.value - key.value) / dt : 0.0f;
	} else {
		key.outTangent = 0.0f;
	}
}

void AnimationClipAsset::SetSmoothTangents(AnimationCurve& curve, int keyIndex) {
	if (keyIndex < 0 || keyIndex >= static_cast<int>(curve.keys.size())) {
		return;
	}
	AnimationKeyframe& key = curve.keys[keyIndex];

	// Catmull-Rom: 前後キーを結ぶ直線の傾きをin/out共通のタンジェントにする。端は片側の傾き。
	const AnimationKeyframe* prev = (keyIndex > 0) ? &curve.keys[keyIndex - 1] : nullptr;
	const AnimationKeyframe* next = (keyIndex + 1 < static_cast<int>(curve.keys.size())) ? &curve.keys[keyIndex + 1] : nullptr;

	float tangent = 0.0f;
	if (prev && next) {
		float dt = next->time - prev->time;
		tangent = (dt > 1.0e-6f) ? (next->value - prev->value) / dt : 0.0f;
	} else if (prev) {
		float dt = key.time - prev->time;
		tangent = (dt > 1.0e-6f) ? (key.value - prev->value) / dt : 0.0f;
	} else if (next) {
		float dt = next->time - key.time;
		tangent = (dt > 1.0e-6f) ? (next->value - key.value) / dt : 0.0f;
	}

	key.inTangent = tangent;
	key.outTangent = tangent;
}

bool AnimationClipAsset::IsClipFile(const std::filesystem::path& path) {
	std::string filename = path.filename().generic_string();
	std::string suffix = kClipFileSuffix;
	if (filename.size() <= suffix.size()) {
		return false;
	}
	std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool AnimationClipAsset::Load(const std::filesystem::path& path, AnimationClipData& outClip, std::string& message) {
	std::ifstream file(path);
	if (!file.is_open()) {
		message = "Failed to open animation clip: " + path.generic_string();
		return false;
	}

	nlohmann::json json;
	try {
		file >> json;
	} catch (const nlohmann::json::exception& exception) {
		message = std::string("Failed to parse animation clip JSON: ") + exception.what();
		return false;
	}

	if (!json.is_object()) {
		message = "Animation clip JSON is not an object.";
		return false;
	}

	outClip = ReadJsonObject(json);
	message.clear();
	return true;
}

bool AnimationClipAsset::Save(const std::filesystem::path& path, const AnimationClipData& clip, std::string& message) {
	nlohmann::json json;
	WriteJsonObject(json, clip);

	std::ofstream file(path);
	if (!file.is_open()) {
		message = "Failed to open animation clip for write: " + path.generic_string();
		return false;
	}

	file << json.dump(2);
	message.clear();
	return true;
}

bool AnimationClipAsset::CreateDefaultFile(const std::filesystem::path& path, std::string& message) {
	AnimationClipData clip;
	clip.name = path.filename().generic_string();
	// "xxx.anim.json" → "xxx" を表示名にする。
	std::string suffix = kClipFileSuffix;
	if (clip.name.size() > suffix.size()) {
		clip.name = clip.name.substr(0, clip.name.size() - suffix.size());
	}
	return Save(path, clip, message);
}

AnimationClipData AnimationClipAsset::ReadJsonObject(const nlohmann::json& json) {
	AnimationClipData clip;
	if (json.contains("name") && json.at("name").is_string()) {
		clip.name = json.at("name").get<std::string>();
	}
	// 旧形式("loop": bool)からの互換読み込み。wrapModeがあれば優先する。
	if (json.contains("loop") && json.at("loop").is_boolean()) {
		clip.wrapMode = json.at("loop").get<bool>() ? AnimationWrapMode::Loop : AnimationWrapMode::Once;
	}
	if (json.contains("wrapMode") && json.at("wrapMode").is_string()) {
		TryParseWrapMode(json.at("wrapMode").get<std::string>(), clip.wrapMode);
	}

	if (json.contains("tracks") && json.at("tracks").is_array()) {
		for (const nlohmann::json& trackJson : json.at("tracks")) {
			if (!trackJson.is_object()) {
				continue;
			}
			if (!trackJson.contains("path") || !trackJson.at("path").is_string()) {
				continue;
			}

			AnimationTrack track;
			track.path = trackJson.at("path").get<std::string>();
			if (trackJson.contains("additive") && trackJson.at("additive").is_boolean()) {
				track.additive = trackJson.at("additive").get<bool>();
			}

			if (trackJson.contains("keys") && trackJson.at("keys").is_array()) {
				for (const nlohmann::json& keyJson : trackJson.at("keys")) {
					if (!keyJson.is_object()) {
						continue;
					}
					AnimationKeyframe key;
					key.time = ReadFloat(keyJson, "time", 0.0f);
					key.value = ReadFloat(keyJson, "value", 0.0f);
					key.inTangent = RestoreTangentFromLoad(ReadFloat(keyJson, "inTangent", 0.0f));
					key.outTangent = RestoreTangentFromLoad(ReadFloat(keyJson, "outTangent", 0.0f));
					if (keyJson.contains("easing") && keyJson.at("easing").is_string()) {
						TryParseEasing(keyJson.at("easing").get<std::string>(), key.easing);
					}
					track.curve.AddKey(key);
				}
			}

			clip.tracks.push_back(std::move(track));
		}
	}

	return clip;
}

void AnimationClipAsset::WriteJsonObject(nlohmann::json& json, const AnimationClipData& clip) {
	json["name"] = clip.name;
	json["wrapMode"] = ToString(clip.wrapMode);

	nlohmann::json tracksJson = nlohmann::json::array();
	for (const AnimationTrack& track : clip.tracks) {
		nlohmann::json trackJson;
		trackJson["path"] = track.path;
		if (track.additive) {
			trackJson["additive"] = true;
		}

		nlohmann::json keysJson = nlohmann::json::array();
		for (const AnimationKeyframe& key : track.curve.keys) {
			nlohmann::json keyJson;
			keyJson["time"] = key.time;
			keyJson["value"] = key.value;
			keyJson["inTangent"] = SanitizeTangentForSave(key.inTangent);
			keyJson["outTangent"] = SanitizeTangentForSave(key.outTangent);
			if (key.easing != EasingType::None) {
				keyJson["easing"] = ToString(key.easing);
			}
			keysJson.push_back(keyJson);
		}
		trackJson["keys"] = keysJson;
		tracksJson.push_back(trackJson);
	}
	json["tracks"] = tracksJson;
}

} // namespace KujataEngine
