# RewardServer for BakkesMod

Drive in Rocket League freeplay and watch **your own** reward functions fire live in a BakkesMod overlay. You can also save the last seconds of play as a clip, tag clips against a list of test shots, and replay saved clips in the game.

This bundle has no rewards in it. You plug yours in with one source file and build the server inside your own GigaLearnCPP-based project.

```
Rocket League + BakkesMod                    your PC (localhost only)
  RewardTesterPlugin.dll  <-- TCP :42069 -->  reward_server.exe  (built from your project)
```

| Path | What is in it |
|---|---|
| `plugin/RewardTesterPlugin.dll` | The plugin, prebuilt |
| `install_plugin.bat` | Copies the DLL into BakkesMod and makes it load on startup |
| `RewardServer/reward_server/` | Server source |
| `RewardServer/rlpr/` | Clip reader and writer |
| `RewardServer/examples/user_reward_example.cpp` | **The file you edit** |
| `RewardServer/examples/example_tasks.json` | Task list template |

***

## Getting Started

1.  Copy `RewardServer` into the root of your project and add the server after your `GigaLearnCPP` target:

    ```cmake
    set(RS_FRAMEWORK_ROOT "${CMAKE_SOURCE_DIR}/GigaLearnCPP")
    add_subdirectory(RewardServer/reward_server)
    ```

2.  Edit `RewardServer/examples/user_reward_example.cpp` so it builds your reward. It's two functions, `rs::CreateUserReward` and `rs::CreateEvalExtension`. `docs/INTEGRATION.md` walks through it. Return null from `CreateEvalExtension`.
3.  Build target `reward_server` (Release, MSVC, C++20).
4.  Close Rocket League and run `install_plugin.bat`. It backs up an existing copy to `RewardTesterPlugin.dll.old` and adds `plugin load rewardtesterplugin` to BakkesMod's `plugins.cfg`.
5.  Start the server from the folder that contains `collision_meshes`, then start the game and enter Freeplay:

    ```powershell
    reward_server.exe --tasks RewardServer/examples/example_tasks.json --meshes collision_meshes
    ```

6.  Press F2, open **Plugins**, **Reward Tester**, and open the inspector window. The title line reads `CONNECTED` when it found the server. Bind the keys once with the buttons under "Active Keybinds" on the same page.

If your fork's `GameState` lacks a field the server reads, the compiler names it. Adapt that line in `reward_eval.cpp` or `rlpr/RLPRExporter.cpp`.

***

## Keys

| Key | Command | What it does |
|---|---|---|
| NumPad 0 | `rewardtester_toggle_menu` | Shows or hides the inspector window |
| X | `rewardtester_save_task_clip` | Saves the last 15 s tagged for the active task and counts an attempt |
| NumPad * | `rewardtester_mark_scored` | Marks the latest attempt as scored. Changes it if it was a miss |
| NumPad / | `rewardtester_mark_miss` | Marks the latest attempt as missed. Changes it if it was scored |
| NumPad . | `rewardtester_undo_attempt` | Takes the latest attempt out of the counters |
| NumPad + and - | `rewardtester_next_task`, `rewardtester_prev_task` | Changes the active task |
| C | `rewardtester_save_clip` | Saves a generic clip |
| console | `rewardtester_reload` | Reloads the config on the server, which calls your `CreateUserReward` again |
| console | `rewardtester_reset` | Clears reward state. It also happens after every goal and freeplay reset |

Clips go to `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips\generic\` (C) and `...\tasks\<tag>\<tag>_attempt<N>.rlpr` (X).

If the overlay stays `DISCONNECTED`, all values are 0, or the plugin doesn't load after an update, The `docs/FAQ.md` has the causes. A prebuilt DLL is tied to the sender's BakkesMod SDK, so ask for a rebuild after a big update.
