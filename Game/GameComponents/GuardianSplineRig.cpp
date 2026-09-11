#include "GuardianSplineRig.h"

#include "GuardianRigMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using KujataEngine::GameObject;
using KujataEngine::Quaternion;
using KujataEngine::Vector3;
using KujataEngine::WorldTransform;

namespace {

constexpr float kDegreeToRadian = std::numbers::pi_v<float> / 180.0f;

/// <summary>プレハブが用意するボーンの本数の上限。これを超える関節数は指定できない。</summary>
constexpr int kMaxBonesPerLeg = 8;

/// <summary>弧長合わせの二分探索の反復回数。ハンドル長の探索範囲[0,6]をこの回数で詰める。</summary>
constexpr int kArcLengthIterations = 14;

/// <summary>ハンドル長の探索上限。これ以上膨らませても実用的な形にならない。</summary>
constexpr float kMaxHandleScale = 6.0f;

GameObject* FindDescendant(GameObject* root, const std::string& name) {
	if (!root) {
		return nullptr;
	}
	if (root->GetName() == name) {
		return root;
	}
	for (GameObject* child : root->GetChildren()) {
		if (GameObject* found = FindDescendant(child, name)) {
			return found;
		}
	}
	return nullptr;
}

} // namespace

int GuardianSplineLeg::GetActiveJointCount() const {
	int available = static_cast<int>(boneObjects_.size());
	if (available <= 0) {
		return 0;
	}
	return std::clamp(jointCount_, 1, available);
}

