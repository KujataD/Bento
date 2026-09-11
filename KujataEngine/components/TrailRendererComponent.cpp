#include "TrailRendererComponent.h"

#include "../3d/Camera.h"
#include "../assets/MaterialAsset.h"
#include "../base/Time.h"
#include "../runtime/AssetResolver.h"
#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"

#include <algorithm>
#include <cmath>

namespace KujataEngine {

namespace {

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key) || !json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

// エンジン内なのでDot/Crossは直接使えるが、ここは意図を明示するため名前を付けておく。
Vector3 CrossProduct(const Vector3& a, const Vector3& b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vector3 SafeNormalize(const Vector3& v, const Vector3& fallback) {
	float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	if (length <= 1.0e-5f) {
		return fallback;
	}
	return {v.x / length, v.y / length, v.z / length};
}

} // namespace

void TrailRendererComponent::OnPlayStart() {
	// 前回Playの軌跡が残らないように毎回まっさらから始める。
	points_.clear();
	materialDirty_ = true;
}

void TrailRendererComponent::EnsureModel() {
	if (model_) {
		return;
	}

	// 1点につき四角形1つ = 三角形2つ = 6頂点。上限ぶんを最初に確保しておく。
	uint32_t maxVertices = static_cast<uint32_t>((std::max)(maxPoints_, 2)) * 6;
	model_.reset(Model::CreateDynamic(maxVertices, "", ShaderModel::kNone));
	materialDirty_ = true;
}

void TrailRendererComponent::ApplyMaterial() {
	if (!model_) {
		return;
	}

	MaterialAssetData material = MaterialAsset::CreateDefault();
	if (!materialAssetId_.empty() || !materialPath_.empty()) {
		std::filesystem::path resolvedPath = GetAssetResolver().ResolveAssetPath(materialAssetId_, materialPath_);
		if (!resolvedPath.empty()) {
			std::string message;
			MaterialAssetData loaded{};
			if (MaterialAsset::Load(resolvedPath, loaded, message)) {
				material = loaded;
			}
		}
	}

	model_->SetShaderModel(static_cast<ShaderModel>(material.shaderModel));
	model_->SetBlendMode(static_cast<BlendMode>(material.blendMode));
	model_->SetDepthWrite(material.depthWrite);
	model_->SetColor(material.baseColor);
	model_->SetTexture(MaterialAsset::ResolveTextureIndex(material, MaterialTextureSlot::BaseColor));
	model_->SetEmissive(material.emissiveColor, material.emissiveIntensity, material.emissiveEnabled);
	model_->SetEmissiveBloom(material.bloomIntensity, material.bloomThreshold, material.bloomSoftKnee);
	// 帯は裏からも見えてよい(カメラを向く板なので基本は表を向くが、急カーブで裏返ることがある)。
	model_->SetDoubleSided(true);
	materialDirty_ = false;
}

void TrailRendererComponent::Update() {
	EnsureModel();
	if (materialDirty_) {
		ApplyMaterial();
	}

	UpdatePoints(Time::GetDeltaTime());
	BuildRibbon();
}

void TrailRendererComponent::UpdatePoints(float deltaTime) {
	// 寿命切れを捨てる。古い順に並んでいるので先頭から落ちる。
	for (TrailPoint& point : points_) {
		point.age += deltaTime;
	}
	points_.erase(
	    std::remove_if(points_.begin(), points_.end(), [this](const TrailPoint& point) { return point.age >= lifetime_; }),
	    points_.end());

	GameObject* owner = GetOwner();
	if (!owner || !emitting_) {
		return;
	}

	// 親子どこにぶら下がっていても正しい位置を得るため、自分と祖先のワールド行列を先に更新する
	// (Scene::UpdateWorldTransforms は全Updateの後に走るので、そのままでは1フレーム古い)。
	owner->UpdateWorldTransformSelfAndAncestors();
	Vector3 current = owner->GetTransform().GetWorldPosition();

	if (points_.empty()) {
		points_.push_back({current, 0.0f});
		return;
	}

	const Vector3& last = points_.back().position;
	Vector3 delta = {current.x - last.x, current.y - last.y, current.z - last.z};
	float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);

