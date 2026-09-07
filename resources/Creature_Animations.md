# 生物アニメーション

すべてその場で再生する1.6秒のループです。各生物フォルダの通常名の `.blend`、`.gltf`、`.bin` に保存し、同名の `.gif` で動きを確認できます。GLTFを移動・ZIPにまとめる際は、BINと参照テクスチャも一緒に含めてください。

| 生物 | フォルダ／ファイル名 | クリップ | 動き |
|---|---|---|---|
| テッポウウオ | Archerfish/Archerfish | Swim | 胴から尾へ伝わる左右の泳ぎ |
| サヨリ | Halfbeak/halfbeak | Swim | 長い頭を保った尾の左右の動き |
| コバンザメ | suckfish/suckfish | Swim | 胴と尾の緩やかな左右の動き |
| カジキ | marlin/marlin | Swim | 尾へ強くなる泳ぎ |
| イルカ | dolphin/dolphin | Swim | 尾の上下運動 |
| シャチ | orca/orca | Swim | 尾の上下運動 |
| サメ | shark/shark | Swim | 胴と尾の左右の動き |
| ハリセンボン | pufferfish/pufferfish | Swim | 左右のひれと尾の動き |
| カニ | crab/crab | Walk | 脚を交互に振り、はさみも軽く動かす |
| シャコ | mantis_shrimp/mantis_shrimp | Walk | 脚を順番に動かし、前脚も軽く動かす |
| クラゲ | jellyfish/jellyfish | Pulse | 傘の収縮と触手の揺れ |
| ヒトデ | Starfish/Starfish | Crawl | 腕を順番に小さく曲げる |
| ウニ | sea_urchin/sea_urchin | Idle | ごく小さな全体の揺れ |
| 貝 | shell/shell | OpenClose | 殻をゆっくり開閉 |

エビの既存の `Shrimp_Walk` と、ゲーム内のマグロの泳ぎ処理は変更していません。
テッポウウオの `Archerfish_textured.gltf` も同じアニメーション付きモデルを参照します。

今回はモデルデータへの追加です。ゲーム内のモデル差し替えや再生処理は変更していません。
検証結果は `creature_animation_checks.json` に保存しています。
