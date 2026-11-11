# Plugging in Your Reward

### Introduction

On this page I want to show you how to get your own reward into the server. At the end you have a `reward_server.exe` whose overlay rows are your terms.

You write one source file with two functions. The server calls them. Nothing else in RewardServer needs to change.

***

## What You Need

*   A CMake project that builds the `GigaLearnCPP` target (any GigaLearnCPP-based project works).
*   MSVC with C++20.
*   The reward classes you want to test, or a config format you can load yourself.

***

## Writing the Source File

**First Step**: Copy `examples/user_reward_example.cpp` next to your reward code and open it.

**Second Step**: Implement `IRewardSource` around your reward.

{% code title="my_reward.cpp" lineNumbers="true" %}
```cpp
#include "user_reward.h"

namespace {
using namespace RLGC;

class MySource : public rs::IRewardSource {
public:
    explicit MySource(std::function<uint64_t()> getTimestep) : getStep_(std::move(getTimestep)) {}

    const std::vector<std::string>& Names() const override { return names_; }

    void PreStep(const GameState& state) override {}

    void Evaluate(const GameState& state, int focus, rs::RewardTerms& out) override {
        const Player& p = state.players[focus];
        out.names = names_;
        out.values = {p.vel.Length() / 2300.f};
        out.weights = {1.f};
        out.weighted = {out.values[0] * out.weights[0]};
        out.total = out.weighted[0];
    }

private:
    std::function<uint64_t()> getStep_;
    std::vector<std::string> names_ = {"Speed"};
};
}

namespace rs {
std::unique_ptr<IRewardSource> CreateUserReward(const std::string& configPath, std::function<uint64_t()> getTimestep) {
    return std::make_unique<MySource>(std::move(getTimestep));
}
std::unique_ptr<IEvalExtension> CreateEvalExtension() { return nullptr; }
}
```
{% endcode %}

**What happens here**:

*   `CreateUserReward` runs on every `load` and `reload`. `configPath` is whatever the overlay's config box holds. Load a file with it or ignore it.
*   `getTimestep` returns the training step the overlay's slider is set to. Use it if your terms fade in over training.
*   `PreStep` runs once per tick before `Evaluate`. Put state tracking there.
*   `Evaluate` fills one row per term. `weights` and `weighted` may stay empty for terms without a weight.
*   Terms that fire in your trainer should keep their trainer name, so the overlay matches your metrics.

**Third Step**: Wrap a real reward. If your reward is a class with `GetReward(player, state, isFinal)`, call it inside `Evaluate` and write one row per class. For a combined reward, push one row per sub-term.

***

## Optional: Evaluation Hooks

`CreateEvalExtension` can return an `IEvalExtension`. Its hooks run around every evaluation and see the same `GameState` the reward sees:

| Hook | Runs | Typical use |
|---|---|---|
| `OnLoad(source)` | After a reward was (re)loaded | Reset detectors, read term configuration |
| `OnReset()` | On goals and freeplay resets | Clear per-episode state |
| `BeforeEvaluate(gs)` | After state reconstruction, before `PreStep` | Fill caches, attach `gs.userInfo` |
| `AfterEvaluate(gs)` | After `Evaluate` | Collect labels |
| `TickReport(focus)` / `ReplayReport()` | During `--replay --json --extra` | Add your fields to the output |

Return null if you don't need any of it.

***

## Building

**First Step**: Tell CMake about your file before adding the server.

{% code title="CMakeLists.txt" lineNumbers="true" %}
```cmake
set(RS_FRAMEWORK_ROOT "${CMAKE_SOURCE_DIR}/GigaLearnCPP")
set(RS_INTEGRATION_SOURCES "${CMAKE_SOURCE_DIR}/my_reward.cpp")
set(RS_EXTRA_SOURCES ${MY_REWARD_SOURCES})
add_subdirectory(RewardServer/reward_server)
```
{% endcode %}

**Second Step**: Build the `reward_server` target in Release.

Set these as normal variables before `add_subdirectory`. The full list is in [Server](SERVER.md#build-variables).

***

## Checking That It Works

Start the server from the folder that contains `collision_meshes`:

```powershell
reward_server.exe --config reward.json
```

The console should print these lines:

```text
[server] RocketSim arena mirror enabled (meshes: collision_meshes)
[server] preloaded config 'reward.json' (N terms)
[server] listening on 127.0.0.1:42069
```

Then run the protocol check from another terminal. It loads the config, evaluates a canned state and checks every reply shape:

```powershell
python tests/smoke_client.py --config reward.json
```

`SMOKE OK (N terms)` means the server and your reward agree on the row count. After that, open the overlay in the game. See [Plugin](PLUGIN.md).

**Note**: Changing reward code means rebuilding and restarting the server. The plugin reconnects by itself. A config change only needs **Reload Config** in the overlay.

And done. Next: [Server](SERVER.md) for clips, tasks and the offline replay.
