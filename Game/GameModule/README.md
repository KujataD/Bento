# GameModule

ゲーム側コード(`../GameComponents/` の Component)を
**Hot Reload 用の DLL** (`GameModule.dll`) にまとめるプロジェクトです。

- ソースはこのフォルダと `../GameComponents/` にあります。
- ビルド出力は `bin/<構成>/GameModule.dll`(git 管理外)。エンジンの exe が起動時にここから読み込みます。
- エディタの `Reload DLL` を押すと、エンジンが MSBuild でこのプロジェクトを `../Temp/HotReload/` 配下へ世代別にビルドし直して差し替えます。
- エンジンは `Game/` の隣の `DirectXGame/` にある前提で参照しています(`GameModule.vcxproj` の `KujataEngineDir`)。

注意: exe と DLL は同じ構成 (Debug/Release) でビルドしないと STL の ABI が食い違ってクラッシュします。
Component の追加・変更時は `.sln` 経由で exe と DLL を同時にビルドしてください。

## このゲーム固有の設定の置き場所

| 設定 | 場所 |
|---|---|
| exe 名 | `Game/Game.props` の `KujataExeName` |
| ウィンドウのタイトル・背景色 | `Game/Data/ProjectSettings/Project.json` |
| 起動シーン | `Game/Data/ProjectSettings/StartupScene.txt` |

背景色がほぼ黒なのは、画面奥を洞窟の闇に見せるため(空色のままだと、天井の無い通路から明るい「空」が覗いてしまう)。
