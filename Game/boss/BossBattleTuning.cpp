#include "BossBattleTuning.h"
#include "BossTuningFields.h"
#include "BossTuningStore.h"
#include "imgui.h"
#include <algorithm>
#include <string>

struct BossBattleTuning::Impl {
    BossTuningStore store;
    BossCombatSettings draft;
    BossCombatController::Stats stats{};
    uint64_t seenRevision=0, appliedRevision=0;
    bool initialized=false, open=false, dirty=false, unsaved=false, showHelp=true;
    int request=-1, repeat=-1;

    void SyncDraft() {
        draft=store.Current(); seenRevision=store.Revision(); dirty=unsaved=false;
    }
    bool Apply() {
        if(!store.Apply(draft)) return false;
        draft=store.Current(); seenRevision=store.Revision();
        unsaved=unsaved||dirty; dirty=false; return true;
    }
};

BossBattleTuning::BossBattleTuning():impl_(std::make_unique<Impl>()) {}
BossBattleTuning::~BossBattleTuning()=default;
BossBattleTuning& BossBattleTuning::Get() { static BossBattleTuning instance; return instance; }
void BossBattleTuning::Initialize() {
    auto& e=*impl_; if(e.initialized) return;
    e.store.Initialize(); e.SyncDraft(); e.initialized=true;
}
void BossBattleTuning::Update(bool togglePressed) {
    Initialize(); auto& e=*impl_;
    e.store.Poll(e.dirty||e.unsaved);
    if(!e.store.ToolsEnabled()) {e.open=false;e.request=e.repeat=-1;return;}
    if(!e.dirty&&!e.unsaved&&e.seenRevision!=e.store.Revision()) e.SyncDraft();
    if(togglePressed) e.open=!e.open;
}
bool BossBattleTuning::IsOpen() const {return impl_->open&&IsEnabled();}
bool BossBattleTuning::IsEnabled() const {return impl_->initialized&&impl_->store.ToolsEnabled();}
bool BossBattleTuning::IsPaused() const {return IsOpen();}
const BossCombatSettings& BossBattleTuning::Settings() const {
    const_cast<BossBattleTuning*>(this)->Initialize(); return impl_->store.Current();
}
uint64_t BossBattleTuning::Revision() const {
    const_cast<BossBattleTuning*>(this)->Initialize(); return impl_->store.Revision();
}
void BossBattleTuning::Publish(const BossCombatController::Stats& stats,uint64_t revision) {
    impl_->stats=stats; impl_->appliedRevision=revision;
    if(!stats.enabled) impl_->request=-1;
}
int BossBattleTuning::ConsumeAttackRequest() {
    const int result=IsEnabled()?impl_->request:-1; impl_->request=-1; return result;
}
int BossBattleTuning::RepeatAttack() const {return IsEnabled()?impl_->repeat:-1;}

namespace {
const char* AttackLabel(int i) {
    static constexpr const char* names[]{"機雷","ビーム","ショックウェーブ＋岩","アンカー","スクリュー＋機雷","休止中"};
    return names[std::clamp(i,0,5)];
}
const char* Advice(const char* group) {
    const std::string g=group;
    if(g=="ビーム") return "おすすめ：判定を細くし、照準が固定されてから横へ逃げる猶予を確保。威力より先に回避の納得感を調整します。";
    if(g=="機雷") return "おすすめ：軽い先読みと到着時間で進路をふさぐ役割に。爆発範囲だけを広げず、起爆予告は残します。";
    if(g=="アンカー") return "おすすめ：上下の振れ幅を少し増やし、深さを変えてかわす攻撃に。半径や最高速度の急増は避けます。";
    if(g=="スクリュー") return "おすすめ：予告で範囲を認識して外へ泳ぐ。追加機雷は予告後に船から飛ばし、突然の出現をなくします。";
    if(g=="ショックウェーブ") return "おすすめ：予告した深さを水平に広がる波。上または下へ避ける役割にし、まず低いダメージで確認します。";
    if(g=="地面の岩") return "岩はショックウェーブ後の別の当たり判定です。現状の強さと箱モデルは維持。今後は落下地点の予告と狙い直しをセットで検討します。";
    return "数値の反映は次の攻撃から。すでに飛んでいる機雷・岩の設定は維持します。単体確認では残存弾を消して選んだ攻撃から再開します。";
}
}

