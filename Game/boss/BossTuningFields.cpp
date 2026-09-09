#include "BossTuningFields.h"
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace {
#define FIELD(K,G,ID,L,M,LO,HI,STEP,H,U) {ID,G,L,H,U,LO,HI,STEP,BossTuningField::Kind::K, \
    [](const BossCombatSettings& s){return static_cast<float>(s.M);}, \
    [](BossCombatSettings& s,float v){s.M=static_cast<std::remove_reference_t<decltype(s.M)>>(v);}}
#define F(G,ID,L,M,LO,HI,STEP,H,U) FIELD(Number,G,ID,L,M,LO,HI,STEP,H,U)
#define I(G,ID,L,M,LO,HI,H) FIELD(Integer,G,ID,L,M,LO,HI,1,H,"個")
#define B(G,ID,L,M,H) FIELD(Boolean,G,ID,L,M,0,1,1,H,"")
const BossTuningField fields[]{
    F("全体","battle.cooldown","攻撃間の休み",cooldown,.1f,10,.05f,"攻撃終了後、次の攻撃までの待ち時間。小さいほど忙しくなります。","秒"),
    F("全体","battle.windup","機雷・波の予告時間",windup,.3f,5,.05f,"機雷散布・ショックウェーブの発動前の予告。","秒"),
    F("全体","battle.playerRadius","プレイヤー判定半径",playerRadius,.1f,5,.05f,"ボス攻撃に対する共通の球半径。魚モデルや地形衝突には影響しません。","m"),
    F("ビーム","beam.width","見た目の幅",ping.beamWidth,.2f,12,.1f,"ビームの全幅。判定はこの半分に判定倍率とプレイヤー半径を反映します。","m"),
    F("ビーム","beam.height","見た目の高さ",ping.beamHeight,.2f,12,.1f,"ビームの全高。見た目と判定の基準を共有します。","m"),
    F("ビーム","beam.hitScale","判定の倍率",battle.beamHitRadiusScale,.1f,1,.05f,"ビーム自身の楕円断面を縮めます。小さいほどかすり避けが容易。プレイヤー半径は別に加算。","倍"),
    F("ビーム","beam.tracking","照準が追う時間",ping.trackingTime,.2f,5,.05f,"各射撃の前に照準が追尾する時間。この後は狙う位置を固定します。","秒"),
    F("ビーム","beam.charge","照準固定から発射まで",ping.chargeTime,.2f,5,.05f,"固定された照準から逃げる猶予。長いほど回避しやすくなります。","秒"),
    F("ビーム","beam.duration","発射の持続時間",ping.beamDuration,.05f,2,.01f,"1発が残り、当たり判定を持つ時間。","秒"),
    F("ビーム","beam.interval","次の照準までの休み",ping.beamInterval,.05f,3,.05f,"3連射の各発の後、次の追尾へ入るまでの間隔。","秒"),
    F("ビーム","beam.returnTime","砲台が船へ戻る時間",battle.beamReturnDuration,0,2,.05f,"3連射を終えた砲台を船の取り付け位置へ滑らかに収納します。0で即時収納。攻撃判定の時間は変えません。","秒"),
    F("ビーム","beam.leadScale","移動先の先読み倍率",battle.beamLeadScale,0,1,.05f,"0で現在位置、1で従来と同じ先読み。急な方向転換で避ける余地を調整します。","倍"),
    F("ビーム","beam.maxLead","先読み距離の上限",battle.beamMaxLeadDistance,0,45,.5f,"高速移動時でも照準がこの距離以上先へ飛ばないようにします。","m"),
    F("ビーム","beam.damage","ダメージ",ping.damage,0,200,1,"ビーム命中時のHPダメージ。","HP"),
    F("ビーム","beam.slow","減速の強さ",ping.moveSpeedDamage,0,100,1,"既存の被弾減速へ渡す値。すべての攻撃に共通する回復処理を使用します。","設定値"),
    I("機雷","mine.count","1回に投げる数",mineCount,1,24,"機雷の同時プール上限は24個です。残存機雷がある場合は空きの範囲で発射します。"),
    F("機雷","mine.interval","投げる間隔",mineInterval,.08f,2,.02f,"機雷を順番に発射する間隔。","秒"),
    F("機雷","mine.travelTime","到着の目安時間",battle.mineTravelTime,.5f,5,.1f,"水の抵抗を補正して投げる速さを決めます。吸引や近距離の速度制限で実際の時間は変わります。","秒"),
    F("機雷","mine.leadTime","移動先の先読み",battle.mineAimLeadTime,0,2.5f,.05f,"各機雷の発射前に移動先を予測する時間。発射後は追尾しません。","秒"),
    F("機雷","mine.triggerRadius","近接起爆の半径",battle.mineTriggerRadius,.5f,8,.1f,"プレイヤーが近づくと導火線を開始する半径。プレイヤー判定半径を別に加算します。","m"),
    B("機雷","mine.linkShell","泡・探知・爆風を連動",battle.mineLinkShell,"ON: 泡の公称半径を探知半径に合わせ、爆風は下の倍率で小さくします。OFF: 従来の小さな泡と独立した爆風半径。"),
    F("機雷","mine.blastRatio","泡に対する爆風の倍率",battle.mineBlastRatio,.5f,.95f,.05f,"連動ON時に使用。0.85なら泡半径の85%。プレイヤー判定半径は探知・被弾の両方へ別に加算します。","倍"),
    F("機雷","mine.fuse","接近後の爆発猶予",mineFuse,.2f,3,.05f,"起爆予告から爆発まで。短すぎると対処しづらくなるため0.2秒以上。","秒"),
    F("機雷","mine.blastRadius","非連動時の爆風半径",mine.explosionRadius,1,15,.25f,"泡との連動がOFFのときだけ使用。ONなら探知半径×爆風倍率が見た目とダメージの共通半径です。","m"),
    F("機雷","mine.lifetime","自然起爆まで",mineLifetime,2,20,.5f,"接近しなかった機雷も、この時間で導火線を開始します。","秒"),
    F("機雷","mine.chainFuse","誘爆の猶予",mineChainFuse,.1f,3,.05f,"他の機雷の爆風を受けたときの導火線の時間。","秒"),
    F("機雷","mine.drag","水中抵抗",mine.drag,0,3,.05f,"飛翔速度を減衰させる強さ。弾道が変わるため到着時間と合わせて調整します。","1/秒"),
    B("機雷","mine.terrain.enabled","地面・岩にぶつかる",mine.terrain.enabled,"本編の海底・砂丘・岩に接触すると跳ね返ります。OFFで以前の地形を通過する動作。爆発の判定半径とは別です。"),
    F("機雷","mine.terrain.radius","地形に当たる半径",mine.terrain.radius,.6f,4,.05f,"機雷中心から地形までの距離。初期値1.6m。泡の探知半径とは独立して調整でき、大きいほど地面から離れて跳ねます。","m"),
    F("機雷","mine.terrain.restitution","跳ね返りの強さ",mine.terrain.restitution,0,1,.05f,"0で跳ねずに止まり、0.5で接触面へ向かう速度の半分を反射します。1でも水中抵抗による減速は残ります。","割合"),
    F("機雷","mine.terrain.friction","接触時の滑りにくさ",mine.terrain.friction,0,1,.05f,"0で横方向の速度を保ち、1で接触面に沿う速度を止めます。空中の水中抵抗とは別です。","割合"),
    F("機雷","mine.terrain.minimumBounceSpeed","小さな跳ね返りを止める速度",mine.terrain.minimumBounceSpeed,0,5,.1f,"接触面へ向かう速度がこれ未満なら跳ねません。接地中の細かな震えを抑えます。","m/秒"),
    F("機雷","mine.damage","爆発ダメージ",mine.damage,0,200,1,"単体散布・スクリューに使われる機雷の共通ダメージ。","HP"),
    F("アンカー","anchor.radius","回転半径の下限",anchor.radius,5,70,.5f,"実際の半径は予告開始時の船とプレイヤーの水平距離にも合わせます。","m"),
    F("アンカー","anchor.radiusBias","狙う半径の追加幅",battle.anchorRadiusBias,-5,10,.25f,"プレイヤーまでの水平距離に加える量。半径の下限と上限85mも適用されます。","m"),
    B("アンカー","anchor.retarget","1周ごとに少し狙い直す",battle.anchorRetarget.enabled,"回転が1周するたびに、その時点のプレイヤーまでの距離と高さへ少し寄せます。発射方向や回転角は飛ばしません。"),
    F("アンカー","anchor.retargetRadius","1周で半径を変える上限",battle.anchorRetarget.maxRadiusShiftPerTurn,0,5,.1f,"1回の狙い直しで半径を変える最大距離。0なら半径を固定します。","m"),
    F("アンカー","anchor.retargetHeight","1周で高さを変える上限",battle.anchorRetarget.maxHeightShiftPerTurn,0,3,.1f,"1回の狙い直しで回転面を上下へ動かす最大距離。上下の揺れは別に残ります。","m"),
    F("アンカー","anchor.retargetTime","狙い直しにかける時間",battle.anchorRetarget.transitionTime,.4f,3,.1f,"半径・高さを滑らかに移す時間。短すぎる急な追尾を避けるため0.4秒以上。","秒"),
    F("アンカー","anchor.preview","予告リングの時間",anchor.warningRing.previewTime,.3f,5,.05f,"アンカーの動作前に回転範囲を予告する時間。","秒"),
    F("アンカー","anchor.drop","降ろす時間",anchor.dropDuration,.2f,3,.05f,"船から回転する高さへアンカーを降ろす時間。","秒"),
    F("アンカー","anchor.wait","降下後の猶予",anchor.waitTime,.1f,3,.05f,"回転を始める前の待ち時間。","秒"),
    F("アンカー","anchor.startSpeed","回転の初速",anchor.startAngularSpeed,.1f,6,.1f,"回転開始時の角速度。半径が大きいと同じ角速度でも速く動きます。","rad/秒"),
    F("アンカー","anchor.acceleration","回転の加速",anchor.angularAcceleration,0,5,.1f,"回転速度が毎秒増える量。","rad/秒²"),
    F("アンカー","anchor.maxSpeed","回転の最高速度",anchor.maxAngularSpeed,.1f,8,.1f,"回転の初速以上に設定してください。","rad/秒"),
    F("アンカー","anchor.vertical","上下に動く幅",anchor.verticalAmplitude,0,8,.1f,"予告時の深さを中心に上下へ動く振幅。少し増やすと高さを変える必要が生まれます。","m"),
    F("アンカー","anchor.frequency","上下運動の速さ",anchor.verticalFrequency,0,5,.1f,"上下に揺れる周期の速さ。","設定値"),
    F("アンカー","anchor.duration","回転を続ける時間",anchor.duration,1,12,.25f,"長いほど同じ場所への滞在が危険になります。","秒"),
    F("アンカー","anchor.scale","モデルと判定の倍率",anchor.overallScale,.1f,1.2f,.02f,"実モデルと5つの当たり判定を同時に拡大します。光だけのcollisionRadiusは調整対象外です。","倍"),
    F("アンカー","anchor.damage","命中ダメージ",anchor.damage,0,200,1,"アンカー本体に当たったときのダメージ。鎖自体には判定はありません。","HP"),
    F("スクリュー","screw.preview","吸引前の予告",screw.previewTime,.4f,4,.05f,"吸引範囲が現れてから力が働くまでの猶予。","秒"),
    F("スクリュー","screw.power","吸い込む力",screw.suctionPower,0,60,.5f,"吸引の加速度。泳ぐ力と外力の抵抗もあるため、この値が移動速度になるわけではありません。","設定値"),
    F("スクリュー","screw.glow","吸引の発光",battle.screwGlow,.25f,2.5f,.05f,"吸引演出の発光の強さ。判定や吸い込む力には影響しません。","倍"),
    F("スクリュー","screw.flowSpeed","内向きの光の速さ",battle.screwFlowSpeed,.25f,2.5f,.05f,"帯・粒が吸引中心へ流れる速さ。ゲームの吸引速度とは別です。","倍"),
    F("スクリュー","screw.bandWidth","吸引の帯の太さ",battle.screwBandWidth,.5f,2,.05f,"水の帯を見つけやすくする幅の倍率。","倍"),
    F("スクリュー","screw.dangerMix","吸引の危険色の割合",battle.screwDangerMix,0,1,.05f,"0で水色寄り、1で暖色の危険表示を強調します。","割合"),
    F("スクリュー","screw.duration","吸引を続ける時間",screw.suctionDuration,1,8,.1f,"吸引開始から機雷を保持する段階へ移るまでの時間。","秒"),
    F("スクリュー","screw.hold","放出前の保持",screw.holdTime,.2f,3,.05f,"吸引後、下へ放出する前の間。","秒"),
    F("スクリュー","screw.release","プレイヤーを下へ放出する力",screw.releasePower,0,70,1,"プレイヤーだけに加える下向きの速度。範囲内の位置で弱まり、既存の外力上限60m/秒と水中抵抗もかかります。機雷とは別設定です。","m/秒"),
    F("スクリュー","screw.mineRelease","機雷を下へ放出する力",battle.screwMineReleasePower,0,120,1,"機雷だけに加える下向きの速度。範囲内の位置で弱まります。同じ値でもプレイヤーとは抵抗や速度制限が異なります。","m/秒"),
    F("スクリュー","screw.mineReleaseSpread","機雷を横へ散らす力",screw.releaseSpread,0,120,1,"放出時に機雷を中心から横へ広げる速度。0なら下向きだけ。プレイヤーには加えません。","m/秒"),
    F("スクリュー","screw.mineReleaseFuse","機雷の放出後の爆発猶予",battle.screwMineReleaseFuse,.2f,8,.1f,"放出から爆発まで。初期値1.1秒。跳ねる様子を長く残すなら延長します。すでに起爆予告中の機雷の猶予は延長しません。","秒"),
    I("スクリュー","screw.mineCount","追加する機雷の数",battle.screwPayloadCount,0,12,"予告後に船から飛ばす機雷の数。0なら吸引だけを確認できます。"),
    F("スクリュー","screw.mineDelay","機雷を投げるまで",battle.screwPayloadDelay,.3f,4,.05f,"攻撃開始から投げるまで。吸引予告より短くしても予告終了まで待ちます。吸引終了より前に設定してください。","秒"),
    F("スクリュー","screw.mineSpread","機雷の散布半径",battle.screwPayloadSpawnRadius,2,16,.25f,"吸引中心の周囲に機雷を投げる半径。目の前に突然生成せず船から飛ばします。","m"),
    F("スクリュー","screw.outerX","吸引範囲の横半幅",screw.outerRangeHalfSize.x,2,50,.5f,"船のローカルX方向。全幅はこの2倍。中間範囲以上にします。","m"),
    F("スクリュー","screw.outerY","吸引範囲の縦半幅",screw.outerRangeHalfSize.y,2,40,.5f,"船のローカルY方向。深さ方向の逃げやすさに関係します。","m"),
    F("スクリュー","screw.outerZ","吸引範囲の奥行半幅",screw.outerRangeHalfSize.z,2,50,.5f,"船のローカルZ方向。全長はこの2倍。","m"),
    F("スクリュー","screw.middleX","中間範囲の横半幅",screw.middleRangeHalfSize.x,1,40,.5f,"内側以上、外側以下に設定します。","m"),
    F("スクリュー","screw.middleY","中間範囲の縦半幅",screw.middleRangeHalfSize.y,1,30,.5f,"内側以上、外側以下に設定します。","m"),
    F("スクリュー","screw.middleZ","中間範囲の奥行半幅",screw.middleRangeHalfSize.z,1,40,.5f,"内側以上、外側以下に設定します。","m"),
    F("スクリュー","screw.innerX","内側範囲の横半幅",screw.innerRangeHalfSize.x,.5f,30,.25f,"中心付近の強い吸引範囲。中間範囲以下に設定します。","m"),
    F("スクリュー","screw.innerY","内側範囲の縦半幅",screw.innerRangeHalfSize.y,.5f,25,.25f,"中心付近の強い吸引範囲。中間範囲以下に設定します。","m"),
    F("スクリュー","screw.innerZ","内側範囲の奥行半幅",screw.innerRangeHalfSize.z,.5f,30,.25f,"中心付近の強い吸引範囲。中間範囲以下に設定します。","m"),
    F("ショックウェーブ","wave.startRadius","開始半径",wave.radiusStart,.1f,10,.1f,"予告した中心から波が広がり始める半径。","m"),
    F("ショックウェーブ","wave.maxRadius","最大半径",wave.radiusMax,5,90,.5f,"発生源から波が届く最大距離。船側から出す場合は船までの距離も考慮してください。","m"),
    F("ショックウェーブ","wave.speed","波の広がる速さ",wave.expansionSpeed,2,40,.5f,"半径が1秒に増える量。速いほど水平に泳いで逃げにくくなります。","m/秒"),
    F("ショックウェーブ","wave.duration","波の持続時間",wave.duration,.5f,8,.1f,"最大半径まで到達できる時間を確保してください。最大半径到達後に岩が発生します。","秒"),
    B("ショックウェーブ","wave.playerDepth","泳いでいる深さに出す",battle.waveAtTargetDepth,"ON: 予告開始時のプレイヤーの深さに水平の波。上下に避けられます。OFF: 従来の海底の波。"),
    F("ショックウェーブ","wave.halfHeight","波の判定の上下幅",battle.waveHalfHeight,.2f,4,.1f,"波の中心面から上下それぞれの当たり幅。プレイヤー半径を別に加算します。","m"),
    F("ショックウェーブ","wave.damage","波のダメージ",waveDamage,0,100,1,"波本体のダメージ。地面から出る岩のダメージとは別です。","HP"),
    I("ショックウェーブ","wave.count","1回の連射数",battle.waveVolley.count,1,3,"各波に独立した予告を付けます。岩の発生は最後の波だけなので岩の数は増えません。"),
    F("ショックウェーブ","wave.interval","連射の間隔",battle.waveVolley.interval,1,5,.1f,"各波の予告を始める間隔。予告時間以上に設定します。短いと複数の波が重なります。","秒"),
    F("ショックウェーブ","wave.speedJitter","速度のランダム幅",battle.waveVolley.speedJitter,0,.2f,.01f,"0.08で基準速度の±8%。発射時に決めた速度は途中で変えません。遅い波は寿命も補正します。","割合"),
    B("ショックウェーブ","wave.originAtBoss","船側から波を出す",battle.waveVolley.originAtBoss,"ON: 各予告開始時の船の水平位置から発射。OFF: 従来のプレイヤー位置が中心。高さは別設定です。"),
    F("ショックウェーブ","wave.thickness","波の見た目の半幅",battle.waveThickness,.15f,1.5f,.05f,"水の帯の太さ。ダメージの上下幅とは別です。危険色の芯は実際の半径に沿います。","m"),
    F("ショックウェーブ","wave.intensity","波の発光",battle.waveIntensity,.25f,3,.05f,"水色の帯・暖色の縁・明るい芯の発光強度。","倍"),
    F("ショックウェーブ","wave.dangerMix","波の危険色の割合",battle.waveDangerMix,0,1,.05f,"0で水色寄り、1で暖色の危険表示を強調します。","割合"),
    I("地面の岩","rock.count","出現する岩の数",wave.rock.spawnCount,0,48,"波が最大半径に達すると出る岩の総数。0なら波単体の確認ができます。"),
    F("地面の岩","rock.interval","岩が出る間隔",wave.rock.spawnInterval,.05f,.5f,.01f,"岩を順番に出す間隔。","秒"),
    F("地面の岩","rock.radiusMin","出現距離の最小値",wave.rock.spawnRadiusMin,0,30,.5f,"予告中心からの水平距離。中央を狙う岩は例外として近くに出ます。","m"),
    F("地面の岩","rock.radiusMax","出現距離の最大値",wave.rock.spawnRadiusMax,1,60,.5f,"最小値以上に設定します。現在は予告時の位置が中心で、発射後の追尾はありません。","m"),
    F("地面の岩","rock.scaleMin","岩の大きさ・最小",wave.rock.scaleMin,.1f,4,.05f,"不規則な石と鉱脈の見た目、および従来の球近似の当たり判定に使う大きさ。","倍"),
    F("地面の岩","rock.scaleMax","岩の大きさ・最大",wave.rock.scaleMax,.1f,4,.05f,"最小値以上に設定します。","倍"),
    F("地面の岩","rock.launchMin","上昇の初速・下限",wave.rock.launchPowerMin,0,80,1,"泳いでいる深さへ届くよう本編では必要な初速まで補正されます。これは下限です。","m/秒"),
    F("地面の岩","rock.launchMax","上昇の初速・上限",wave.rock.launchPowerMax,0,85,1,"本編での深さ補正でさらに大きくなる場合があります。下限以上に設定します。","m/秒"),
    F("地面の岩","rock.horizontal","横に飛ぶ速さ",wave.rock.horizontalPower,0,20,.5f,"中央の一部の岩を除き、外側へ広がる速度。","m/秒"),
    F("地面の岩","rock.lifetime","残る時間",wave.rock.lifetime,1,8,.25f,"出現後に消えるまで。長いと同時に残る岩が増えます。","秒"),
    F("地面の岩","rock.damage","岩のダメージ",wave.rock.damage,0,100,1,"岩1個が命中した際のダメージ。","HP"),
    B("泳ぎ","presentation.swim.enabled","速度演出を使う",swim.enabled,"泳いだ実際の移動速度に応じた画面演出。OFFで完全に無効。カメラを回すだけでは発生しません。"),
    B("泳ぎ","presentation.swim.blurEnabled","周辺のブラー",swim.blurEnabled,"画面の周辺だけに薄い放射状のブラーを加えます。中央と画面上のUIは鮮明に保ちます。"),
    B("泳ぎ","presentation.swim.streaksEnabled","周辺の水の流れ",swim.streaksEnabled,"速度に応じて画面の端に細い流れを加えます。ブラーと別々に切り替えできます。"),
    F("泳ぎ","presentation.swim.startSpeed","演出を始める速度",swim.startSpeed,0,40,.5f,"この速度以下では演出を出しません。実際の位置の変化から求めた速さです。","m/秒"),
    F("泳ぎ","presentation.swim.fullSpeed","演出が最大になる速度",swim.fullSpeed,1,80,.5f,"開始速度より大きくしてください。この速度を超えても演出の強さは増えません。","m/秒"),
    F("泳ぎ","presentation.swim.maxBlur","ブラーの最大幅",swim.maxBlur,0,.04f,.001f,"大きいほど周辺が流れます。初期値0.018。酔いやすい場合は小さくするかOFFにしてください。","UV"),
    F("泳ぎ","presentation.swim.streakOpacity","流れの明るさ",swim.streakOpacity,0,.2f,.005f,"画面の端を流れる線の明るさ。敵の予告より目立たない程度がおすすめです。","強さ"),
    F("泳ぎ","presentation.swim.clearRadius","中央を鮮明に保つ範囲",swim.clearRadius,.25f,.8f,.01f,"画面の縦半分を1とした中心からの半径。大きいほど照準や周囲の景色を読みやすくなります。","比率"),
    F("泳ぎ","presentation.swim.response","演出がなじむ時間",swim.response,.05f,1,.05f,"加速・減速への追従を滑らかにします。大きいほどゆっくり変わります。","秒"),
};
#undef F
#undef I
#undef B
#undef FIELD
}

