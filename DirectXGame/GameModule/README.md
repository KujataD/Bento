# GameModule

ゲーム側コード(`../GameComponents/` の Component)を
**Hot Reload 用の DLL** (`GameModule.dll`) にまとめるプロジェクトです。

- ソースはこのフォルダと `../GameComponents/` にあります。新しい Component は `GameModule.cpp` の `RegisterGameComponents()` で登録します。
- ビルド出力は `bin/<構成>/GameModule.dll`(git 管理外)。エンジンの exe が起動時にここから読み込みます。
- エディタの `Reload DLL` を押すと、エンジンが MSBuild でこのプロジェクトを `../Temp/HotReload/` 配下へ世代別にビルドし直して差し替えます。
- ゲームは `DirectXGame/`、エンジンはその隣の `KujataEngine/`、外部ライブラリは `externals/` にある前提で参照しています(`GameModule.vcxproj` の `KujataEngineDir`)。
- ゲーム固有の設定(exe 名・ウィンドウタイトル等)の置き場所は [../README.md](../README.md) を参照。

注意: exe と DLL は同じ構成 (Debug/Release) でビルドしないと STL の ABI が食い違ってクラッシュします。
Component の追加・変更時は `.sln` 経由で exe と DLL を同時にビルドしてください。