void BossBattleTuning::DrawPanel() {
    if(!IsOpen()||!ImGui::GetCurrentContext()) return;
    auto& e=*impl_;
    const auto display=ImGui::GetIO().DisplaySize;
    const ImVec2 size{std::min(860.0f,std::max(300.0f,display.x-24)),std::min(760.0f,std::max(300.0f,display.y-24))};
    ImGui::SetNextWindowSize(size,ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({12,12},ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("ボス攻撃の調整  [F9で閉じる]###BossBattleTuning",&e.open,
        ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking)) {
        ImGui::End();return;
    }
    ImGui::TextColored({.5f,.9f,1,1},"一時停止中 — 閉じるとゲームを再開します");
    ImGui::TextWrapped("JSON: resources/Data/BossBattleTuning.json");
    ImGui::Text("自動読込: %s / %s",e.store.HotReloadEnabled()?"有効（0.5秒間隔）":"無効",
        e.dirty?"編集内容は未反映":e.unsaved?"反映済み・JSON未保存":"保存値と同期済み");
    if(e.stats.enabled) {
        ImGui::Text("現在: %s  |  機雷 %zu  岩 %zu  命中 %llu",AttackLabel(static_cast<int>(e.stats.attack)),
            e.stats.mines,e.stats.rocks,static_cast<unsigned long long>(e.stats.hits));
        if(e.appliedRevision!=e.store.Revision()) ImGui::TextColored({1,.8f,.3f,1},"変更は次の攻撃から反映されます。");
    } else ImGui::TextDisabled("単体確認はゲームシーンでボス登場後に使えます。");

    ImGui::BeginDisabled(!e.stats.enabled);
    const char* repeats[]{"通常の順番","機雷","ビーム","ショックウェーブ＋岩","アンカー","スクリュー＋機雷"};
    int repeat=e.repeat+1;
    ImGui::SetNextItemWidth(270);
    if(ImGui::Combo("繰り返す攻撃",&repeat,repeats,6)) e.repeat=repeat-1;
    if(ImGui::Button("選んだ攻撃を単体確認して再開")&&e.Apply()) {
        e.request=e.repeat>=0?e.repeat:static_cast<int>(e.stats.attack);
        if(e.request<0||e.request>=5) e.request=1;
        e.open=false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();ImGui::Checkbox("説明を表示",&e.showHelp);
    ImGui::Separator();

    // Keep actions visible while the long parameter pages scroll separately.
    const float editorHeight=std::max(110.0f,ImGui::GetContentRegionAvail().y-150.0f);
    if(ImGui::BeginChild("parameters",{0,editorHeight},ImGuiChildFlags_Borders)) {
        if(ImGui::BeginTabBar("attackGroups")) {
            static constexpr const char* groups[]{"全体","ビーム","機雷","アンカー","スクリュー","ショックウェーブ","地面の岩"};
            for(const char* group:groups) if(ImGui::BeginTabItem(group)) {
                ImGui::TextWrapped("%s",Advice(group));ImGui::Separator();
                for(const auto& f:BossTuningFields()) {
                    if(std::string(f.group)!=group) continue;
                    ImGui::PushID(f.key);
                    float value=f.read(e.draft); bool changed=false;
                    ImGui::SetNextItemWidth(135);
                    if(f.kind==BossTuningField::Kind::Boolean) {
                        bool flag=value!=0;
                        changed=ImGui::Checkbox(f.label,&flag); value=flag?1.0f:0.0f;
                    } else if(f.kind==BossTuningField::Kind::Integer) {
                        int count=static_cast<int>(value);
                        changed=ImGui::DragInt(f.label,&count,1,static_cast<int>(f.minimum),static_cast<int>(f.maximum),"%d",ImGuiSliderFlags_AlwaysClamp);
                        value=static_cast<float>(count);
                    } else changed=ImGui::DragFloat(f.label,&value,f.step,f.minimum,f.maximum,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                    if(changed) {f.write(e.draft,value);e.dirty=true;}
                    if(*f.unit) {ImGui::SameLine();ImGui::TextDisabled("%s",f.unit);}
                    if(e.showHelp) {ImGui::PushStyleColor(ImGuiCol_Text,{.68f,.76f,.82f,1});ImGui::TextWrapped("%s",f.help);ImGui::PopStyleColor();}
                    else if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",f.help);
                    ImGui::Spacing();ImGui::PopID();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
    std::string validation;
    const bool valid=ValidateBossTuningSettings(e.draft,validation);
    if(!valid) ImGui::TextColored({1,.45f,.3f,1},"%s",validation.c_str());
    ImGui::BeginDisabled(!valid);
    if(ImGui::Button("数値を反映")) e.Apply();
    ImGui::SameLine();
    if(ImGui::Button("JSONに保存して反映")&&e.store.Save(e.draft)) e.SyncDraft();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("JSONを再読込（編集を破棄）")&&e.store.Reload()) e.SyncDraft();
    if(ImGui::Button("おすすめ初期値を編集欄へ戻す")) {e.draft=BossTuningDefaults();e.dirty=true;}
    ImGui::SameLine();
    if(ImGui::Button("閉じて再開 [F9]")) e.open=false;
    ImGui::TextWrapped("%s",e.store.Status().c_str());
    ImGui::End();
}