void GuardianSplineLeg::RegisterSerializedFields(KujataEngine::SerializedFieldRegistry& registry) {
	KUJATA_REGISTER_STRING_NAMED_TIP(name_, "Name",
	    "脚を構成するGameObjectの名前の接頭辞。\n"
	    "\"<これ>_Hip\" と \"<これ>_Bone0\"〜\"_Bone7\"、その子の \"_Bone0Mesh\"〜 を階層から名前で探す。\n"
	    "リネームするとリグが脚を見失うので、階層側の名前と必ず揃えること。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(hipYawDeg_, "Hip Yaw (deg)", 0.5f, -180.0f, 180.0f,
	    "接合部の水平角。ボディ前方(+Z)が0で、右(+X)向きが正。\n"
	    "球体のどこから脚を生やすかを決める。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(hipPitchDeg_, "Hip Pitch (deg)", 0.5f, -90.0f, 90.0f,
	    "接合部の仰角。負にすると球の下寄りから脚が生える。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(hipRadius_, "Hip Radius", 0.01f, 0.0f, 20.0f,
	    "ボディ中心から接合部までの距離。\n"
	    "球メッシュの半径より小さくすると、脚が球体の内側から生えているように見える。");
	KUJATA_REGISTER_INT_NAMED_TIP(jointCount_, "Joint Count", 0.1f, 1, kMaxBonesPerLeg,
	    "実際に使う関節(ボーン)の数。階層に用意されている本数が上限。\n"
	    "余ったボーンは SetActive(false) で親ごと隠れ、当たり判定にも参加しない。\n"
	    "曲線方式なので、増やしても解が不安定になったりはしない。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(totalLength_, "Total Length", 0.01f, 0.1f, 60.0f,
	    "付け根から足先までのボーン長の合計。**そのまま脚の最大到達距離になる**。\n"
	    "GuardianGait の Detach / Urgent / Dangle の各 Ratio はこの値に対する割合。\n"
	    "変えたらしきい値の並び順を確認すること。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(baseThickness_, "Base Thickness", 0.005f, 0.01f, 5.0f,
	    "付け根側のボーンの太さ。見た目と当たり判定の両方に効く\n"
	    "(コライダーはMeshのスケールに追従するため)。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(tipThickness_, "Tip Thickness", 0.005f, 0.01f, 5.0f,
	    "足先側のボーンの太さ。Base より細くするとテーパーの効いた機械脚になる。");
	KUJATA_REGISTER_VECTOR3_NAMED_TIP(hipTangent_, "Hip Tangent (body local)", 0.01f, -10.0f, 10.0f,
	    "付け根から脚が出ていく向き(曲線の制御ハンドル)。ボディローカル空間なので球体が傾けば一緒に傾く。\n"
	    "**Y成分が「どの高さで曲がるか」を決める最重要のツマミ。**\n"
	    "  正 … 曲線が接合部より上へ膨らみ、高い位置で折れて脚が縦に立つ\n"
	    "  負 … 付け根から下外へ抜け、曲がりの頂点が接合部より低くなる\n"
	    "水平成分(外向き)を強くするほど脚が横へ張り出す。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(hipTangentScale_, "Hip Tangent Scale", 0.01f, 0.0f, 5.0f,
	    "付け根側ハンドルの長さ(付け根→足先の距離に対する倍率)。\n"
	    "**Foot Tangent Scale との比が「曲がる位置」を前後させる。**\n"
	    "  こちらを大きく … 膨らみが付け根寄りへ移る\n"
	    "  こちらを小さく … 膨らみが足先寄りへ移る\n"
	    "Match Arc Length がonのときは全長が合うよう自動で倍率がかかるので、比だけが効く。");
	KUJATA_REGISTER_VECTOR3_NAMED_TIP(footTangent_, "Foot Tangent (root local)", 0.01f, -10.0f, 10.0f,
	    "足先へ入っていく向き(曲線の制御ハンドル)。ルートローカル空間。\n"
	    "外向き成分を入れると、脚が足の位置より外側へ弓なりに張り出してから内へ戻る。\n"
	    "真上(0,1,0)だけにすると脚が垂直に降りてくる形になる。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(footTangentScale_, "Foot Tangent Scale", 0.01f, 0.0f, 5.0f,
	    "足先側ハンドルの長さ(付け根→足先の距離に対する倍率)。\n"
	    "Hip Tangent Scale との比で曲がる位置が決まる(上の説明を参照)。");
	KUJATA_REGISTER_BOOL_NAMED_TIP(matchArcLength_, "Match Arc Length",
	    "曲線の弧長が Total Length に一致するようハンドル長を二分探索する。\n"
	    "onにするとボーンの伸縮が数%に収まり目視では分からなくなる。\n"
	    "offにするとハンドル長がそのまま効くので、意図的に伸ばしたいときに使う。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(homeYawDeg_, "Home Yaw (deg)", 0.5f, -180.0f, 180.0f,
	    "足の定位置の水平角。ルート前方(+Z)が0。\n"
	    "**ボディではなくルート基準**なので、球体が回転しても足の定位置は動かない。\n"
	    "接合部の角度(Hip Yaw)とは独立に決められる。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(homeDistance_, "Home Distance", 0.01f, 0.0f, 20.0f,
	    "ルート中心から足の定位置までの水平距離。脚をどれだけ外へ張り出して立つか。");
	KUJATA_REGISTER_FLOAT_NAMED_TIP(curveWeight_, "Curve Weight", 0.01f, 0.0f, 1.0f,
	    "**曲線制御とプロシージャル歩行の切り替え。**\n"
	    "0 = GuardianGait の歩行に任せる / 1 = Curve Target へ完全追従。\n"
	    "この値自体がアニメーションチャンネルなので、攻撃クリップの中で\n"
	    "「踏み込みで0→1、着地後1→0」と打てば、その脚だけ歩行から切り離せる。\n"
	    "0.5を超えている間は歩行側が踏み出しを止める。");
	KUJATA_REGISTER_VECTOR3_NAMED_TIP(curveTarget_, "Curve Target (root local)", 0.01f, -100.0f, 100.0f,
	    "曲線制御時の足先の位置。**Guardianルート基準のローカル座標**。\n"
	    "ワールド座標ではないので、ボスの位置や向きが変わっても同じ軌道が再現される。");
}

GuardianSplineRig::GuardianSplineRig() {
	struct LegDefault {
		const char* name;
		float yawDeg;
	};
	const LegDefault defaults[kGuardianLegCount] = {
	    {"Leg0", -45.0f},
	    {"Leg1", 45.0f},
	    {"Leg2", 135.0f},
	    {"Leg3", -135.0f},
	};

	for (int index = 0; index < kGuardianLegCount; ++index) {
		GuardianSplineLeg* leg = GetLeg(index);
		if (!leg) {
			continue;
		}
		leg->name_ = defaults[index].name;
		leg->hipYawDeg_ = defaults[index].yawDeg;
		leg->homeYawDeg_ = defaults[index].yawDeg;

		float yaw = defaults[index].yawDeg * kDegreeToRadian;
		Vector3 outward = {std::sin(yaw), 0.0f, std::cos(yaw)};
		leg->curveTarget_ = outward * leg->homeDistance_;

		// 付け根のハンドルは「外向き、やや下向き」。
		// **上成分を入れると曲線が接合部より上へ膨らみ、曲がる位置が高くなる。**
		// 少し下げておくと脚は付け根から下外へ抜け、曲がりの頂点が接合部より低い位置に来る。
		leg->hipTangent_ = outward * 1.0f + Vector3{0.0f, -0.15f, 0.0f};

		// 足先へは「外側の上から」入る。両ハンドルとも外を向くので曲線は上ではなく外へ膨らみ、
		// 脚が足の位置より外側へ弓なりに張り出してから内へ戻る(ガーディアンの脚の形)。
		leg->footTangent_ = outward * 0.9f + Vector3{0.0f, 0.55f, 0.0f};
	}
}

