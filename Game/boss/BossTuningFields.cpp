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
    F("ビーム","beam.leadScale","移動先の先読み倍率",battle.beamLeadScale,0,1,.05f,"0で現在位置、1で従来と同じ先読み。急な方向転換で避ける余地を調整します。","倍"),
    F("ビーム","beam.maxLead","先読み距離の上限",battle.beamMaxLeadDistance,0,45,.5f,"高速移動時でも照準がこの距離以上先へ飛ばないようにします。","m"),
    F("ビーム","beam.damage","ダメージ",ping.damage,0,200,1,"ビーム命中時のHPダメージ。","HP"),
    F("ビーム","beam.slow","減速の強さ",ping.moveSpeedDamage,0,100,1,"既存の被弾減速へ渡す値。すべての攻撃に共通する回復処理を使用します。","設定値"),
    I("機雷","mine.count","1回に投げる数",mineCount,1,24,"機雷の同時プール上限は24個です。残存機雷がある場合は空きの範囲で発射します。"),
    F("機雷","mine.interval","投げる間隔",mineInterval,.08f,2,.02f,"機雷を順番に発射する間隔。","秒"),
    F("機雷","mine.travelTime","到着の目安時間",battle.mineTravelTime,.5f,5,.1f,"水の抵抗を補正して投げる速さを決めます。吸引や近距離の速度制限で実際の時間は変わります。","秒"),
    F("機雷","mine.leadTime","移動先の先読み",battle.mineAimLeadTime,0,2.5f,.05f,"各機雷の発射前に移動先を予測する時間。発射後は追尾しません。","秒"),
    F("機雷","mine.triggerRadius","近接起爆の半径",battle.mineTriggerRadius,.5f,8,.1f,"プレイヤーが近づくと導火線を開始する半径。プレイヤー判定半径を別に加算します。","m"),
    F("機雷","mine.fuse","接近後の爆発猶予",mineFuse,.2f,3,.05f,"起爆予告から爆発まで。短すぎると対処しづらくなるため0.2秒以上。","秒"),
    F("機雷","mine.blastRadius","爆風の半径",mine.explosionRadius,1,15,.25f,"爆発の見た目とダメージ範囲の共通半径。","m"),
    F("機雷","mine.lifetime","自然起爆まで",mineLifetime,2,20,.5f,"接近しなかった機雷も、この時間で導火線を開始します。","秒"),
    F("機雷","mine.chainFuse","誘爆の猶予",mineChainFuse,.1f,3,.05f,"他の機雷の爆風を受けたときの導火線の時間。","秒"),
    F("機雷","mine.drag","水中抵抗",mine.drag,0,3,.05f,"飛翔速度を減衰させる強さ。弾道が変わるため到着時間と合わせて調整します。","1/秒"),
    F("機雷","mine.damage","爆発ダメージ",mine.damage,0,200,1,"単体散布・スクリューに使われる機雷の共通ダメージ。","HP"),
    F("アンカー","anchor.radius","回転半径の下限",anchor.radius,5,70,.5f,"実際の半径は予告開始時の船とプレイヤーの水平距離にも合わせます。","m"),
    F("アンカー","anchor.radiusBias","狙う半径の追加幅",battle.anchorRadiusBias,-5,10,.25f,"プレイヤーまでの水平距離に加える量。半径の下限と上限85mも適用されます。","m"),
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
    F("スクリュー","screw.duration","吸引を続ける時間",screw.suctionDuration,1,8,.1f,"吸引開始から機雷を保持する段階へ移るまでの時間。","秒"),
    F("スクリュー","screw.hold","放出前の保持",screw.holdTime,.2f,3,.05f,"吸引後、下へ放出する前の間。","秒"),
    F("スクリュー","screw.release","下へ放出する力",screw.releasePower,0,70,1,"吸引範囲に残っていたプレイヤー・機雷に加える下向きの速度。","m/秒"),
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
    F("ショックウェーブ","wave.maxRadius","最大半径",wave.radiusMax,5,60,.5f,"波が届く最大距離。開始半径以上に設定します。","m"),
    F("ショックウェーブ","wave.speed","波の広がる速さ",wave.expansionSpeed,2,40,.5f,"半径が1秒に増える量。速いほど水平に泳いで逃げにくくなります。","m/秒"),
    F("ショックウェーブ","wave.duration","波の持続時間",wave.duration,.5f,8,.1f,"最大半径まで到達できる時間を確保してください。最大半径到達後に岩が発生します。","秒"),
    B("ショックウェーブ","wave.playerDepth","泳いでいる深さに出す",battle.waveAtTargetDepth,"ON: 予告開始時のプレイヤーの深さに水平の波。上下に避けられます。OFF: 従来の海底の波。"),
    F("ショックウェーブ","wave.halfHeight","波の判定の上下幅",battle.waveHalfHeight,.2f,4,.1f,"波の中心面から上下それぞれの当たり幅。プレイヤー半径を別に加算します。","m"),
    F("ショックウェーブ","wave.damage","波のダメージ",waveDamage,0,100,1,"波本体のダメージ。地面から出る岩のダメージとは別です。","HP"),
    I("地面の岩","rock.count","出現する岩の数",wave.rock.spawnCount,0,48,"波が最大半径に達すると出る岩の総数。0なら波単体の確認ができます。"),
    F("地面の岩","rock.interval","岩が出る間隔",wave.rock.spawnInterval,.05f,.5f,.01f,"岩を順番に出す間隔。","秒"),
    F("地面の岩","rock.radiusMin","出現距離の最小値",wave.rock.spawnRadiusMin,0,30,.5f,"予告中心からの水平距離。中央を狙う岩は例外として近くに出ます。","m"),
    F("地面の岩","rock.radiusMax","出現距離の最大値",wave.rock.spawnRadiusMax,1,60,.5f,"最小値以上に設定します。現在は予告時の位置が中心で、発射後の追尾はありません。","m"),
    F("地面の岩","rock.scaleMin","岩の大きさ・最小",wave.rock.scaleMin,.1f,4,.05f,"実モデルと球近似の当たり判定に使う大きさ。モデルの交換は別作業です。","倍"),
    F("地面の岩","rock.scaleMax","岩の大きさ・最大",wave.rock.scaleMax,.1f,4,.05f,"最小値以上に設定します。","倍"),
    F("地面の岩","rock.launchMin","上昇の初速・下限",wave.rock.launchPowerMin,0,80,1,"泳いでいる深さへ届くよう本編では必要な初速まで補正されます。これは下限です。","m/秒"),
    F("地面の岩","rock.launchMax","上昇の初速・上限",wave.rock.launchPowerMax,0,85,1,"本編での深さ補正でさらに大きくなる場合があります。下限以上に設定します。","m/秒"),
    F("地面の岩","rock.horizontal","横に飛ぶ速さ",wave.rock.horizontalPower,0,20,.5f,"中央の一部の岩を除き、外側へ広がる速度。","m/秒"),
    F("地面の岩","rock.lifetime","残る時間",wave.rock.lifetime,1,8,.25f,"出現後に消えるまで。長いと同時に残る岩が増えます。","秒"),
    F("地面の岩","rock.damage","岩のダメージ",wave.rock.damage,0,100,1,"岩1個が命中した際のダメージ。","HP"),
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
    s.anchor.startAngularSpeed=.6f; s.anchor.verticalAmplitude=1.5f;
    s.screw.previewTime=1.3f; s.screw.suctionDuration=3.5f;
    s.battle.screwPayloadDelay=1.1f;
    // A 30 m / 18 m/s wave finished before an ordinary 15 m/s swimmer
    // could be reached after the .9 s warning. Keep expansion time similar.
    s.wave.radiusMax=40; s.wave.expansionSpeed=24;
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
    if(!require(s.anchor.maxAngularSpeed>=s.anchor.startAngularSpeed,"アンカー: 最高速度を初速以上にしてください。")) return false;
    if(!require(s.wave.radiusMax>=s.wave.radiusStart,"波: 最大半径を開始半径以上にしてください。")) return false;
    if(!require(s.wave.duration+.0001f>=(s.wave.radiusMax-s.wave.radiusStart)/s.wave.expansionSpeed,"波: 最大半径へ届くよう、持続時間を延ばすか速度を上げてください。")) return false;
    if(!require(s.wave.rock.spawnRadiusMax>=s.wave.rock.spawnRadiusMin&&s.wave.rock.scaleMax>=s.wave.rock.scaleMin&&
        s.wave.rock.launchPowerMax>=s.wave.rock.launchPowerMin,"岩: 各上限を下限以上にしてください。")) return false;
    if(!require(s.battle.screwPayloadCount==0||s.battle.screwPayloadDelay<s.screw.previewTime+s.screw.suctionDuration,"スクリュー: 機雷を投げる時間を吸引終了より前にしてください。")) return false;
    const auto nested=[](const Vector3& a,const Vector3& b){return a.x<=b.x&&a.y<=b.y&&a.z<=b.z;};
    if(!require(nested(s.screw.innerRangeHalfSize,s.screw.middleRangeHalfSize)&&nested(s.screw.middleRangeHalfSize,s.screw.outerRangeHalfSize),"スクリュー: 各軸を 内側≦中間≦外側 にしてください。")) return false;
    error.clear(); return true;
}
