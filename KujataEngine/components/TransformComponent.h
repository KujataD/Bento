#pragma once

#include "../runtime/KujataApi.h"
#include "../3d/WorldTransform.h"
#include "../scene/Component.h"

namespace KujataEngine {

/// <summary>
/// GameObjectが必ず持つTransform Component
/// </summary>
class KUJATA_API TransformComponent : public Component {
public:
	const char* GetTypeName() const override { return "Transform"; }
	void Initialize() override;
	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;
	void CollectAnimatableChannels(std::vector<AnimatableChannel>& channels) override;
	bool CanRemove() const override { return false; }
	bool AllowMultiple() const override { return false; }
	bool IsTransformComponent() const override { return true; }

	WorldTransform& GetTransform();

	const WorldTransform& GetTransform() const;

private:
	WorldTransform fallbackTransform_;
	bool initialized_ = false;
};

} // namespace KujataEngine
