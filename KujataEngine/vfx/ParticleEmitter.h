#pragma once
#include "../runtime/KujataApi.h"
#include "Particle.h"
#include "ParticleField.h"
#include "ParticleModel.h"
#include <3d/Model.h>
#include <math/MathUtil.h>

#include <list>
#include <vector>

namespace KujataEngine {

class KUJATA_API ParticleEmitter {
public:
	// 生成図形タイプ
	enum EmitShape {
		kEmitShapeBox,       // !< 従来の直方体からランダム生成する。
		kEmitShapeModelEdge, // !< モデルの辺からランダム生成する。（SetSourceModel必要）
		kEmitSegmentEdge,    // !< 線分からランダム生成する。（SetSourceModel必要）
	};

public:
	void Initialize(ParticleModel* model);

	void Update(float deltaTime, const Camera& camera);
	void Draw();

	void Emit();

	// --- set ---
	void AddField(AccelerationField& field) { accelerationFields_.push_back(field); }
	void SetIsActiveField(bool isActive) { isActiveField_ = isActive; }
	void SetSourceVertices(const std::vector<VertexData>& vertices, WorldTransform* sourceWorldTransform) {
		vertices_ = vertices;
		sourceWorldTransform_ = sourceWorldTransform;
	}
	void SetSourceSegments(const std::vector<Segment>& segments) { segments_ = segments; }

	// --- get ---

	Vector3 GetRandomPosModelEdge();
	Vector3 GetRandomPosSegmentsEdge();

private:
	Particle MakeParticle();

public:
	Vector3 translation_;
	Vector3 rotation_;
	Vector3 scale_ = {1.0f, 1.0f, 1.0f};

	uint32_t count_ = 3;         // !< 発生数
	float frequency_ = 0.5f;     // !< 発生頻度
	float frequencyTime_ = 0.0f; // !< 頻度用時刻

	EmitShape emitShape_ = kEmitShapeBox;

	Vector3 particleScale_ = {1.0f, 1.0f, 1.0f};
	Vector2 lifeTimeMinMax_ = {1.0f, 3.0f};

	// セグメント発生用：ベジェ曲線の制御点を値だけ上へ持ち上げる。
	float segmentCurveHeightRate_ = 1.0f;

private:
	// 生成可能か
	bool canEmit_ = true;

	// モデルを使って生成する場合に必要
	std::vector<VertexData> vertices_;

	// 線分を使って生成する場合に必要
	std::vector<Segment> segments_;

	WorldTransform* sourceWorldTransform_ = nullptr;

	// パーティクルモデル
	ParticleModel* model_ = nullptr;

	std::list<Particle> particles_;

	// フィールド
	std::list<AccelerationField> accelerationFields_;
	bool isActiveField_ = true;
};

} // namespace KujataEngine
