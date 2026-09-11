#pragma once

#include "../runtime/KujataApi.h"
#include "InvokableMethod.h"
#include "SerializedFieldRegistry.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <iosfwd>
#include <string>
#include <vector>

namespace KujataEngine {

class ColliderComponent;
class GameObject;
class ModelRendererComponent;
class SerializedFieldRegistry;
struct AnimatableChannel;
struct Collision;

/// <summary>
/// GameObjectに追加するComponentの基底クラス
/// </summary>
class KUJATA_API Component {
public:
	virtual ~Component();

	virtual void Initialize() {}
	virtual void Update() {}
	virtual void Draw() {}

	/// <summary>
	/// Inspector表示用のComponent名を取得
	/// </summary>
	virtual const char* GetTypeName() const { return "Component"; }

	virtual void DrawInspector();

	/// <summary>
	/// Component固有情報をJSONへ書き出します。
	/// </summary>
	virtual void WriteJson(nlohmann::json& json) const;

	/// <summary>
	/// Component固有情報をJSONから読み込みます。
	/// </summary>
	virtual void ReadJson(const nlohmann::json& json);

	virtual void OnAfterReadJson() {}

	/// <summary>
	/// アニメーション可能なfloatチャンネルを列挙します。
	/// KUJATA_SERIALIZED_FIELDS使用Componentは自動対応。手書きComponentは個別にoverrideします。
	/// </summary>
	virtual void CollectAnimatableChannels(std::vector<AnimatableChannel>& channels) { (void)channels; }

	/// <summary>
	/// シリアライズ済みのGameObject参照(ObjectRef/ComponentRef)を、instanceIdから実ポインタへ解決します。
	/// Scene構築後(全GameObject生成後)に呼ばれます。参照を持たないComponentは何もしません。
	/// KUJATA_SERIALIZED_FIELDS_BEGIN を使うComponentは自動でoverrideされます。
	/// </summary>
	virtual void ResolveReferences(IObjectResolver& resolver) { (void)resolver; }

	/// <summary>
	/// このComponentが公開する「Inspectorから呼び出せるメソッド」を登録します。
	/// UnityのUnityEvent(Button.onClick等)のメソッド選択肢に相当します。
	/// 例: registry.Add("LoadBTSet", [this]() { LoadBTSet(); });
	/// </summary>
	virtual void RegisterInvokableMethods(InvokableMethodRegistry& registry) { (void)registry; }

	/// <summary>
	/// Component情報を共通形式のJSONとして書き出す
	/// </summary>
	void WriteJson(std::ostream& os, int indent) const;

	/// <summary>
	/// Editor上で削除可能かどうか
	/// </summary>
	virtual bool CanRemove() const { return true; }

	/// <summary>
	/// 同じ種類のComponentを複数追加できるかどうか
	/// </summary>
	virtual bool AllowMultiple() const { return true; }

	virtual void OnPlayStart() {}

	virtual void OnPlayStop() {}

	/// <summary>
	/// 通常Collider同士が接触開始した時に呼ばれます。
	/// </summary>
	virtual void OnCollisionEnter(const Collision& collision) { (void)collision; }

	/// <summary>
	/// 通常Collider同士が接触中の時に呼ばれます。
	/// </summary>
	virtual void OnCollisionStay(const Collision& collision) { (void)collision; }

	/// <summary>
	/// 通常Collider同士が接触終了した時に呼ばれます。
	/// </summary>
	virtual void OnCollisionExit(const Collision& collision) { (void)collision; }

	/// <summary>
	/// Trigger Colliderとの接触開始時に呼ばれます。
	/// </summary>
	virtual void OnTriggerEnter(ColliderComponent* other) { (void)other; }

	/// <summary>
	/// Trigger Colliderとの接触中に呼ばれます。
	/// </summary>
	virtual void OnTriggerStay(ColliderComponent* other) { (void)other; }

	/// <summary>
	/// Trigger Colliderとの接触終了時に呼ばれます。
	/// </summary>
	virtual void OnTriggerExit(ColliderComponent* other) { (void)other; }

	void SetOwner(GameObject* owner) { owner_ = owner; }

	GameObject* GetOwner() const { return owner_; }

	// --- Unity風のComponent検索(定義は循環include回避のためGameObject.h側) ---

	/// <summary>同じGameObjectのT型Componentを返します(無ければnullptr)。</summary>
	template <class T>
	T* GetComponent() const;

	/// <summary>自身と子孫からT型Componentを探します(深さ優先、無ければnullptr)。</summary>
	template <class T>
	T* GetComponentInChildren() const;

	/// <summary>自身と祖先からT型Componentを探します(無ければnullptr)。</summary>
	template <class T>
	T* GetComponentInParent() const;

	void SetEnabled(bool enabled) { enabled_ = enabled; }

	bool IsEnabled() const { return enabled_; }

	virtual bool IsTransformComponent() const { return false; }

	/// <summary>
	/// ColliderComponentなら自身を返す高速ダウンキャスト(dynamic_castの代替)。
	/// Scene::UpdateCollisions等、毎フレーム全Componentを型判定する箇所でRTTIコストを避けるために使う。
	/// </summary>
	virtual ColliderComponent* AsColliderComponent() { return nullptr; }

	/// <summary>
	/// ModelRendererComponentなら自身を返す高速ダウンキャスト(dynamic_castの代替)。
	/// Scene::RenderShadowPass等、毎フレーム全Componentを型判定する箇所でRTTIコストを避けるために使う。
	/// </summary>
	virtual ModelRendererComponent* AsModelRendererComponent() { return nullptr; }

protected:
	GameObject* owner_ = nullptr;
	bool enabled_ = true;
};

} // namespace KujataEngine