std::span<const BossTuningField> BossTuningFields() { return fields; }

BossCombatSettings BossTuningDefaults() {
    BossCombatSettings s;
    s.Load(); // Preserve team-authored model attachment data from the F2 file.
    s.ping.beamWidth=s.ping.beamHeight=2.4f;
    s.ping.trackingTime=.9f; s.ping.chargeTime=.95f;
    s.ping.beamDuration=.28f; s.ping.beamInterval=.35f; s.ping.damage=24;
    s.mineFuse=.6f;
    s.battle.mineTriggerRadius=1.6f;
    s.anchor.startAngularSpeed=.6f; s.anchor.verticalAmplitude=1.5f;
    s.screw.previewTime=1.3f; s.screw.suctionDuration=3.5f;
    s.battle.screwPayloadDelay=1.1f;
    // A ship-origin volley needs more reach, with slower individually warned
    // pulses. Swimming away from a distant source remains a valid escape.
    s.wave.radiusMax=60; s.wave.expansionSpeed=20; s.wave.duration=3.8f;
    // Moving the wave into the water layer makes it a new threat.
    s.waveDamage=10;
    return s;
}

bool ValidateBossTuningSettings(const BossCombatSettings& s, std::string& error) {
    for (const auto& f:fields) {
        const float v=f.read(s);
        if(!std::isfinite(v)||v<f.minimum||v>f.maximum||
            (f.kind!=BossTuningField::Kind::Number&&std::trunc(v)!=v)) {
            error=std::string(f.label)+": 許容範囲外です。"; return false;
        }
    }
    const auto require=[&](bool ok,const char* message){if(!ok) error=message;return ok;};
    if(!require(s.swim.fullSpeed>s.swim.startSpeed,"泳ぎ: 最大になる速度を、開始速度より大きくしてください。")) return false;
    if(!require(s.anchor.maxAngularSpeed>=s.anchor.startAngularSpeed,"アンカー: 最高速度を初速以上にしてください。")) return false;
    if(!require(!s.battle.mineLinkShell||s.battle.mineTriggerRadius>=.75f,"機雷: 泡との連動中は、中心モデルが収まるよう探知半径を0.75m以上にしてください。")) return false;
    if(!require(s.battle.waveVolley.count<=1||s.battle.waveVolley.interval>=s.windup,"波: 連射間隔を予告時間以上にして、各波の予告を順番に表示してください。")) return false;
    if(!require(s.wave.radiusMax>=s.wave.radiusStart,"波: 最大半径を開始半径以上にしてください。")) return false;
    if(!require(s.wave.duration+.0001f>=(s.wave.radiusMax-s.wave.radiusStart)/s.wave.expansionSpeed,"波: 最大半径へ届くよう、持続時間を延ばすか速度を上げてください。")) return false;
    if(!require(s.wave.rock.spawnRadiusMax>=s.wave.rock.spawnRadiusMin&&s.wave.rock.scaleMax>=s.wave.rock.scaleMin&&
        s.wave.rock.launchPowerMax>=s.wave.rock.launchPowerMin,"岩: 各上限を下限以上にしてください。")) return false;
    if(!require(s.battle.screwPayloadCount==0||s.battle.screwPayloadDelay<s.screw.previewTime+s.screw.suctionDuration,"スクリュー: 機雷を投げる時間を吸引終了より前にしてください。")) return false;
    const auto nested=[](const Vector3& a,const Vector3& b){return a.x<=b.x&&a.y<=b.y&&a.z<=b.z;};
    if(!require(nested(s.screw.innerRangeHalfSize,s.screw.middleRangeHalfSize)&&nested(s.screw.middleRangeHalfSize,s.screw.outerRangeHalfSize),"スクリュー: 各軸を 内側≦中間≦外側 にしてください。")) return false;
    error.clear(); return true;
}