GuardianSplineLeg* GuardianSplineRig::GetLeg(int index) {
	switch (index) {
	case 0:
		return &leg0_;
	case 1:
		return &leg1_;
	case 2:
		return &leg2_;
	case 3:
		return &leg3_;
	default:
		return nullptr;
	}
}

const GuardianSplineLeg* GuardianSplineRig::GetLeg(int index) const {
	return const_cast<GuardianSplineRig*>(this)->GetLeg(index);
}

void GuardianSplineRig::Initialize() { ResolveHierarchy(); }

void GuardianSplineRig::OnPlayStart() {
	ResolveHierarchy();

	for (int index = 0; index < kGuardianLegCount; ++index) {
		GuardianSplineLeg* leg = GetLeg(index);
		if (!leg) {
			continue;
		}
		leg->proceduralTarget_ = GetHomeWorld(index);
		leg->resolvedFoot_ = leg->proceduralTarget_;
	}
}

void GuardianSplineRig::ResolveHierarchy() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	bodyObject_ = FindDescendant(owner, bodyObjectName_);
	if (!bodyObject_) {
		bodyObject_ = owner;
	}

	for (int index = 0; index < kGuardianLegCount; ++index) {
		GuardianSplineLeg* leg = GetLeg(index);
		if (!leg) {
			continue;
		}

		leg->hipObject_ = FindDescendant(owner, leg->name_ + "_Hip");

		// Bone0から連番で、見つからなくなったところで打ち切る。
		leg->boneObjects_.clear();
		leg->boneMeshObjects_.clear();
		for (int boneIndex = 0; boneIndex < kMaxBonesPerLeg; ++boneIndex) {
			std::string boneName = leg->name_ + "_Bone" + std::to_string(boneIndex);
			GameObject* bone = FindDescendant(owner, boneName);
			if (!bone) {
				break;
			}
			leg->boneObjects_.push_back(bone);
			leg->boneMeshObjects_.push_back(FindDescendant(owner, boneName + "Mesh"));
		}
	}

	hierarchyResolved_ = true;
}

void GuardianSplineRig::Update() {
	if (!hierarchyResolved_) {
		ResolveHierarchy();
	}

	const int usedLegCount = GetLegCount();
	for (int index = 0; index < kGuardianLegSlotCount; ++index) {
		GuardianSplineLeg* leg = GetLeg(index);
		if (!leg) {
			continue;
		}

		// **使わない脚は接合部ごと隠す。**
		// 階層は4本ぶん用意したまま Leg Count を減らせるようにするため。
		// 隠さないと、解かれないままの脚がPrefabの初期姿勢で取り残されて見える。
		if (index >= usedLegCount) {
			if (leg->hipObject_ && leg->hipObject_->IsActive()) {
				leg->hipObject_->SetActive(false);
			}
			continue;
		}
		if (leg->hipObject_ && !leg->hipObject_->IsActive()) {
			leg->hipObject_->SetActive(true);
		}

		if (applyHipPlacement_) {
			ApplyHipPlacement(*leg);
		}
		ApplyJointVisibility(*leg);
		SolveLeg(*leg);
	}
}

void GuardianSplineRig::ApplyHipPlacement(GuardianSplineLeg& leg) {
	if (!leg.hipObject_) {
		return;
	}

	float yaw = leg.hipYawDeg_ * kDegreeToRadian;
	float pitch = leg.hipPitchDeg_ * kDegreeToRadian;
	float horizontal = std::cos(pitch) * leg.hipRadius_;

	WorldTransform& transform = leg.hipObject_->GetTransform();
	transform.translation_ = {horizontal * std::sin(yaw), std::sin(pitch) * leg.hipRadius_, horizontal * std::cos(yaw)};
	transform.rotation_ = {0.0f, 0.0f, 0.0f};
}

