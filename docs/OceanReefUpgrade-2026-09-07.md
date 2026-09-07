# 浅瀬の見せ場・太陽の影・HDR・水面の実景反射

今回の変更は、[前回の品質評価](OceanQualityAssessment-2026-09-07.md)で挙げた1〜4を、既存のゲームで動作する形にまとめたものです。平らな床へ光の模様を増やす段階から、起伏・岩陰・植生・水面の映り込みがある一区域へ進めました。

Subnautica 2の提供映像は見た目の目標です。今回の方式が同作の内部実装と同じだと確認できたわけではありません。また、今回の追加だけで同等の完成度になったと評価するものでもありません。

## 1. 浅瀬の一区域と画角

新しい `ReefSceneRenderer` が、砂の盛り上がり、大小の岩、岩のアーチ、揺れる葉、サンゴを固定のワールド座標へ配置します。植生が集まる場所と空いた砂地を分け、手前から奥へ形が重なるようにしています。砂の色と凹凸の強さは既存の環境設定を共有します。

制作区域はおおむね **Z = -30〜210、X = -110〜110**。中央の **X = ±14** の範囲には背の高い景観を置かず、従来の進行経路を確保しています。新規メッシュは69,717頂点・105,532三角形で、砂・岩・葉・サンゴをまとめて描きます。

環境の初期設定では縦視野角を **55度** にします。共有 `Camera` の標準値は変えず、`UnderwaterEnvironment` が開始時の視野角を保存します。既存の `Underwater Environment` ウィンドウで `Wide Reef View (55 deg)` を切ると保存した画角へ戻り、環境終了時にも元の値を復元します。広角では敵やプレイヤーが画面上で小さく見えるため、ゲームとしての狙いやすさは通常プレイで確認する必要があります。

**この地形は装飾です。当たり判定は追加していません。** 中央以外へ泳ぐと岩や砂の起伏を通り抜けられます。正式なステージにする際は、描画地形と衝突形状の共有、敵の移動経路、カメラとの干渉を別途整える必要があります。区域外には従来の海底が続きます。

## 2. 岩陰で日光とコースティクスを遮る

新しい砂地形と岩から、**2048×2048の静的シャドウマップ**を作ります。平均水面で屈折した太陽方向を使い、海底で見える光の模様と影の方向を合わせています。

影の範囲はワールド座標で固定し、カメラを動かしても影の投影格子が泳がない構成です。地形の高さや太陽方向が変わった場合だけ描き直し、それ以外は前のマップを再利用します。

既存の深度から再構成した水中の受光点へ影を適用します。岩陰では直接光に相当する成分とコースティクスを弱め、環境光を残します。水中の光の積分にも同じマップを参照させ、岩に遮られた場所の光柱を弱めます。受光面には3×3のPCF、光の積分には4点の補間を使用し、マップの縁では効果を滑らかに消します。

影を落とすのは今回追加した静的な砂地形と岩です。従来の小岩、魚、プレイヤー、敵、揺れる葉は今回の影の投影元に含みません。これらの可視部分は共通の受光処理で影を受けられますが、自分自身の影は追加されません。受光処理は既存の色から直接光の比率を近似する方式で、材質ごとの照明成分を厳密に分離するG-bufferは導入していません。

## 3. HDRで合成し、最後に表示の明るさを整える

scene、preview、ポスト処理と発光の合成先を **`R16G16B16A16_FLOAT`** に統一しました。途中でRGBを0〜1に切り詰めていた加算合成とLight Shaftの出力も修正し、表示する直前まで強い光の値を保持します。

最終段で露出を適用し、強いハイライトを滑らかに圧縮します。初期値は **0 EV、圧縮開始値0.8**。最大RGB成分が0.8以下の色はそのまま維持し、従来の明るい砂と透明感が露出変更だけで暗くならないようにしました。RGB全体へ同じ比率を掛け、コースティクスの色や水の色相を保ちます。

DevelopmentのSceneTextureとPreviewは専用のsRGB表示バッファへ、ReleaseはBackBufferへ、同じ処理で一度だけ変換します。HUDは変換後の表示面へ描きます。Spriteと円形マスクは実際の描画先に合わせてHDR用／表示用PSOを選び、ImGuiも従来の表示空間を維持します。scene内部の2D描画は、従来どおりsceneのポスト処理対象です。

