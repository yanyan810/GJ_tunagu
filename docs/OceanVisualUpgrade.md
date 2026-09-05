# ツナ具：海面・水中表現の改善

2026-09-05。提供動画（約9秒）と現行の DirectX 12 / C++ / HLSL 実装を確認し、水と光の表現を改善した。Subnautica 2 と同等の描画基盤を移植したものではなく、このエンジンの既存資産を使った最初の品質向上である。

2026-09-06更新：チームの既存の操作と編集箇所を優先し、追加したドッキング・確認用カメラ操作・停止起動モードを撤去した。海面・水中の描画改善と環境の調整項目は維持している。

## 調査で確認できた技術

| 公開資料 | 確認できたこと | 今回との関係 |
| --- | --- | --- |
| [Unknown Worlds 公式](https://www.unknownworlds.com/en/news/subnautica-free-weekend-sale) | Subnautica 2 は Unreal Engine 5 を採用 | 個別の水面アルゴリズム、Lumen / Nanite の採用範囲は今回確認した一次資料だけでは断定しない |
| [Rare：The Technical Art of Sea of Thieves, SIGGRAPH 2018](https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf) | FFT海面、視線・太陽・波頂に基づく散乱色、波頂泡、深度比較による接触泡、時間フィードバック、太陽反射、Snell's Window | 水中から見上げる光学表現と、複数の表現を組み合わせる考え方を参考にした。今回はFFTや接触泡は追加していない |
| [GPU Gems 第1章](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models) | 大きな頂点変形と細かな法線波を分けるリアルタイム水面表現 | 4本のGerstner波と既存の2枚の法線マップを組み合わせた |
| [Epic：Single Layer Water](https://dev.epicgames.com/documentation/unreal-engine/single-layer-water-shading-model?application_version=4.27) | 吸収・散乱と、シーン深度・色を使う反射／屈折の構成 | 今後の描画パス拡張の参考。Subnautica 2 がこの方式をそのまま使うという証拠ではない |

## 今回の変更

- **海面の形**：2三角形の平面を、128×128セル・32,768三角形の中心高密度グリッドへ変更。4本のGerstner波と解析法線を使用。遠くでは解像できない波を減衰する。既定の鉛直変位は合計最大約±0.5ゲーム単位。
- **水面の光学**：水中から空を見られるSnell窓と全反射を誘電体Fresnelで計算。窓内の天空キューブを屈折方向から参照し、水上には天空反射と太陽ハイライトを追加。屈折率1.333で臨界角は約48.61度。
- **水中の奥行き**：深度バッファと逆VP行列から世界座標を復元し、カメラから画素までの実距離のうち、水中を通る区間にRGB別の指数吸収を適用。深さに応じて光も減衰させる。
- **水中の光**：世界座標に固定した光の模様を既定8サンプルで積分する軽量な光柱を追加。既存の画面空間LightShaftは露出を0.35から0.14へ下げ、重ね過ぎを抑制。
- **海底**：既存の色むらに砂紋を加えた。画面上の微分で遠方の細かすぎる模様を減衰させる。既存のコースティクス・マリンスノー・遊泳時の粒子は利用を継続。
- **調整とUI**：HPバーをポストエフェクト後の2D描画へ移動。環境終了時に霧設定を復帰。停止中のカメラ移動でも水の行列を描画前に環境クラス内で更新する。ImGuiのドッキング・既存操作は元の状態を維持し、環境の調整項目だけを追加している。

主な編集箇所は `Engine/Render/WaterSurfaceRenderer.*`、`Engine/Render/RenderManager.*`、`resources/shaders/WaterSurface*`、`resources/shaders/DepthFog.PS.hlsl`、`resources/shaders/Object3d.PS.hlsl`、`Game/environment/UnderwaterEnvironment.*`、`Game/scene/Main/GameScene.*`。新しい共有シェーダーは `WaterSurfaceCommon.hlsli`。

## 確認方法

Visual Studioで `CG2_Setup.sln` の **Development / x64** をビルドし、作業ディレクトリをこのリポジトリのルートにして起動する。HLSLとテクスチャは `resources` から実行時に読み込む。

起動すると従来どおりゲームが進行する。追加の開始操作、停止解除、起動引数は不要。F5〜F7の海専用操作と確認用カメラボタンは削除済み。

| 操作 | 内容 |
| --- | --- |
| F4 / Pause Simulation | シミュレーションを停止・再開 |
| Tab | マウスのカメラ操作／自由カーソルを切り替え |
| F1 | デバッグカメラを切り替え |

上記はすべて従来からの操作。海の確認には既存のデバッグカメラを使い、水面を見上げる、海底へ近づく、遠方を見る方法で確認する。海のクラスはカメラの位置や操作モード、シーンの停止状態を変更しない。

今回の検証用実行ファイルは `generated/ocean-review/bin/CG2_Setup.exe`。この出力と `generated/ocean-review/build.py` はローカル検証用でGit管理対象外。

## チームでの変更範囲

`GameScene.cpp` はHPバーを既存の `DrawOverlay2D` コールバックへ分ける4行の追加、`GameScene.h` はその宣言1行の追加だけ。起動・Update・入力処理・DrawImGuiは変更前と同じ。HPバーを霧で変色させないため、この描画順の接続のみ残している。

カメラに依存する水面・水中霧・光の筋の更新は `UnderwaterEnvironment::DrawBackground()` 内で完結する。停止中もこの既存の描画呼び出しから同期するため、GameSceneに海専用の停止処理は不要。アニメーション時刻と粒子の発生は従来の `Update(dt)` だけで進め、描画準備では進めない。

`ImGuiManagaer.cpp` の今回の変更はすべて撤去し、チームの既存レイアウトへ復帰した。共有の起動設定にも新しい引数は追加していない。

## 調整の出発点

| 設定 | 初期値 | 効果 |
| --- | --- | --- |
| Ocean Swell Strength | 1.0（0〜2） | 大きなうねり。0で頂点変位を止める |
| Water Normal Strength | 0.20 | 細かい波。上げ過ぎると模様が強くなる |
| 水中吸収の開始距離 | 2.0 | 近傍の読みやすさ |
| Extinction Distance RGB | 40 / 95 / 130 | 小さい値ほどその色が早く失われる |
| Max Opacity | 1.0 | 遠景を背景水色へ十分に収束させる |
| Sunlit Water Strength | 0.10 | 水中の光柱。0で追加の光柱を止める |
| Light Shaft Exposure | 0.14 | 既存の画面空間の光の筋 |

`Underwater Environment` の `Depth / Sunlight Optics` で追加した世界座標の水中光学を切り替えられる。`Post Effect` の `Enable Depth Fog` は霧全体を切り替える。既存の `Underwater Medium Model` を切ると従来の一般的な霧に戻る。RGB吸収モデルでは `Density` は主な調整値ではなく、`Extinction Distance RGB` を使う。

## 検証結果

- 9月6日：操作・レイアウトの復帰後にDevelopment / x64を再ビルド。起動引数なし・追加の開始操作なしでゲームが進行することと、従来レイアウトへの復帰を実画面で確認。`GameScene` の差分はcpp 4行＋h 1行、`ImGuiManagaer.cpp` の差分はゼロ。`git diff --check` も通過。
- Development / x64 の全体ビルド成功（MSVC v145）。最終の初期表示修正まで再ビルド済み。
- 水面VS・水面PS・水面深度PS・DepthFog PS・Object3d PSを、実行時相当のDXC設定 `-Zi -Qembed_debug -Od -Zpr` と警告エラー化 `-WX` でコンパイル成功。
- CPU/HLSLの定数配置を検査：海面80バイト、水中霧256バイト。深度と色の海面描画は同一VS・同一索引を使用。
- 9月5日のビルドを実際に起動。水平視点、水面を見上げる視点、海底視点、停止中の視点切替、HPバーの位置・色を画面で確認。
- 9月5日の実画面の記録：`generated/ocean-review/after-horizon.png`、`after-surface.png`、`after-seabed.png`。撤去前の確認用レイアウトを含む。提供動画とは撮影位置や配置が異なるため、厳密に同じカメラによるA/Bではない。
- GPU時間の比較測定、低性能GPUでの検証、Release構成での実機検証は未実施。FPS向上は主張しない。

## 現段階の限界と次の改善

水中反射は水の色で近似しているため、魚や海底の鏡像は映らない。天空の屈折はあるが、船を含むシーン全体の屈折やSSR・平面反射は未導入。入水判定は既存の平均水位で行い、個々の波の高さを厳密には追跡しない。主な検証対象は水中であり、水上の空・水際の完全な演出は次段階となる。

追加した光柱には影マップによる遮蔽がないため、洞窟や大きな障害物の遮光には拡張が必要。砂紋は色の表現で、海底メッシュの凹凸や衝突判定を変えない。既存ターゲットはLDRであり、HDRライティングやトーンマッピングへ全面移行していない。

次にプレイヤーの驚きを作るには、まず岩礁・海藻・遠方の大きな地形で近景／中景／遠景を設計し、泳いだ先で景色が開ける場所を作ることを提案する。その上でシーン反射・屈折、船や物体との接触泡と航跡、影付きの光柱、HDRと控えめなBloomを順に検討する。これらは今後の提案で、今回の実装には含まない。
