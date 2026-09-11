#pragma once

#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"
#include <string>

namespace KujataEngine {

class GameObject;
class WorldTransform;

/// <summary>
/// キャラクター移動の標準関数群。
/// 「カメラ基準の移動」「移動方向へのスムーズなY旋回」をゲーム側Componentから1行で使えるようにする。
/// </summary>
namespace MovementUtil {

/// <summary>
/// 入力ベクトル(x,z)をカメラのyaw基準の移動方向へ変換します(カメラの前=+z入力)。
/// y成分は無視され、結果は水平方向のベクトルです。
/// </summary>
KUJATA_API Vector3 ToCameraRelative(const Vector3& input, float cameraYaw);

/// <summary>
/// gameObjectが属するSceneから、名前で指定したカメラObjectのyaw[rad]を取得します。
/// 見つからない場合は0を返します。
/// </summary>
KUJATA_API float GetCameraYaw(const GameObject& gameObject, const std::string& cameraName = "Main Camera");

/// <summary>
/// directionの水平方向へ、turnSpeed[rad/s]で制限しながらY回転をスムーズに向けます(Quaternionの最短経路)。
/// directionがほぼゼロの場合は何もしません。
/// </summary>
KUJATA_API void FaceDirection(WorldTransform& transform, const Vector3& direction, float turnSpeedRadPerSec, float deltaTime);

/// <summary>自身の向き(回転)基準の前方(+Z)ベクトルをワールド空間で返します。</summary>
KUJATA_API Vector3 GetForward(const GameObject& gameObject);

/// <summary>自身の向き(回転)基準の右(+X)ベクトルをワールド空間で返します。</summary>
KUJATA_API Vector3 GetRight(const GameObject& gameObject);

/// <summary>自身の向き(回転)基準の上(+Y)ベクトルをワールド空間で返します。</summary>
KUJATA_API Vector3 GetUp(const GameObject& gameObject);

/// <summary>
/// 自身の向き基準のローカル移動量(x=右/-左, y=上/-下, z=前/-後)をワールドへ変換して移動します。
/// 例: MoveLocal(obj, {0,0,1.5f}) = 向いている方向へ1.5前進。
/// </summary>
KUJATA_API void MoveLocal(GameObject& gameObject, const Vector3& localOffset);

/// <summary>
/// カメラ基準で移動しつつ、移動方向へスムーズに旋回するまとめ関数。
/// - input: ローカル入力(x=横, z=前後)。スティックの倒し具合(長さ0〜1)は移動速度に反映されます。
/// - moveSpeed: 移動速度[unit/s] / turnSpeed: 旋回速度[rad/s]
/// 入力がほぼゼロの場合は何もしません。
/// </summary>
KUJATA_API void MoveAndFace(GameObject& gameObject, const Vector3& input, float moveSpeed, float turnSpeed, float deltaTime, const std::string& cameraName = "Main Camera");

} // namespace MovementUtil

} // namespace KujataEngine