**ここでいうHDRは内部の光の計算です。HDRモニターへの出力ではありません。** 最終出力は従来と同じSDRです。自動露出やHDR10出力は追加していません。既存の `Post Effect` ウィンドウに `Scene exposure (EV)` と `Highlight shoulder` を追加しています。

1280×720での色バッファ増分は概算約42.2 MiBです。影マップと水面用の色・深度コピーは別に使用します。GPUの処理時間はまだ測定していません。

## 4. 水面に実際の岩やモデルを映す

水面の深度を書き込む直前に、そのフレームのsceneの色と深度を専用テクスチャへコピーします。水面シェーダーはこれらを参照し、反射・屈折した方向に画面内の形状を探します。描画中のRTVやDSVを直接サンプリングせず、コピー前後で元のリソース状態を戻します。

探索は初期値28ステップ、調整範囲12〜40ステップです。交差候補を追加で絞り込み、画面端、遠距離、厚みの合わない交差では信頼度を下げます。見つからなかった部分には空や従来の海底近似を混ぜます。スネルの窓、フレネル反射、全反射は従来の処理を引き継ぎます。水面から反射・屈折先までの水中経路の減衰と、その後のカメラから水面までの減衰を分けています。

この方式は**画面内の色・深度を使う反射と屈折**です。画面外の物体、別の物体の裏に隠れた面、極端に薄い形状は取得できません。視線を動かすと実景と近似の割合が変わるため、完全な鏡やレイトレーシングと同じ結果にはなりません。水面後に描かれる粒子もコピーに含みません。

さらに、コピー元はDepthFog内の共通受光処理より前です。**post方式の影・コースティクス・接地陰影は反射／屈折像に含まれません。** 直接見た岩陰より、水面内の像が明るく見える場合があります。今後ここを一致させるには、受光処理を水面より前の独立パスへ分けるなどの変更が必要です。

## 組み込み先とチームへの影響

`GameScene`、入力処理、ゲーム開始操作、ドッキング処理は変更していません。既存の環境呼び出しを使い、`UnderwaterEnvironment` 内で景観・影・水面コピーを呼び出します。追加したUI項目を操作しなくても通常どおり開始します。

| 担当範囲 | 主なファイル |
| --- | --- |
| 環境への組み込み、画角保存と復元、既存UI内の調整項目 | `Game/environment/UnderwaterEnvironment.h/.cpp` |
| 浅瀬の形状・植生・静的影の描画 | `Engine/Render/ReefSceneRenderer.h/.cpp`、`resources/shaders/ReefScene.*` |
| 砂の起伏・既存海底との素材の整合 | `resources/shaders/SandSurface.hlsli`、`resources/shaders/SeabedDetail.*` |
| 影の受光、コースティクスと水中光への遮蔽 | `Engine/Render/OceanShadowParameters.h`、`resources/shaders/OceanSunShadow.hlsli`、`resources/shaders/OceanReceiverLighting.hlsli`、`resources/shaders/DepthFog.PS.hlsl` |
| HDRの合成・最終表示変換 | `Engine/Render/SceneColorFormat.h`、`Engine/Render/RenderManager.h/.cpp`、`Engine/Render/OffscreenPass.h`、`resources/shaders/ToneMap.PS.hlsl`、`resources/shaders/AdditiveComposite.PS.hlsl`、`resources/shaders/LightShaft.PS.hlsl` |
| 描画先のformat記録と影パスからの復帰 | `Engine/Core/DirectXCommon.h/.cpp` |
| 既存描画のHDR対応 | `Engine/2D/SpriteCommon.h/.cpp`、Object3d・Primitive・Skinning・SkinnedModel・Skybox・ParticleのPSO、UnderwaterBackground／SeabedDetailのPSO |
| sceneコピーと水面の実景反射・屈折 | `Engine/Render/WaterSurfaceRenderer.h/.cpp`、`resources/shaders/WaterSceneOptics.hlsli`、`resources/shaders/WaterSurface.PS.hlsl` |
| Visual Studioへの登録 | `CG2_Setup.vcxproj`、`CG2_Setup.vcxproj.filters` |

描画順は、静的影の必要時更新 → 元のHDR色・深度描画先へ復帰 → 背景・環境・モデル → 水面用の色・深度コピー → 水面深度と水面色 → 粒子 → 水中受光・吸収・散乱などのポスト処理 → HDRレイヤー合成 → 最終表示変換 → HUD／ImGui、です。

