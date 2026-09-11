#include "ParticleEmitter.h"
#include "../shapes/ShapeUtil.h"

namespace KujataEngine {

	using namespace ShapeUtil;

	void ParticleEmitter::Initialize(ParticleModel* model) { model_ = model; }

	void ParticleEmitter::Update(float deltaTime, const Camera& camera) {

		frequencyTime_ += deltaTime;        // 時刻を進める
		if (frequency_ <= frequencyTime_) { // 頻度より大きいなら発生
			Emit();                         // パーティクル生成処理
			frequencyTime_ -= frequency_;   // 余計に過ぎた時間も加味して頻度計算する
		}

		for (std::list<Particle>::iterator particleIterator = particles_.begin(); particleIterator != particles_.end();) {
			// ライフタイムを進める
			(*particleIterator).currentTime += deltaTime;

			// すでに死んでいたら、リストから削除
			if ((*particleIterator).IsDead()) {
				particleIterator = particles_.erase(particleIterator);
				continue;
			}

			// Field計算処理
			if (isActiveField_) {
				for (auto& field : accelerationFields_) {
					if (IsCollision(field.area, (*particleIterator).translation)) {
						(*particleIterator).velocity += field.acceleration * deltaTime;
					}
				}
			}

			// 透明度計算
			float alpha = 1.0f - ((*particleIterator).currentTime / (*particleIterator).lifeTime);
			(*particleIterator).color.w = alpha;
			(*particleIterator).translation += (*particleIterator).velocity * deltaTime;

			TransformationMatrix billboardMatrix = MakeBillboardMatrix((*particleIterator).scale, (*particleIterator).rotation, (*particleIterator).translation, camera);

			// InstancingModelに追加
			model_->AddInstanceParticle(billboardMatrix, (*particleIterator).color);
			++particleIterator;
		}
		model_->UpdateBuffer();
	}

	void ParticleEmitter::Draw() {
		if (!model_) {
			return;
		}

		model_->Draw();
	}

	void ParticleEmitter::Emit() {
		for (uint32_t count = 0; count < count_; ++count) {
			particles_.push_back(MakeParticle());
		}
	}

	Particle ParticleEmitter::MakeParticle() {
		Particle particle{};

		particle.scale = particleScale_;
		particle.lifeTime = Random::GetRandom(lifeTimeMinMax_.x, lifeTimeMinMax_.y);
		particle.rotation.z = Random::GetRandom(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
		particle.currentTime = 0.0f;
		switch (emitShape_) {
		case KujataEngine::ParticleEmitter::kEmitShapeBox: {
			Vector3 randomTranslation = { Random::GetRandom(-1.0f * scale_.x, 1.0f * scale_.x), Random::GetRandom(-1.0f * scale_.y, 1.0f * scale_.y), Random::GetRandom(-1.0f * scale_.z, 1.0f * scale_.z) };
			particle.translation = translation_ + randomTranslation;
			particle.velocity = { Random::GetRandom(-1.0f, 1.0f), Random::GetRandom(-1.0f, 1.0f), Random::GetRandom(-1.0f, 1.0f) };
			break;
		}
		case KujataEngine::ParticleEmitter::kEmitShapeModelEdge: {
			float randomScale = Random::GetRandom(0.1f, 1.0f);
			particle.scale = { randomScale * particleScale_.x,randomScale * particleScale_.y,randomScale * particleScale_.z };
			particle.color = { Random::GetRandom(0.0f, 0.8f), Random::GetRandom(0.0f, 0.8f), Random::GetRandom(0.8f, 1.0f), 1.0f };
			assert(sourceWorldTransform_);
			particle.translation = GetRandomPosModelEdge();
			particle.velocity = { Random::GetRandom(-0.3f, 0.3f), Random::GetRandom(-0.3f, 0.3f), Random::GetRandom(-0.3f, 0.3f) };
			break;
		}
		case KujataEngine::ParticleEmitter::kEmitSegmentEdge: {
			particle.color = { Random::GetRandom(0.8f, 1.0f), Random::GetRandom(0.0f, 0.8f), Random::GetRandom(0.0f, 0.8f), 1.0f };
			particle.translation = GetRandomPosSegmentsEdge();
			particle.scale = { Random::GetRandom(0.1f, 1.0f) * particleScale_.x, Random::GetRandom(0.1f, 1.0f) * particleScale_.y, Random::GetRandom(0.1f, 1.0f) * particleScale_.z };
			break;
		}
		default: {
			break;
		}
		}

		if (!canEmit_) {
			particle.lifeTime = 0.0f;
			canEmit_ = true;
		}
		return particle;
	}

	Vector3 ParticleEmitter::GetRandomPosModelEdge() {
		const auto& vertices = vertices_;

		if (vertices.size() < 3) {
			canEmit_ = false;
			return translation_;
		}

		// 三角形を一つ選ぶ
		uint32_t triangleIndex = Random::GetRandom(0, static_cast<int>(vertices.size() / 3 - 1)) * 3;

		// その頂点座標を取得
		Vector3 triangleVertices[3];

		for (int32_t i = 0; i < 3; i++) {
			triangleVertices[i] = { vertices[triangleIndex + i].position.x, vertices[triangleIndex + i].position.y, vertices[triangleIndex + i].position.z };
		}

		// 三角形の3辺から1つ選ぶ
		int edge = Random::GetRandom(0, 2);

		Vector3 a;
		Vector3 b;

		if (edge == 0) {
			a = triangleVertices[0];
			b = triangleVertices[1];
		}
		else if (edge == 1) {
			a = triangleVertices[1];
			b = triangleVertices[2];
		}
		else {
			a = triangleVertices[2];
			b = triangleVertices[0];
		}

		// ここが重要：tは1つだけ
		float t = Random::GetRandom(0.0f, 1.0f);
		Vector3 localPos = Lerp(a, b, t);

		// モデルのワールド行列から変換
		return Transform(localPos, sourceWorldTransform_->matWorld_);
	}

	Vector3 ParticleEmitter::GetRandomPosSegmentsEdge() {
		if (segments_.empty()) {
			canEmit_ = false;
			return translation_;
		}

		// 線分を一つ選ぶ
		uint32_t segmentIndex = Random::GetRandom(0, static_cast<int>(segments_.size() - 1));

		// その頂点座標を取得
		Vector3 a = segments_[segmentIndex].origin;
		Vector3 b = segments_[segmentIndex].diff + segments_[segmentIndex].origin;
		float length = Length(segments_[segmentIndex].diff);

		if (length <= 0.0f) {
			return a;
		}

		// 中点を持ち上げて、ベジェに使う。ちょっち足っぽくなるかね。
		Vector3 control = Lerp(a, b, 0.5f);
		control.y += length * segmentCurveHeightRate_;

		float t = Random::GetRandom(0.0f, 1.0f);
		return Bezier(a, control, b, t);
	}

} // namespace KujataEngine