void GuardianSplineRig::ApplyJointVisibility(GuardianSplineLeg& leg) {
	int jointCount = leg.GetActiveJointCount();
	for (size_t boneIndex = 0; boneIndex < leg.boneObjects_.size(); ++boneIndex) {
		if (GameObject* bone = leg.boneObjects_[boneIndex]) {
			// ボーンは親子で連なっているので、境目の1本を消せば以降も丸ごと隠れる。
			bone->SetActive(static_cast<int>(boneIndex) < jointCount);
		}
	}
}

void GuardianSplineRig::SolveLeg(GuardianSplineLeg& leg) {
	GameObject* owner = GetOwner();
	if (!owner || !leg.hipObject_) {
		return;
	}

	int jointCount = leg.GetActiveJointCount();
	if (jointCount < 1) {
		return;
	}

	GuardianRigMath::WorldPose rootPose = GuardianRigMath::ComputeWorldPose(owner);
	GuardianRigMath::WorldPose hipPose = GuardianRigMath::ComputeWorldPose(leg.hipObject_);
	GuardianRigMath::WorldPose bodyPose = bodyObject_ ? GuardianRigMath::ComputeWorldPose(bodyObject_) : rootPose;

	// --- 曲線の両端。ここが曲線レイヤーとプロシージャルの合流点(2ボーン版と同じ) ---
	Vector3 curveWorld = rootPose.TransformPoint(leg.curveTarget_);
	float weight = std::clamp(leg.curveWeight_, 0.0f, 1.0f);
	Vector3 start = hipPose.position;
	Vector3 end = GuardianRigMath::Lerp3(leg.proceduralTarget_, curveWorld, weight);

	float span = KujataEngine::Length(end - start);
	if (span < 1.0e-4f) {
		span = 1.0e-4f;
	}

	// --- ハンドルの向き。付け根はボディ基準、足先はルート基準 ---
	Vector3 hipDirection = GuardianRigMath::SafeNormalize(bodyPose.rotation.RotateVector(leg.hipTangent_), {0.0f, 1.0f, 0.0f});
	Vector3 footDirection = GuardianRigMath::SafeNormalize(rootPose.rotation.RotateVector(leg.footTangent_), {0.0f, 1.0f, 0.0f});

	float chainScale = (hipPose.scale > 1.0e-6f) ? hipPose.scale : 1.0f;
	float targetLength = leg.totalLength_ * chainScale;

	GuardianRigMath::CurvePolyline curve{};

	// 直線モード: 制御点を接合部→目標の軸上に等間隔で置くと、3次ベジェは厳密に直線になる。
	// 弧長=弦長になるので弧長合わせも要らず、ボーンが一直線に等分される。
	if (leg.curveStraight_) {
		Vector3 axis = GuardianRigMath::SafeNormalize(end - start, {0.0f, 0.0f, 1.0f});
		Vector3 control1 = start + axis * (span / 3.0f);
		Vector3 control2 = end - axis * (span / 3.0f);
		GuardianRigMath::BuildBezierPolyline(start, control1, control2, end, curveSampleCount_, curve);
	} else {

	auto buildCurve = [&](float handleScale) {
		Vector3 control1 = start + hipDirection * (leg.hipTangentScale_ * handleScale * span);
		Vector3 control2 = end + footDirection * (leg.footTangentScale_ * handleScale * span);
		GuardianRigMath::BuildBezierPolyline(start, control1, control2, end, curveSampleCount_, curve);
	};

	// --- ハンドル長を二分探索して、曲線の弧長をボーン合計長へ揃える ---
	// 弧長はハンドル長に対して単調増加する(膨らむほど長くなる)ので二分探索が使える。
	// 目標が遠すぎる(span >= targetLength)場合はどう縮めても届かないので、
	// 直線に近い形のまま伸ばして「脚を突っ張った」見た目にする。
	float handleScale = 1.0f;
	if (leg.matchArcLength_ && span < targetLength) {
		float low = 0.0f;
		float high = kMaxHandleScale;
		for (int iteration = 0; iteration < kArcLengthIterations; ++iteration) {
			float middle = (low + high) * 0.5f;
			buildCurve(middle);
			if (curve.totalLength < targetLength) {
				low = middle;
			} else {
				high = middle;
			}
		}
		handleScale = (low + high) * 0.5f;
	}
	buildCurve(handleScale);

	} // 直線モードでない場合

	leg.resolvedFoot_ = end;
	leg.stretchRatio_ = curve.totalLength / (std::max)(targetLength, 1.0e-4f);

	// --- 曲線上に等弧長で関節を並べる ---
	Vector3 joints[kMaxBonesPerLeg + 1] = {};
	for (int jointIndex = 0; jointIndex <= jointCount; ++jointIndex) {
		float distance = curve.totalLength * static_cast<float>(jointIndex) / static_cast<float>(jointCount);
		joints[jointIndex] = curve.PointAtDistance(distance);
	}
	// 端点は誤差を残さず厳密に合わせる。
	joints[0] = start;
	joints[jointCount] = end;

	// --- 各ボーンを「次の関節を向く剛体」として置く ---
	// upベクトルは平行移動フレーム(前のボーンのupを次の向きへ投影)で運ぶ。
	// 毎ボーンで世界の上方向から作り直すと、脚が水平に近づいたときにねじれが跳ぶため。
	Vector3 previousDirection = GuardianRigMath::SafeNormalize(joints[1] - joints[0], {0.0f, 0.0f, 1.0f});
	Vector3 bodyUp = bodyPose.rotation.RotateVector({0.0f, 1.0f, 0.0f});
	Vector3 up = bodyUp - previousDirection * GuardianRigMath::Dot3(bodyUp, previousDirection);
	if (KujataEngine::Length(up) <= 1.0e-4f) {
		up = GuardianRigMath::AnyPerpendicular(previousDirection);
	} else {
		up = GuardianRigMath::SafeNormalize(up);
	}

	Quaternion previousWorldRotation = hipPose.rotation;
	float previousLength = 0.0f;

	for (int boneIndex = 0; boneIndex < jointCount; ++boneIndex) {
		GameObject* bone = leg.boneObjects_[boneIndex];
		if (!bone) {
			continue;
		}

		Vector3 segment = joints[boneIndex + 1] - joints[boneIndex];
		float segmentLength = KujataEngine::Length(segment);
		Vector3 direction = GuardianRigMath::SafeNormalize(segment, previousDirection);

		// upを新しい向きへ投影し直す(ねじれを最小に保つ)。
		Vector3 projectedUp = up - direction * GuardianRigMath::Dot3(up, direction);
		if (KujataEngine::Length(projectedUp) <= 1.0e-4f) {
			projectedUp = GuardianRigMath::AnyPerpendicular(direction);
		} else {
			projectedUp = GuardianRigMath::SafeNormalize(projectedUp);
		}
		up = projectedUp;

		Quaternion worldRotation = Quaternion::LookRotation(direction, up);

		WorldTransform& transform = bone->GetTransform();
		// 前のボーンの先端へ繋ぐ。ローカル長は親のスケールで割り戻す。
		transform.translation_ = {0.0f, 0.0f, previousLength / chainScale};
		transform.SetRotationFromQuaternion(previousWorldRotation.Inverse() * worldRotation);

		// --- モデルを曲線に合わせる。弦長と弧長のズレはここで吸収する ---
		if (boneIndex < static_cast<int>(leg.boneMeshObjects_.size())) {
			if (GameObject* mesh = leg.boneMeshObjects_[boneIndex]) {
				float taper = (jointCount > 1) ? static_cast<float>(boneIndex) / static_cast<float>(jointCount - 1) : 0.0f;
				float thickness = leg.baseThickness_ + (leg.tipThickness_ - leg.baseThickness_) * taper;
				float localLength = segmentLength / chainScale;

				WorldTransform& meshTransform = mesh->GetTransform();
				// 位置は常にボーンの中央。ボーンは+Zへ伸びるので、その半分だけ前に出す。
				meshTransform.translation_ = {0.0f, 0.0f, localLength * 0.5f};

				// **モデルの長手方向に合わせて伸ばし方を変える。**
				// ボーンは+Zへ伸びるが、モデルがY方向に長く作られていることは珍しくない。
				// その場合にモデルを書き出し直させるのは筋が悪いので、リグ側で吸収する。
				if (meshUpAxis_ == 1) {
					// Y-up: モデルの+Yをボーンの+Zへ倒す。X軸まわりに-90度。
					// 伸ばす軸もモデル自身のY(=倒す前の軸)なので、scaleのyへ長さを入れる。
					meshTransform.scale_ = {thickness, localLength, thickness};
					meshTransform.rotation_ = {-std::numbers::pi_v<float> * 0.5f, 0.0f, 0.0f};
				} else {
					// Z-up(既定): 素直にZへ伸ばす。Cubeなどの単純な部品はこちら。
					meshTransform.scale_ = {thickness, thickness, localLength};
					meshTransform.rotation_ = {0.0f, 0.0f, 0.0f};
				}
			}
		}

		previousWorldRotation = worldRotation;
		previousDirection = direction;
		previousLength = segmentLength;
	}
}

