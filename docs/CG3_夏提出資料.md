---
title: CG3_評価課題 KujataEngine


---

# 操作方法

## カメラ(Scene Window)
- Sceneウィンドウをクリックし、フォーカスしてから操作
- WASD : 前後左右移動
- QE : 上昇 / 下降
- 右クリックホールド + マウス : FPS 視点移動

## エディタ
- Hierarchy でオブジェクト名をクリック
- ▶ : ゲーム実行 / ■ : 実行停止
- ゲーム実行中は Game Window を確認していただきたい。

## ゲーム実行中(Game Window)
キーボード操作 / コントローラー操作
- WASD / Lスティック : 移動
- 上下左右キー / Rスティック : 視点移動
- Space / Aボタン : 回避...特に意味はない
- K / RT : 攻撃...特に意味はない

---

# 必須内容

## obj の表示 
- シーン上のobjを確認するには、Hierarchy上にある `Suzanne > Model` のように、空のGameObject + Modelの構造になっています。
- Model をクリック選択し、Inspectorの`ModelRendererComponent`で各種変更が可能です。

## Sprite の表示 
- Hierarchy の `Canvas` を選択。子供に配置されている Image を選択し、Inspector で調整することができる。

## ImGui で SRT を変える 
- Inspector 最上部 `Transform` の `Translate` / `Rotate` / `Scale` をドラッグ。Scene ビューのギズモでも動かせます。
- GameObjectには必ず `Transform` があるように設定されています。

---

# 加点要素
## 球の描画 
- Hierarchy上で、右クリック `Create > Sphere` で出すことができます。
- `ModelRendererComponent` のPrimitive で、`Cube` / `Sphere` / `Capsule` / `Plane` / `Model` に変更できます。 
- シーン上に設置されている5つの球はこの方法で生成しています。

## Lambertian Reflectance / Half Lambert
- シーン上に各Lighting方式のSphereが配置されています。
- Playerの子供にPoint Light があるため、ゲームを実行し、Playerを移動させて光の加減をみることができます。
- Directional Lightがシーン上に存在するので、Inspectorを調整することで、確認することもできます。

## Lighting 方式の変更 
- このエンジンではMaterial の設定に反射モデルが結び付けられています。
- 反射モデルを変更するには `Project > Materials` に各種Material が入っおり、Inspector上で調整可能です。
- 5種類のモンスターボールのMaterial は `Ball<反射モデル名>.material.json`という名前になっています。 

## UVTransform 
- Material の `UV Transform`や、`Canvas > Image` InspectorのImageの `UV Transform`などから変更可能です。

## 複数モデルの描画 
- SampleSceneには 10種以上のモデルが配置され、まとめて描画されています。

## Utah Teapot の描画 
- Hierarchy の `UtahTeapot > Model`。

## Stanford Bunny の描画 
- Hierarchy の `StanfordBunny > Model`。

## Suzanne の描画 
- Hierarchy の `Suzanne > Model`。
- Texture未設定のオブジェクトは1x1サイズの白いテクスチャを設定しています。

## MultiMesh 対応 
- Hierarchy の `MultiMesh > Model`。

## MultiMaterial 対応 
- Hierarchy の `MultiMaterial > Model` 内の`ModelRendererComponent` にある --- Sub Mesh UV Transform --- で調整可能です。

## Sound 
- Hierarchy の `AudioPlayer`で再生を管理しています。
- Inspector の  `Play (Preview)` でその場で再生できます (実行中にループ再生されています)。
- 鳴っている間に `Volume` をドラッグすると音量が変わります 。

## GamePad 
- XInputで実装。実行中のPlayer操作に用いている。
- 詳細はReadMe冒頭の`# 操作方法`に記載。

## その他 
### Emission + Bloom
- MaterialでEmissionが設定可能です。
- Emissionのチェックボックスを有効にすると設定がオープンします。
- Emission中のマテリアルはBloomがかかります。

### Emission調整項目
- Emissive Color : 発光する色	
- Emissive Intensity : 発光の強さ(倍率)
- Bloom Intensity : 滲みの強さ	
- Bloom Threshold : 滲み始める輝度	
- Bloom Soft Knee : 閾値の柔らかさ

### ワールドにUIを設置可能にする。
- Canvas の Render Mode を `World Space` にすると、画面ではなくワールド上にUIを配置できます。
- シーン上の各モデルに付いている名前ラベル(`Teapot` / `Bunny` / `Lambert` など)は、この方法で設置しています。
- Hierarchy の `UtahTeapot > World Canvas > Text` のように、モデルの子として置いているためモデルを動かすとラベルも追従します。
- 3Dオブジェクトと同じ空間に描画されるので、カメラで回り込むと角度が変わり、手前のモデルに隠れます。
- 新規作成は Hierarchy 上で、右クリック `Create > UI > Canvas (World Space)`。
- 画面に貼り付く通常のUIと使い方は同じで、Button を置けばゲーム実行中に押すこともできます。

## 追加のLighting方式( Phong / Blinn-Phong )
- シーン上にPhong Blinn-Phongを含めた5種類のLighting方式のSphereを置いています。