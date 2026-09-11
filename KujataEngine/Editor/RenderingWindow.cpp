#include "RenderingWindow.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#include "../3d/Camera.h"
#include "../components/VolumeComponent.h"
#include "../postprocess/PostProcess.h"
#include "../postprocess/VolumeStack.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "../shadow/ShadowMap.h"
#include "EditorApplication.h"
#include "EditorSelection.h"
#endif // USE_IMGUI

namespace KujataEngine {

void RenderingWindow::Draw(bool* pOpen) {
#ifdef USE_IMGUI
	if (!ImGui::Begin("Rendering", pOpen)) {
		ImGui::End();
		return;
	}

	// このウィンドウは編集画面ではなく「今フレーム何が適用されたか」の確認用。
	// 値の編集はシーン上のVolumeComponent(Inspector)で行う。
	ImGui::TextDisabled("ポストエフェクトはシーン上の Volume で設定します");

	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		ImGui::TextUnformatted("No Scene.");
		ImGui::End();
		return;
	}

	// Sceneビューのカメラ位置で解決した結果を表示する(Local Volumeの効き具合が見えるように)。
	Camera* sceneCamera = scene->GetSceneViewCamera();
	Vector3 cameraPosition = sceneCamera ? sceneCamera->translation_ : Vector3{0.0f, 0.0f, 0.0f};
	VolumeResolveResult resolved = VolumeStack::Resolve(*scene, cameraPosition);

	ImGui::SeparatorText("Contributing Volumes");
	if (resolved.contributors.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "有効な Volume がありません(ポストエフェクト無し)");
		// HierarchyのCreate > Global Volumeと同じものを作る(生成内容を1箇所に寄せる)。
		if (ImGui::Button("Create Global Volume")) {
			EditorSelection::GetInstance()->SetSelectedGameObject(CreateGlobalVolumeObject(*scene));
		}
	} else {
		for (VolumeComponent* volume : resolved.contributors) {
			GameObject* owner = volume->GetOwner();
			const char* name = owner ? owner->GetName().c_str() : "(no owner)";
			float weight = VolumeStack::ComputeWeight(*volume, cameraPosition);
			ImGui::BulletText("%s  [%s]  priority=%.1f  weight=%.2f", name, volume->IsGlobal() ? "Global" : "Local", volume->GetPriority(), weight);
		}
	}

	ImGui::SeparatorText("Resolved Settings (read-only)");
	// 解決結果はコピーなので、ここでの編集はどこにも反映されない。誤操作防止のため常にDisabledで描く。
	VolumeProfileData preview = resolved.profile;
	for (const VolumeEffectDescriptor& descriptor : GetVolumeEffectDescriptors()) {
		ImGui::PushID(descriptor.name);
		bool overriding = preview.IsOverriding(descriptor.id);
		if (ImGui::CollapsingHeader(descriptor.name)) {
			ImGui::Indent();
			if (!overriding) {
				ImGui::TextDisabled("どの Volume も上書きしていません(既定値)");
			}
			ImGui::BeginDisabled(true);
			descriptor.drawInspector(preview);
			ImGui::EndDisabled();
			ImGui::Unindent();
		}
		ImGui::PopID();
	}

	// シャドウマップの中身を直接見られるようにしておく。
	// 影が出ないときの切り分け(ここが真っ白ならシャドウパスが描けていない)に使う。
	ShadowMap* shadowMap = ShadowMap::GetInstance();
	if (shadowMap->IsInitialized()) {
		ImGui::SeparatorText("Shadow");
		float bias = shadowMap->GetShadowBias();
		if (ImGui::DragFloat("Depth Bias", &bias, 0.0001f, 0.0f, 0.05f, "%.4f")) {
			shadowMap->SetShadowBias(bias);
		}
		if (ImGui::TreeNode("Shadow Map")) {
			// 深度をR32_FLOATとして表示する。手前ほど黒、遠いほど白。
			ImGui::Image(static_cast<ImTextureID>(shadowMap->GetSrvHandleGPU().ptr), ImVec2(256.0f, 256.0f));
			ImGui::TreePop();
		}
	}

	// フェードはVolumeとは独立した演出用のランタイム状態。動作確認用に現在値だけ表示する。
	PostProcess* postProcess = PostProcess::GetInstance();
	if (postProcess->GetFadeAmount() > 0.0f) {
		ImGui::Separator();
		ImGui::Text("Fade: %.2f", postProcess->GetFadeAmount());
	}

	ImGui::End();
#else
	(void)pOpen;
#endif // USE_IMGUI
}

} // namespace KujataEngine