KujataEngine::Vector3 GuardianSplineRig::GetHipWorld(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	if (!leg || !leg->hipObject_) {
		return GuardianRigMath::ComputeWorldPose(GetOwner()).position;
	}
	return GuardianRigMath::ComputeWorldPose(leg->hipObject_).position;
}

float GuardianSplineRig::GetMaxReach(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	if (!leg) {
		return 0.0f;
	}

	// ボーンの見かけの長さは親のスケールぶん伸びるので、届く距離にも同じ倍率をかける。
	float chainScale = 1.0f;
	if (leg->hipObject_) {
		float scale = GuardianRigMath::ComputeWorldPose(leg->hipObject_).scale;
		if (scale > 1.0e-6f) {
			chainScale = scale;
		}
	}
	return leg->totalLength_ * chainScale;
}

KujataEngine::Vector3 GuardianSplineRig::GetHomeWorld(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	GuardianRigMath::WorldPose rootPose = GuardianRigMath::ComputeWorldPose(GetOwner());
	if (!leg) {
		return rootPose.position;
	}

	float yaw = leg->homeYawDeg_ * kDegreeToRadian;
	Vector3 local = {std::sin(yaw) * leg->homeDistance_, 0.0f, std::cos(yaw) * leg->homeDistance_};
	return rootPose.TransformPoint(local);
}

