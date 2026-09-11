#include "AssetResolver.h"

#include "EngineContext.h"

namespace KujataEngine {

namespace {

// リゾルバ未設定時のフォールバック。Asset ID機構なしでパスを素通しする。
class FallbackAssetResolver : public IAssetResolver {
public:
	std::filesystem::path ResolveAssetPath(const std::string& assetId, const std::filesystem::path& fallbackPath) override {
		(void)assetId;
		return fallbackPath;
	}
	std::string GetOrCreateAssetId(const std::filesystem::path& assetPath) override {
		(void)assetPath;
		return {};
	}
	std::string MakeProjectRelativePath(const std::filesystem::path& assetPath) const override {
		return assetPath.generic_string();
	}
};

} // namespace

IAssetResolver& GetAssetResolver() {
	if (IAssetResolver* resolver = GetEngineContext().assetResolver) {
		return *resolver;
	}
	static FallbackAssetResolver fallback;
	return fallback;
}

void SetAssetResolver(IAssetResolver* resolver) { GetEngineContext().assetResolver = resolver; }

} // namespace KujataEngine
