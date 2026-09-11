#include "VolumeProfile.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif // USE_IMGUI

#include <algorithm>

namespace KujataEngine {

namespace {

// --- ブレンド用の小道具 ---------------------------------------------------

float Lerp(float destination, float source, float weight) { return destination + (source - destination) * weight; }

Vector3 LerpVector3(const Vector3& destination, const Vector3& source, float weight) {
	return {
	    Lerp(destination.x, source.x, weight),
	    Lerp(destination.y, source.y, weight),
	    Lerp(destination.z, source.z, weight),
	};
}

Vector4 LerpVector4(const Vector4& destination, const Vector4& source, float weight) {
	return {
	    Lerp(destination.x, source.x, weight),
	    Lerp(destination.y, source.y, weight),
	    Lerp(destination.z, source.z, weight),
	    Lerp(destination.w, source.w, weight),
	};
}

// bool/intは中間値に意味が無いので、weightが半分を超えたところで置き換える(Unityのoverrideと同じ挙動)。
bool BlendBool(bool destination, bool source, float weight) { return weight >= 0.5f ? source : destination; }
int32_t BlendInt(int32_t destination, int32_t source, float weight) { return weight >= 0.5f ? source : destination; }

// --- JSON読み込み用の小道具(キーが無ければ現在値を維持) -------------------

void ReadBool(const nlohmann::json& json, const char* key, bool& out) {
	if (json.contains(key) && json.at(key).is_boolean()) {
		out = json.at(key).get<bool>();
	}
}

void ReadFloat(const nlohmann::json& json, const char* key, float& out) {
	if (json.contains(key) && json.at(key).is_number()) {
		out = json.at(key).get<float>();
	}
}

void ReadInt(const nlohmann::json& json, const char* key, int32_t& out) {
	if (json.contains(key) && json.at(key).is_number_integer()) {
		out = json.at(key).get<int32_t>();
	}
}

void ReadVector3(const nlohmann::json& json, const char* key, Vector3& out) {
	if (json.contains(key) && json.at(key).is_array() && json.at(key).size() >= 3) {
		out.x = json.at(key).at(0).get<float>();
		out.y = json.at(key).at(1).get<float>();
		out.z = json.at(key).at(2).get<float>();
	}
}

void ReadVector3AsVector4Rgb(const nlohmann::json& json, const char* key, Vector4& out) {
	if (json.contains(key) && json.at(key).is_array() && json.at(key).size() >= 3) {
		out.x = json.at(key).at(0).get<float>();
		out.y = json.at(key).at(1).get<float>();
		out.z = json.at(key).at(2).get<float>();
	}
}

// --- Bloom ----------------------------------------------------------------

void WriteBloom(const VolumeProfileData& profile, nlohmann::json& json) {
	json["enabled"] = profile.bloom.enabled;
	json["intensity"] = profile.bloom.intensity;
}

void ReadBloom(const nlohmann::json& json, VolumeProfileData& profile) {
	ReadBool(json, "enabled", profile.bloom.enabled);
	ReadFloat(json, "intensity", profile.bloom.intensity);
}

void BlendBloom(const VolumeProfileData& source, float weight, VolumeProfileData& destination) {
	destination.bloom.enabled = BlendBool(destination.bloom.enabled, source.bloom.enabled, weight);
	destination.bloom.intensity = Lerp(destination.bloom.intensity, source.bloom.intensity, weight);
}

bool DrawBloomInspector(VolumeProfileData& profile) {
#ifdef USE_IMGUI
	bool changed = false;
	changed |= ImGui::Checkbox("Enabled", &profile.bloom.enabled);
	changed |= ImGui::DragFloat("Intensity", &profile.bloom.intensity, 0.01f, 0.0f, 5.0f);
	return changed;
#else
	(void)profile;
	return false;
#endif // USE_IMGUI
}

// --- Tonemap --------------------------------------------------------------

void WriteTonemap(const VolumeProfileData& profile, nlohmann::json& json) {
	json["exposure"] = profile.tonemap.exposure;
	json["tonemapper"] = profile.tonemap.tonemapper;
}

void ReadTonemap(const nlohmann::json& json, VolumeProfileData& profile) {
	ReadFloat(json, "exposure", profile.tonemap.exposure);
	ReadInt(json, "tonemapper", profile.tonemap.tonemapper);
	profile.tonemap.tonemapper = std::clamp(profile.tonemap.tonemapper, 0, 2);
}

void BlendTonemap(const VolumeProfileData& source, float weight, VolumeProfileData& destination) {
	destination.tonemap.exposure = Lerp(destination.tonemap.exposure, source.tonemap.exposure, weight);
	destination.tonemap.tonemapper = BlendInt(destination.tonemap.tonemapper, source.tonemap.tonemapper, weight);
}

bool DrawTonemapInspector(VolumeProfileData& profile) {
#ifdef USE_IMGUI
	bool changed = false;
	changed |= ImGui::DragFloat("Exposure", &profile.tonemap.exposure, 0.01f, 0.0f, 10.0f);
	const char* tonemapItems[] = {"None", "Reinhard", "ACES"};
	int tonemapIndex = profile.tonemap.tonemapper;
	if (ImGui::Combo("Tonemapper", &tonemapIndex, tonemapItems, IM_ARRAYSIZE(tonemapItems))) {
		profile.tonemap.tonemapper = tonemapIndex;
		changed = true;
	}
	return changed;
#else
	(void)profile;
	return false;
#endif // USE_IMGUI
}

// --- Color Grading --------------------------------------------------------

void WriteColorGrading(const VolumeProfileData& profile, nlohmann::json& json) {
	json["saturation"] = profile.colorGrading.saturation;
	json["contrast"] = profile.colorGrading.contrast;
	json["colorFilter"] = {profile.colorGrading.colorFilter.x, profile.colorGrading.colorFilter.y, profile.colorGrading.colorFilter.z};
}

void ReadColorGrading(const nlohmann::json& json, VolumeProfileData& profile) {
	ReadFloat(json, "saturation", profile.colorGrading.saturation);
	ReadFloat(json, "contrast", profile.colorGrading.contrast);
	ReadVector3AsVector4Rgb(json, "colorFilter", profile.colorGrading.colorFilter);
}

void BlendColorGrading(const VolumeProfileData& source, float weight, VolumeProfileData& destination) {
	destination.colorGrading.saturation = Lerp(destination.colorGrading.saturation, source.colorGrading.saturation, weight);
	destination.colorGrading.contrast = Lerp(destination.colorGrading.contrast, source.colorGrading.contrast, weight);
	destination.colorGrading.colorFilter = LerpVector4(destination.colorGrading.colorFilter, source.colorGrading.colorFilter, weight);
}

bool DrawColorGradingInspector(VolumeProfileData& profile) {
#ifdef USE_IMGUI
	bool changed = false;
	changed |= ImGui::SliderFloat("Saturation", &profile.colorGrading.saturation, 0.0f, 2.0f);
	changed |= ImGui::SliderFloat("Contrast", &profile.colorGrading.contrast, 0.0f, 2.0f);
	changed |= ImGui::ColorEdit3("Color Filter", &profile.colorGrading.colorFilter.x);
	return changed;
#else
	(void)profile;
	return false;
#endif // USE_IMGUI
}

// --- Vignette -------------------------------------------------------------

void WriteVignette(const VolumeProfileData& profile, nlohmann::json& json) {
	json["intensity"] = profile.vignette.intensity;
	json["smoothness"] = profile.vignette.smoothness;
}

void ReadVignette(const nlohmann::json& json, VolumeProfileData& profile) {
	ReadFloat(json, "intensity", profile.vignette.intensity);
	ReadFloat(json, "smoothness", profile.vignette.smoothness);
}

void BlendVignette(const VolumeProfileData& source, float weight, VolumeProfileData& destination) {
	destination.vignette.intensity = Lerp(destination.vignette.intensity, source.vignette.intensity, weight);
	destination.vignette.smoothness = Lerp(destination.vignette.smoothness, source.vignette.smoothness, weight);
}

bool DrawVignetteInspector(VolumeProfileData& profile) {
#ifdef USE_IMGUI
	bool changed = false;
	changed |= ImGui::SliderFloat("Intensity", &profile.vignette.intensity, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("Smoothness", &profile.vignette.smoothness, 0.01f, 1.0f);
	return changed;
#else
	(void)profile;
	return false;
#endif // USE_IMGUI
}

// --- Fog ------------------------------------------------------------------

void WriteFog(const VolumeProfileData& profile, nlohmann::json& json) {
	json["enabled"] = profile.fog.enabled;
	json["color"] = {profile.fog.color.x, profile.fog.color.y, profile.fog.color.z};
	json["density"] = profile.fog.density;
	json["heightFalloff"] = profile.fog.heightFalloff;
	json["heightBase"] = profile.fog.heightBase;
	json["startDistance"] = profile.fog.startDistance;
	json["maxDistance"] = profile.fog.maxDistance;
	json["spotScatter"] = profile.fog.spotScatter;
}

void ReadFog(const nlohmann::json& json, VolumeProfileData& profile) {
	ReadBool(json, "enabled", profile.fog.enabled);
	ReadVector3(json, "color", profile.fog.color);
	ReadFloat(json, "density", profile.fog.density);
	ReadFloat(json, "heightFalloff", profile.fog.heightFalloff);
	ReadFloat(json, "heightBase", profile.fog.heightBase);
	ReadFloat(json, "startDistance", profile.fog.startDistance);
	ReadFloat(json, "maxDistance", profile.fog.maxDistance);
	ReadFloat(json, "spotScatter", profile.fog.spotScatter);
}

void BlendFog(const VolumeProfileData& source, float weight, VolumeProfileData& destination) {
	destination.fog.enabled = BlendBool(destination.fog.enabled, source.fog.enabled, weight);
	destination.fog.color = LerpVector3(destination.fog.color, source.fog.color, weight);
	destination.fog.density = Lerp(destination.fog.density, source.fog.density, weight);
	destination.fog.heightFalloff = Lerp(destination.fog.heightFalloff, source.fog.heightFalloff, weight);
	destination.fog.heightBase = Lerp(destination.fog.heightBase, source.fog.heightBase, weight);
	destination.fog.startDistance = Lerp(destination.fog.startDistance, source.fog.startDistance, weight);
	destination.fog.maxDistance = Lerp(destination.fog.maxDistance, source.fog.maxDistance, weight);
	destination.fog.spotScatter = Lerp(destination.fog.spotScatter, source.fog.spotScatter, weight);
}

bool DrawFogInspector(VolumeProfileData& profile) {
#ifdef USE_IMGUI
	bool changed = false;
	changed |= ImGui::Checkbox("Enabled", &profile.fog.enabled);
	changed |= ImGui::ColorEdit3("Color", &profile.fog.color.x);
	changed |= ImGui::DragFloat("Density", &profile.fog.density, 0.001f, 0.0f, 1.0f);
	changed |= ImGui::DragFloat("Height Falloff", &profile.fog.heightFalloff, 0.005f, 0.0f, 2.0f);
	changed |= ImGui::DragFloat("Height Base", &profile.fog.heightBase, 0.1f, -100.0f, 100.0f);
	changed |= ImGui::DragFloat("Start Distance", &profile.fog.startDistance, 0.1f, 0.0f, 100.0f);
	changed |= ImGui::DragFloat("Max Distance", &profile.fog.maxDistance, 0.5f, 1.0f, 1000.0f);
	changed |= ImGui::DragFloat("Spot Scatter", &profile.fog.spotScatter, 0.01f, 0.0f, 10.0f);
	return changed;
#else
	(void)profile;
	return false;
#endif // USE_IMGUI
}

} // namespace

const std::vector<VolumeEffectDescriptor>& GetVolumeEffectDescriptors() {
	// エフェクトを追加するときはここへ1エントリ足すだけでよい。
	// JSON入出力・ブレンド・Inspector・Volumeのoverride管理はすべてこの表を回して処理される。
	static const std::vector<VolumeEffectDescriptor> descriptors = {
	    {VolumeEffectId::kBloom, "Bloom", &WriteBloom, &ReadBloom, &BlendBloom, &DrawBloomInspector},
	    {VolumeEffectId::kTonemap, "Tonemap", &WriteTonemap, &ReadTonemap, &BlendTonemap, &DrawTonemapInspector},
	    {VolumeEffectId::kColorGrading, "ColorGrading", &WriteColorGrading, &ReadColorGrading, &BlendColorGrading, &DrawColorGradingInspector},
	    {VolumeEffectId::kVignette, "Vignette", &WriteVignette, &ReadVignette, &BlendVignette, &DrawVignetteInspector},
	    {VolumeEffectId::kFog, "Fog", &WriteFog, &ReadFog, &BlendFog, &DrawFogInspector},
	};
	return descriptors;
}

void WriteVolumeProfileJson(const VolumeProfileData& profile, nlohmann::json& json) {
	for (const VolumeEffectDescriptor& descriptor : GetVolumeEffectDescriptors()) {
		nlohmann::json effectJson = nlohmann::json::object();
		// overrideフラグは全エフェクト共通なので共通処理側で書く。
		effectJson["override"] = profile.IsOverriding(descriptor.id);
		descriptor.write(profile, effectJson);
		json[descriptor.name] = effectJson;
	}
}

void ReadVolumeProfileJson(const nlohmann::json& json, VolumeProfileData& profile) {
	for (const VolumeEffectDescriptor& descriptor : GetVolumeEffectDescriptors()) {
		if (!json.contains(descriptor.name) || !json.at(descriptor.name).is_object()) {
			continue;
		}
		const nlohmann::json& effectJson = json.at(descriptor.name);
		bool overrideState = profile.IsOverriding(descriptor.id);
		ReadBool(effectJson, "override", overrideState);
		profile.SetOverriding(descriptor.id, overrideState);
		descriptor.read(effectJson, profile);
	}
}

void BlendVolumeProfile(const VolumeProfileData& source, float weight, VolumeProfileData& destination) {
	if (weight <= 0.0f) {
		return;
	}
	float clampedWeight = std::clamp(weight, 0.0f, 1.0f);

	for (const VolumeEffectDescriptor& descriptor : GetVolumeEffectDescriptors()) {
		// overrideしていないエフェクトは素通し(下位Volumeや既定値の結果を残す)。
		if (!source.IsOverriding(descriptor.id)) {
			continue;
		}
		descriptor.blend(source, clampedWeight, destination);
		destination.SetOverriding(descriptor.id, true);
	}
}

} // namespace KujataEngine