	// 瞬間移動(プールの弾を撃ち直した等)。前の場所から線が伸びないよう履歴ごと捨てる。
	if (teleportDistance_ > 0.0f && distance >= teleportDistance_) {
		points_.clear();
		points_.push_back({current, 0.0f});
		return;
	}

	if (distance < minDistance_) {
		return;
	}

	points_.push_back({current, 0.0f});
	// 上限を超えたら古い方から捨てる。
	while (static_cast<int>(points_.size()) > (std::max)(maxPoints_, 2)) {
		points_.erase(points_.begin());
	}
}

void TrailRendererComponent::BuildRibbon() {
	if (!model_) {
		return;
	}

	vertices_.clear();

	// 帯には最低2点が要る。1点以下なら何も描かない(頂点0で描画がスキップされる)。
	if (points_.size() < 2 || !camera_) {
		model_->UpdateDynamicVertices(vertices_);
		return;
	}

	const Vector3 eye = camera_->translation_;
	const size_t count = points_.size();

	// 各点の「左右へ開く向き」と「幅」を先に求める。
	// 横ベクトル = 進行方向 × 視線方向。これでカメラに対して常に正面を向く帯になる。
	std::vector<Vector3> sides(count);
	std::vector<float> halfWidths(count);
	std::vector<float> ratios(count);
	for (size_t i = 0; i < count; ++i) {
		const Vector3& position = points_[i].position;

		// 進行方向は前後の点から取る(端は片側だけ)。
		const Vector3& previous = points_[i > 0 ? i - 1 : i].position;
		const Vector3& next = points_[i + 1 < count ? i + 1 : i].position;
		Vector3 forward = SafeNormalize({next.x - previous.x, next.y - previous.y, next.z - previous.z}, {0.0f, 0.0f, 1.0f});

		Vector3 toEye = SafeNormalize({eye.x - position.x, eye.y - position.y, eye.z - position.z}, {0.0f, 1.0f, 0.0f});
		sides[i] = SafeNormalize(CrossProduct(forward, toEye), {1.0f, 0.0f, 0.0f});

		// 配列は古い順なので、末尾(i=count-1)が最も新しい=先頭側。
		float newness = (count > 1) ? static_cast<float>(i) / static_cast<float>(count - 1) : 1.0f;
		halfWidths[i] = (endWidth_ + (startWidth_ - endWidth_) * newness) * 0.5f;
		// UVのuは「先頭が0・末尾が1」。シェーダーはこれを見て末尾を薄くする。
		ratios[i] = 1.0f - newness;
	}

	// 隣り合う点のペアで四角形(三角形2つ)を張る。インデックスバッファは使わない構成に合わせる。
	for (size_t i = 0; i + 1 < count; ++i) {
		const Vector3& p0 = points_[i].position;
		const Vector3& p1 = points_[i + 1].position;

		Vector3 a0 = {p0.x + sides[i].x * halfWidths[i], p0.y + sides[i].y * halfWidths[i], p0.z + sides[i].z * halfWidths[i]};
		Vector3 b0 = {p0.x - sides[i].x * halfWidths[i], p0.y - sides[i].y * halfWidths[i], p0.z - sides[i].z * halfWidths[i]};
		Vector3 a1 = {p1.x + sides[i + 1].x * halfWidths[i + 1], p1.y + sides[i + 1].y * halfWidths[i + 1], p1.z + sides[i + 1].z * halfWidths[i + 1]};
		Vector3 b1 = {p1.x - sides[i + 1].x * halfWidths[i + 1], p1.y - sides[i + 1].y * halfWidths[i + 1], p1.z - sides[i + 1].z * halfWidths[i + 1]};

		// 法線はカメラ向き(ライティングはしない前提だが、値は入れておく)。
		Vector3 normal = SafeNormalize({eye.x - p0.x, eye.y - p0.y, eye.z - p0.z}, {0.0f, 1.0f, 0.0f});

		VertexData va0{.position = {a0.x, a0.y, a0.z, 1.0f}, .texcoord = {ratios[i], 0.0f}, .normal = normal};
		VertexData vb0{.position = {b0.x, b0.y, b0.z, 1.0f}, .texcoord = {ratios[i], 1.0f}, .normal = normal};
		VertexData va1{.position = {a1.x, a1.y, a1.z, 1.0f}, .texcoord = {ratios[i + 1], 0.0f}, .normal = normal};
		VertexData vb1{.position = {b1.x, b1.y, b1.z, 1.0f}, .texcoord = {ratios[i + 1], 1.0f}, .normal = normal};

		vertices_.push_back(va0);
		vertices_.push_back(va1);
		vertices_.push_back(vb0);

		vertices_.push_back(vb0);
		vertices_.push_back(va1);
		vertices_.push_back(vb1);
	}

	model_->UpdateDynamicVertices(vertices_);
}

