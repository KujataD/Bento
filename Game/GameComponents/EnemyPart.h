#pragma once

#include "IEnemy.h"
#include <KujataEngine.h>
#include <string>

class EnemyHealth;

/// <summary>
/// **敵の「部位」を、独立して狙える的にするための印。** 体力は持たない。
///
/// 第2形態のガーディアンは、浮いている目と地を這う脚が別々の場所にいるのに、
/// **体力は1本を共有する**(HPバーも1本)。ダメージ側はもともと
/// `GetComponentInParent&lt;EnemyHealth&gt;()` で親へ遡るので、当てる分にはこれで足りている。
///
/// 足りないのは**狙い**のほうで、`IEnemy` を持つのがルートだけだと
/// 「目を狙う」「脚を狙う」の区別ができない(Z注目もAIの索敵も1点しか見つけられない)。
/// そこで部位ごとにこの印を付け、狙い点だけを部位が返すようにする。
///
/// 生死は親のEnemyHealthに従うので、本体が倒れれば部位も自動的に狙えなくなる。
/// </summary>
class EnemyPart : public IEnemy {
public:
	const char* GetTypeName() const override { return "EnemyPart"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;

	/// <summary>親のEnemyHealthが生きていれば狙える。</summary>
	bool IsTargetable() const override;

	/// <summary>この部位の位置(+オフセット)。**部位ごとに違う場所を返すのが役目。**</summary>
	KujataEngine::Vector3 GetLockOnPoint() const override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(lockOnOffset_, "Lock On Offset", 0.05f, -50.0f, 50.0f,
		    "狙い点のオフセット(ワールド軸)。レティクル・AIの狙い・弾の仰角がここを向く。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(targetable_, "Targetable",
		    "この部位を狙えるか。OFFにすると的から外れる(倒した脚を的に残さない、など)。");
	}

	// 狙い点のオフセット。
	KUJATA_FIELD_VECTOR3(lockOnOffset_, (KujataEngine::Vector3{0.0f, 0.0f, 0.0f}));
	// 狙えるか。
	KUJATA_FIELD_BOOL(targetable_, true);

	// 親のHP(OnPlayStartで解決)。持ち主が倒れたら部位も狙えなくなる。
	EnemyHealth* health_ = nullptr;
};
