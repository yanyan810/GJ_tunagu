#include "SceneManager.h"
#include "IScene.h"
#include "GameApp.h"
#include <cassert>

void SceneManager::Register(const std::string& name, Factory factory) {
    factories_[name] = std::move(factory);
}

void SceneManager::Change(GameApp& app, const std::string& name) {
    auto it = factories_.find(name);
    assert(it != factories_.end());

    if (current_) {
        current_->OnExit(app);
        retiredScenes_.push_back(std::move(current_));
    }

    if (app.Render()) {
        app.Render()->SetMode(PostEffectMode::FullScreen);
    }

    current_ = it->second();
    currentName_ = name;
    current_->OnEnter(app);
}

void SceneManager::Update(GameApp& app, float dt) {
    if (!current_) return;

    current_->Update(app, dt);

    const std::string next = current_->NextScene();
    if (!next.empty()) {
        current_->ClearNextScene_();
        Change(app, next);
    }
}

void SceneManager::DrawRender(GameApp& app) {
    if (!current_) return;
    current_->DrawRender(app);
}

void SceneManager::Draw3D(GameApp& app) {
    if (!current_) return;
    current_->Draw3D(app);
}

void SceneManager::Draw2D(GameApp& app) {
    if (!current_) return;
    current_->Draw2D(app);
}

void SceneManager::DrawOverlay2D(GameApp& app) {
    if (!current_) return;
    current_->DrawOverlay2D(app);
}

void SceneManager::Draw(GameApp& app) {
    if (!current_) return;
    current_->Draw(app);
}

void SceneManager::DrawImGui(GameApp& app) {
    if (!current_) return;
    current_->DrawImGui(app);
}

void SceneManager::DrawPreview(GameApp& app) {
    if (!current_) return;
    current_->DrawPreview(app);
}

void SceneManager::DrawPostEffectTargets(GameApp& app) {
    if (!current_) return;
    current_->DrawPostEffectTargets(app);
}

bool SceneManager::HasObjectBloomTargets() const {
    return current_ && current_->HasObjectBloomTargets();
}

bool SceneManager::HasObjectOutlineBloomTargets() const {
    return current_ && current_->HasObjectOutlineBloomTargets();
}

bool SceneManager::HasObjectLuminanceOutlineTargets() const {
    return current_ && current_->HasObjectLuminanceOutlineTargets();
}