影用と通常カメラ用の定数バッファは分けています。DepthFog定数は416 bytes、影の80 bytesはoffset336に配置します。ポスト処理側の影テクスチャはt3／root11、最終表示用の定数はb8／root12です。水面のt3は別のroot signatureに属するため衝突しません。

## 検証と比較画像

Development／Releaseのフルビルドと、対象シェーダーのDXCコンパイル（警告をエラー扱いする `-WX`）が成功しています。環境専用QAではD3D12 debug layerのエラーは0件でした。既存スキニング入力のSINT／uint型の組み合わせに関するID 245の警告12件は残っています。該当する宣言がHEADにも存在することを確認しており、「全警告なし」とはしていません。

新規メッシュの監査では、範囲外インデックス、非有限値、無効な法線、退化三角形、法線と逆向きの面はすべて0件でした。影に使用する三角形は46,148です。監査値は [reef_geometry_audit.txt](../generated/ocean-review/reef_geometry_audit.txt) にあります。

以下は**環境専用の固定カメラによるQA画像**です。キャラクターや通常のUIを含むゲーム動画、あるいは提供されたSubnautica 2動画との同条件比較ではありません。静止画だけではちらつき、反射の切り替わり、操作感、フレーム時間は評価できません。

| 実装前後の比較 | 画像 | 画角 |
| --- | --- | --- |
| 実装前に保存した海底 | [実装前](../generated/ocean-review/clear-water-seabed.png) | 縦約25.7831度 |
| 同じカメラ位置・同じ旧画角での実装後 | [実装後・旧画角](../generated/ocean-review/reef-original-fov.png) | 縦約25.7831度 |
| 同じ位置で広角にした最終景観 | [実装後・55度](../generated/ocean-review/reef-showcase.png) | 縦55度 |

実装前と旧画角の実装後はカメラ位置・画角を合わせていますが、コースティクスのアニメーション位相は異なります。画素単位の差分計測用の画像ではありません。55度の画像は画角の変更も含むため、形状や光だけの比較と区別してください。

| 比較する機能 | 有効 | 無効 |
| --- | --- | --- |
| 太陽の影 | [影あり](../generated/ocean-review/reef-shadow-on.png) | [影なし](../generated/ocean-review/reef-shadow-off.png) |
| 水上からの実景屈折 | [実景屈折あり](../generated/ocean-review/reef-refraction-on.png) | [近似表示](../generated/ocean-review/reef-refraction-off.png) |
| 水中からの実景反射 | [実景反射あり](../generated/ocean-review/reef-reflection-on.png) | [近似表示](../generated/ocean-review/reef-reflection-off.png) |

[浅瀬の全体像](../generated/ocean-review/reef-showcase.png)

反射の有効／無効を切り替えた画像では、画面上部の水面に映る岩の違いを確認しています。最終QAでもdebug layerはエラー0件・前述の既存警告12件でした。

`generated/ocean-review` は検証用の生成物です。画像を含めてチームへ共有する場合は、リポジトリの差分とは別にこれらのファイルも渡してください。

次の比較動画は、同じ経路・画角・画面解像度で「砂へ近づく → 岩陰を通る → アーチを見る → 水面を見上げる」を30〜60秒撮ると判断しやすくなります。まず通常プレイで広角の操作感と景観への接近を確認し、次にGPUの各パスを測定して反射の探索回数や解像度を調整するのがおすすめです。

## 参考にした原理と公開資料

- Unreal Engineの [Single Layer Water](https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine) は、sceneの色・深度を読む屈折と、SSR・反射キャプチャ・空の合成を説明しています。今回も実景と近似の組み合わせを使いますが、UEのパス構成やタイル分類を移植した実装ではありません。
- NVIDIAの [GPU Gems: Rendering Water Caustics](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics) は、集光の物理とリアルタイム向けの近似を扱います。既存のアニメーション投影と虹色の縁は見た目を調整する近似で、波から光子を追跡するスペクトルシミュレーションではありません。
- Rareの [The Technical Art of Sea of Thieves](https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf) は、FFTの波、散乱色、太陽反射、スネルの窓などを扱います。今回は波のFFT化より、地形・影・水面の景色をそろえることを優先しました。

これらは一般的な描画技術または各資料で扱われた作品の説明です。Subnautica 2が今回と同じコースティクス、影、反射、トーンマッピングを採用しているという根拠にはしていません。