void GuardianSplineRig::SetProceduralTarget(int index, const KujataEngine::Vector3& worldPosition) {
	if (GuardianSplineLeg* leg = GetLeg(index)) {
		leg->proceduralTarget_ = worldPosition;
	}
}

KujataEngine::Vector3 GuardianSplineRig::GetProceduralTarget(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	return leg ? leg->proceduralTarget_ : Vector3{0.0f, 0.0f, 0.0f};
}

KujataEngine::Vector3 GuardianSplineRig::GetFootWorld(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	return leg ? leg->resolvedFoot_ : Vector3{0.0f, 0.0f, 0.0f};
}

bool GuardianSplineRig::IsCurveDriven(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	return leg ? std::clamp(leg->curveWeight_, 0.0f, 1.0f) > 0.5f : false;
}

void GuardianSplineRig::SetCurveWeight(int index, float weight) {
	if (GuardianSplineLeg* leg = GetLeg(index)) {
		leg->curveWeight_ = std::clamp(weight, 0.0f, 1.0f);
	}
}

float GuardianSplineRig::GetCurveWeight(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	return leg ? std::clamp(leg->curveWeight_, 0.0f, 1.0f) : 0.0f;
}

void GuardianSplineRig::SetCurveTargetLocal(int index, const KujataEngine::Vector3& rootLocalPosition) {
	if (GuardianSplineLeg* leg = GetLeg(index)) {
		leg->curveTarget_ = rootLocalPosition;
	}
}

KujataEngine::Vector3 GuardianSplineRig::GetCurveTargetLocal(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	return leg ? leg->curveTarget_ : Vector3{0.0f, 0.0f, 0.0f};
}

void GuardianSplineRig::SetCurveStraight(int index, bool straight) {
	if (GuardianSplineLeg* leg = GetLeg(index)) {
		leg->curveStraight_ = straight;
	}
}

bool GuardianSplineRig::IsCurveStraight(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	return leg ? leg->curveStraight_ : false;
}

KujataEngine::Vector3 GuardianSplineRig::GetHomeLocal(int index) const {
	const GuardianSplineLeg* leg = GetLeg(index);
	if (!leg) {
		return Vector3{0.0f, 0.0f, 0.0f};
	}
	float yaw = leg->homeYawDeg_ * kDegreeToRadian;
	return {std::sin(yaw) * leg->homeDistance_, 0.0f, std::cos(yaw) * leg->homeDistance_};
}

void GuardianSplineRig::RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) {
	registry.Add("ResolveHierarchy", [this]() { ResolveHierarchy(); });
}
