// エディタの CUI のコマンド: コンポーネントの型情報(schema.get)。
// 設計と一覧は .claude/editor-automation.md。共通の関数は EditorCommandUtil にある。
#include "EditorCommandUtil.h"
#include "../../scene/Component.h"
#include "../../scene/ComponentFactory.h"
#include "../../scene/SerializedFieldRegistry.h"
#include <algorithm>
#include <memory>
#include <string>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

// Inspector/JSON を手書きしているコンポーネント向け: 初期値のJSONから型だけ推測する(範囲・説明は出せない)。
json InferSchemaFromJson(const json& properties) {
	json fields = json::array();
	for (auto it = properties.begin(); it != properties.end(); ++it) {
		const json& value = it.value();
		std::string type = "unknown";
		if (value.is_boolean()) {
			type = "bool";
		} else if (value.is_number_integer()) {
			type = "int";
		} else if (value.is_number()) {
			type = "float";
		} else if (value.is_string()) {
			type = "string";
		} else if (value.is_object()) {
			type = "object";
		} else if (value.is_array()) {
			const bool allNumbers = std::all_of(value.begin(), value.end(), [](const json& element) { return element.is_number(); });
			if (allNumbers && value.size() == 3) {
				type = "vector3";
			} else if (allNumbers && value.size() == 4) {
				type = "vector4";
			} else {
				type = "array";
			}
		}
		fields.push_back({{"key", it.key()}, {"label", SerializedFieldRegistry::MakeDisplayName(it.key())}, {"type", type}});
	}
	return fields;
}

// 1つのコンポーネントの型情報。登録簿(KUJATA_SERIALIZED_FIELDS_BEGIN)から出せればそれを使う(source = "registry")。
// 出せなければ初期値から推測する(source = "inferred")。どちらも "default" に今の値を添える。
json DescribeComponentSchema(Component& component) {
	json fields;
	const bool fromRegistry = component.DescribeSerializedFields(fields);
	json values = json::object();
	component.WriteJson(values);
	if (!fromRegistry) {
		fields = InferSchemaFromJson(values);
	}
	for (json& field : fields) {
		const std::string key = field.value("key", std::string());
		if (values.contains(key)) {
			field["default"] = values[key];
		}
	}

	json schema;
	schema["type"] = component.GetTypeName();
	schema["source"] = fromRegistry ? "registry" : "inferred";
	if (!fromRegistry) {
		schema["note"] = "Inspector と JSON を手書きしているコンポーネントなので、型は値から推測したもの(範囲・説明はありません)。";
	}
	schema["fields"] = fields;
	return schema;
}

EditorCommandResult CommandSchemaGet(const EditorCommandArgs& args) {
	ComponentFactory& factory = ComponentFactory::GetInstance();
	if (args.Count() == 0) {
		// 一覧: 型名と、型情報がどこから来るか(registry = 正確 / inferred = 推測)。
		json types = json::array();
		for (const std::string& typeName : factory.GetRegisteredTypeNames()) {
			std::unique_ptr<Component> component = factory.Create(typeName);
			if (!component) {
				continue;
			}
			json schema = DescribeComponentSchema(*component);
			types.push_back({{"type", typeName}, {"source", schema["source"]}, {"fields", schema["fields"].size()}});
		}
		return EditorCommandResult::Success(types);
	}

	// 2つ目があれば「オブジェクト 型名」: シーンにある実物の型情報(default は今の値)。
	if (args.Count() > 1) {
		Scene* scene = GetScene();
		if (!scene) {
			return EditorCommandResult::Failure("シーンがありません。");
		}
		std::string error;
		GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
		if (!gameObject) {
			return EditorCommandResult::Failure(error);
		}
		Component* component = ResolveComponent(*gameObject, args.Get(1), error);
		if (!component) {
			return EditorCommandResult::Failure(error);
		}
		return EditorCommandResult::Success(DescribeComponentSchema(*component));
	}

	// 型名だけ: 新しく作ったときの型情報(default は初期値)。
	std::unique_ptr<Component> component = factory.Create(args.Get(0));
	if (!component) {
		return EditorCommandResult::Failure("登録されていないコンポーネントです: " + args.Get(0) + "(schema.get で一覧を表示できます)");
	}
	return EditorCommandResult::Success(DescribeComponentSchema(*component));
}

} // namespace

void RegisterSchemaCommands(EditorCommandRegistry& registry) {
	registry.Register("schema.get", "schema.get [型名] | schema.get <オブジェクト> <型名[#番号]>",
	                  "コンポーネントのフィールドの型・範囲・説明・初期値。引数なしで一覧(registry = 正確 / inferred = 値から推測)", CommandSchemaGet);
}

} // namespace KujataEngine
