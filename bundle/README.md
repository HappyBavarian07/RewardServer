# RewardServer for BakkesMod

Drive in Rocket League freeplay, or watch a replay, and see your own reward functions fire live in a BakkesMod overlay. Save clips, count attempts at test shots and play clips back in the game.

This bundle has no rewards. You put yours in with one source file and build the server inside your own GigaLearnCPP-based project.

```
Rocket League + BakkesMod                    your PC (localhost only)
  RewardTesterPlugin.dll  <-- TCP :42069 -->  reward_server.exe  (built from your project)
```

| Path | Content |
|---|---|
| `plugin/RewardTesterPlugin.dll` | prebuilt plugin |
| `install_plugin.bat` | copies the DLL into BakkesMod and loads it on startup |
| `RewardServer/reward_server/` | server source |
| `RewardServer/rlpr/` | clip reader and writer |
| `RewardServer/examples/user_reward_example.cpp` | **the file you edit** |
| `RewardServer/examples/example_tasks.json` | task list template |
| `RewardServer/docs/` | full docs |

***

## Getting Started

1.  Copy `RewardServer` into your project root and add the server after your `GigaLearnCPP` target:

    ```cmake
    set(RS_FRAMEWORK_ROOT "${CMAKE_SOURCE_DIR}/GigaLearnCPP")
    add_subdirectory(RewardServer/reward_server)
    ```

2.  Edit `RewardServer/examples/user_reward_example.cpp` so it builds your reward: `rs::CreateUserReward`, and `rs::CreateEvalExtension` returning null. `docs/INTEGRATION.md` explains it.
3.  Build the `reward_server` target (Release, MSVC, C++20).
4.  Close Rocket League and run `install_plugin.bat`. It backs up an existing copy to `RewardTesterPlugin.dll.old` and adds `plugin load rewardtesterplugin` to `plugins.cfg`.
5.  Start the server from the folder that contains `collision_meshes`, then the game:

    ```powershell
    reward_server.exe --tasks RewardServer/examples/example_tasks.json --meshes collision_meshes
    ```

6.  Press F2, open Plugins, Reward Tester. The header says `CONNECTED` when it found the server. In Settings, press **Apply all keybinds**.

If your fork's `GameState` lacks a field the server reads, the compiler names it. Adapt that line in `reward_eval.cpp` or `rlpr/RLPRExporter.cpp`.

***

## Keys

| Key | What it does |
|---|---|
| NumPad 0 | show or hide the window |
| X | save the shot as the next attempt |
| NumPad * / NumPad / | mark the latest attempt scored / missed. The other key changes it |
| NumPad . | remove the latest attempt |
| NumPad + / - | next / previous shot |
| C | save a generic clip |

Clips go to `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips\`. Problems are covered in `docs/FAQ.md`. A prebuilt DLL is tied to the sender's BakkesMod SDK, so ask for a rebuild after a big update.
