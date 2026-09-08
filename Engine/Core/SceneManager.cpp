#include "SceneManager.h"
#include "IScene.h"
#include "GameApp.h"
#include <cassert>
#include "LoadingScreen.h"

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() = default;

void SceneManager::Register(const std::string& name, Factory factory) {
    factories_[name] = std::move(factory);
}

void SceneManager::Shutdown(GameApp& app) {
    loadingTask_ = {};
    loading_ = false;
    if (current_) current_->OnExit(app);
    current_.reset();
    retiredScenes_.clear();
    currentName_.clear();
    loadingScreen_.reset();
}

void SceneManager::Change(GameApp& app, const std::string& name) {
    auto it = factories_.find(name);
    assert(it != factories_.end());

    loadingTask_ = {};
    if (current_) {
        current_->OnExit(app);
        retiredScenes_.push_back(std::move(current_));
    }

    if (app.Render()) {
        app.Render()->SetMode(PostEffectMode::FullScreen);
    }

    current_ = it->second();
    currentName_ = name;
    if (!loadingScreen_) {
        loadingScreen_ = std::make_unique<LoadingScreen>();
        loadingScreen_->Initialize(app);
    }
    loadingScreen_->Reset();
    loadingTask_ = current_->Load(app);
    loading_ = true;
    loadingDrawn_ = completionDrawn_ = false;
}

void SceneManager::Update(GameApp& app, float dt) {
    if (!current_) return;

    if (loading_) {
        if (completionDrawn_) {
            loading_ = false;
            loadingTask_ = {};
        } else {
            // Advance at most one step per presented loading frame.
            if (loadingDrawn_ && !loadingTask_.Done()) {
                loadingDrawn_ = false;
                loadingTask_.Step();
            }
            loadingScreen_->Update(dt, loadingTask_.Progress());
            return;
        }
    }
    current_->Update(app, dt);

    const std::string next = current_->NextScene();
    if (!next.empty()) {
        current_->ClearNextScene_();
        Change(app, next);
    }
}

void SceneManager::DrawRender(GameApp& app) {
    if (loading_) return;
    if (!current_) return;
    current_->DrawRender(app);
}

void SceneManager::Draw3D(GameApp& app) {
    if (loading_) return;
    if (!current_) return;
    current_->Draw3D(app);
}

void SceneManager::Draw2D(GameApp& app) {
    if (loading_) return;
    if (!current_) return;
    current_->Draw2D(app);
}

void SceneManager::DrawOverlay2D(GameApp& app) {
    if (loading_) {
        loadingScreen_->Draw();
        loadingDrawn_ = true;
        completionDrawn_ = loadingTask_.Done();
        return;
    }
    if (!current_) return;
    current_->DrawOverlay2D(app);
}

void SceneManager::Draw(GameApp& app) {
    if (loading_) return;
    if (!current_) return;
    current_->Draw(app);
}

void SceneManager::DrawImGui(GameApp& app) {
    if (loading_) return;
    if (!current_) return;
    current_->DrawImGui(app);
}

void SceneManager::DrawPreview(GameApp& app) {
    if (loading_) return;
    if (!current_) return;
    current_->DrawPreview(app);
}

void SceneManager::DrawPostEffectTargets(GameApp& app) {
    if (loading_) return;
    if (!current_) return;
    current_->DrawPostEffectTargets(app);
}

bool SceneManager::HasObjectBloomTargets() const {
    return !loading_ && current_ && current_->HasObjectBloomTargets();
}

bool SceneManager::HasObjectOutlineBloomTargets() const {
    return !loading_ && current_ && current_->HasObjectOutlineBloomTargets();
}

bool SceneManager::HasObjectLuminanceOutlineTargets() const {
    return !loading_ && current_ && current_->HasObjectLuminanceOutlineTargets();
}
