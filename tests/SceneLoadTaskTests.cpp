#include "../Engine/Core/SceneLoadTask.h"
#include <cassert>
#include <stdexcept>

SceneLoadTask Load(int& stages, int& active) {
    struct Lifetime { int& value; Lifetime(int& v) : value(v) { ++value; } ~Lifetime() { --value; } } guard(active);
    ++stages; co_yield 0.25f;
    ++stages; co_yield 0.75f;
    ++stages;
}
SceneLoadTask Fail() { throw std::runtime_error("load failed"); co_return; }
int main() {
    int stages = 0, active = 0;
    auto task = Load(stages, active);
    assert(stages == 0 && !task.Done());
    task.Step(); assert(stages == 1 && task.Progress() == 0.25f && active == 1);
    auto moved = std::move(task);
    assert(task.Done());
    moved.Step(); assert(stages == 2 && moved.Progress() == 0.75f);
    moved.Step(); assert(stages == 3 && moved.Done() && moved.Progress() == 1.0f && active == 0);
    moved.Step(); assert(stages == 3);
    auto cancelled = Load(stages, active); cancelled.Step();
    assert(active == 1); cancelled = {}; assert(active == 0);
    auto failed = Fail(); bool caught = false;
    try { failed.Step(); } catch (const std::runtime_error&) { caught = true; }
    assert(caught);
}
