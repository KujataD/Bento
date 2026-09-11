#include "ShapeDraw.h"

#include <math/MathUtil.h>

namespace KujataEngine {
namespace ShapeUtil {

void DrawSplineParticles(InstancingModel* model, const std::vector<Vector3>& controlPoints, const Camera& camera) {
	const size_t particleCount = 20;

	model->ClearInstanceParticles();

	for (size_t i = 0; i < particleCount; ++i) {
		float t = 0.0f;

		if (particleCount > 1) {
			t = static_cast<float>(i) / static_cast<float>(particleCount - 1);
		}

		Vector3 pos = CatmullRomPosition(controlPoints, t);

		Vector3 scale = {0.1f, 0.1f, 0.1f};
		Vector3 rotation = {0.0f, 0.0f, 0.0f};

		TransformationMatrix mat = MakeBillboardMatrix(scale, rotation, pos, camera);

		Vector4 color = {1.0f, 0.0f, 0.0f, 0.5f};

		model->AddInstanceModel(mat, color);
	}

	model->UpdateBuffer();

	model->Draw();
}

} // namespace ShapeUtil
} // namespace KujataEngine
