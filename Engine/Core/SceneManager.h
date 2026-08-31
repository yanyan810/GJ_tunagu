#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

class GameApp;
class IScene;

class SceneManager {
public:
    using Factory = std::function<std::unique_ptr<IScene>()>;

    void Register(const std::string& name, Factory factory);
    void Change(GameApp& app, const std::string& name);

    void Update(GameApp& app, float dt);

    void DrawRender(GameApp& app);
    void Draw3D(GameApp& app);
    void Draw2D(GameApp& app);
    void DrawOverlay2D(GameApp& app);
    void Draw(GameApp& app);
    void DrawImGui(GameApp& app);
    void DrawPreview(GameApp& app);
    void DrawPostEffectTargets(GameApp& app);
    bool HasObjectBloomTargets() const;
    bool HasObjectOutlineBloomTargets() const;
    bool HasObjectLuminanceOutlineTargets() const;

    IScene* Current() { return current_.get(); }
    const std::string& CurrentName() const { return currentName_; }
    bool HasRegisteredScene(const std::string& name) const {
        return factories_.contains(name);
    }

private:
    std::unordered_map<std::string, Factory> factories_;
    std::unique_ptr<IScene> current_;
    std::vector<std::unique_ptr<IScene>> retiredScenes_;
    std::string currentName_;
};