void TrailRendererComponent::Draw() {
	if (!model_ || !camera_) {
		return;
	}

	// 頂点は既にワールド座標。親の移動で帯が引きずられないよう、描画は必ず単位行列で行う。
	if (!identityReady_) {
		identityTransform_.Initialize();
		identityReady_ = true;
	}
	identityTransform_.translation_ = {0.0f, 0.0f, 0.0f};
	identityTransform_.rotation_ = {0.0f, 0.0f, 0.0f};
	identityTransform_.scale_ = {1.0f, 1.0f, 1.0f};
	identityTransform_.UpdateMatrix(*camera_);

	model_->Draw(identityTransform_, *camera_);
}

void TrailRendererComponent::DrawInspector() {
#ifdef USE_IMGUI
	InspectorUI::DragFloat("Lifetime", &lifetime_, 0.01f, 0.0f, 30.0f);
	InspectorUI::DragFloat("Min Distance", &minDistance_, 0.01f, 0.0f, 10.0f);
	InspectorUI::DragFloat("Start Width", &startWidth_, 0.01f, 0.0f, 20.0f);
	InspectorUI::DragFloat("End Width", &endWidth_, 0.01f, 0.0f, 20.0f);

	int maxPoints = maxPoints_;
	if (InspectorUI::DragInt("Max Points", &maxPoints, 1.0f, 2, 512)) {
		maxPoints_ = maxPoints;
		// 確保数が変わるのでメッシュを作り直す。
		model_.reset();
	}

	InspectorUI::DragFloat("Teleport Distance", &teleportDistance_, 0.05f, 0.0f, 1000.0f);
	InspectorUI::Checkbox("Emitting", &emitting_);

	// Materialアセットはドラッグ&ドロップで差し替える(ModelRendererと同じ操作感)。
	void* droppedObject = nullptr;
	bool cleared = false;
	if (InspectorUI::ObjectField("Material", materialPath_.c_str(), &droppedObject, &cleared)) {
		if (cleared) {
			materialAssetId_.clear();
			materialPath_.clear();
			materialDirty_ = true;
		}
	}
	if (InspectorUI::Button("Reload Material")) {
		materialDirty_ = true;
	}
#endif // USE_IMGUI
}

void TrailRendererComponent::WriteJson(nlohmann::json& json) const {
	json["lifetime"] = lifetime_;
	json["minDistance"] = minDistance_;
	json["startWidth"] = startWidth_;
	json["endWidth"] = endWidth_;
	json["maxPoints"] = maxPoints_;
	json["teleportDistance"] = teleportDistance_;
	json["emitting"] = emitting_;
	json["materialAssetId"] = materialAssetId_;
	json["materialPath"] = materialPath_;
}

void TrailRendererComponent::ReadJson(const nlohmann::json& json) {
	lifetime_ = ReadFloat(json, "lifetime", lifetime_);
	minDistance_ = ReadFloat(json, "minDistance", minDistance_);
	startWidth_ = ReadFloat(json, "startWidth", startWidth_);
	endWidth_ = ReadFloat(json, "endWidth", endWidth_);
	if (json.contains("maxPoints") && json.at("maxPoints").is_number_integer()) {
		maxPoints_ = json.at("maxPoints").get<int>();
	}
	teleportDistance_ = ReadFloat(json, "teleportDistance", teleportDistance_);
	if (json.contains("emitting") && json.at("emitting").is_boolean()) {
		emitting_ = json.at("emitting").get<bool>();
	}
	if (json.contains("materialAssetId") && json.at("materialAssetId").is_string()) {
		materialAssetId_ = json.at("materialAssetId").get<std::string>();
	}
	if (json.contains("materialPath") && json.at("materialPath").is_string()) {
		materialPath_ = json.at("materialPath").get<std::string>();
	}
	materialDirty_ = true;
	model_.reset();
}

} // namespace KujataEngine
