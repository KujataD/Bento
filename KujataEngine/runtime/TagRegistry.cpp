#include "TagRegistry.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <fstream>

namespace KujataEngine {

namespace {
constexpr const char* kUntagged = "Untagged";
} // namespace

TagRegistry::TagRegistry() : tags_{kUntagged, "Player", "Enemy", "MainCamera"} {}

TagRegistry& TagRegistry::GetInstance() {
	static TagRegistry instance;
	return instance;
}

const std::vector<std::string>& TagRegistry::GetTags() const { return tags_; }

bool TagRegistry::HasTag(const std::string& tag) const { return std::find(tags_.begin(), tags_.end(), tag) != tags_.end(); }

bool TagRegistry::AddTag(const std::string& tag) {
	if (tag.empty() || HasTag(tag)) {
		return false;
	}
	tags_.push_back(tag);
	Save();
	return true;
}

void TagRegistry::EnsureTag(const std::string& tag) {
	if (tag.empty() || HasTag(tag)) {
		return;
	}
	tags_.push_back(tag);
}

void TagRegistry::LoadFromProjectRoot(const std::filesystem::path& projectRoot) {
	storagePath_ = projectRoot / "ProjectSettings" / "Tags.json";
	hasStoragePath_ = true;

	try {
		if (!std::filesystem::exists(storagePath_)) {
			return;
		}
		std::ifstream ifs(storagePath_);
		if (!ifs) {
			return;
		}
		nlohmann::json json;
		ifs >> json;
		if (json.contains("tags") && json.at("tags").is_array()) {
			for (const nlohmann::json& entry : json.at("tags")) {
				if (entry.is_string()) {
					EnsureTag(entry.get<std::string>());
				}
			}
		}
	} catch (...) {
		// 読み込み失敗は既定タグのままとし、致命扱いにしない。
	}
}

void TagRegistry::Save() const {
	if (!hasStoragePath_) {
		return;
	}
	try {
		std::filesystem::create_directories(storagePath_.parent_path());
		nlohmann::json json;
		json["tags"] = tags_;
		std::ofstream ofs(storagePath_);
		if (ofs) {
			ofs << json.dump(2);
		}
	} catch (...) {
		// 保存失敗は致命扱いにしない。
	}
}

const char* TagRegistry::UntaggedTag() { return kUntagged; }

} // namespace KujataEngine
