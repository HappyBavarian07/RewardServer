---
description: Build, install and use the BakkesMod overlay, keybinds and clip replay
---

# Plugin

`RewardTesterPlugin.dll` is the BakkesMod side. It reads the cars and the ball on every physics tick and hands them to `reward_server` from a worker thread. The game thread never waits on the network, so a stuck server shows up as `DISCONNECTED`, not as a frozen game.

***

## Building and Installing

You need Windows, Rocket League with [BakkesMod](https://bakkesmod.com/) (start it once, it unpacks the SDK to `%APPDATA%\bakkesmod\bakkesmod\bakkesmodsdk`), Visual Studio with the C++ workload, CMake and a GigaLearnCPP checkout for the game-state headers.

```powershell
scripts\build_plugin.ps1 -FrameworkRoot <path to GigaLearnCPP> [-NoDeploy] [-DataFiles a.json,b.json]
```

| Parameter | What it does |
|---|---|
| `-FrameworkRoot` | GigaLearnCPP checkout. Required |
| `-NoDeploy` | Build only. Leave the installed plugin alone |
| `-DataFiles` | Files to copy into BakkesMod's `data` folder, for example your reward config and task list |
| `-Config` | Build configuration. Default `Release` |

The script builds into `BakkesModPlugin/build`, moves the installed copy to `RewardTesterPlugin.dll.old`, and copies the new DLL into the BakkesMod `plugins` folder. It refuses to deploy while Rocket League runs, because the game keeps the DLL locked.

Load the plugin from the BakkesMod console (F6) with `plugin load rewardtesterplugin`, or add that line to `%APPDATA%\bakkesmod\bakkesmod\cfg\plugins.cfg`. After a rebuild run `plugin unload rewardtesterplugin` first, or close the game.

***

## Opening the Overlay

1.  Start `reward_server.exe` ([Server](SERVER.md)).
2.  Start Rocket League and enter Freeplay.
3.  Press F2, open **Plugins**, **Reward Tester**, and open the inspector window. The `rewardtester_toggle_menu` keybind does the same.

The title line is green and shows the term count when the server is connected.

***

## Live Rewards Tab

| Control | What it does |
|---|---|
| Server Host, Port, **Connect / Reconnect** | Where the server runs. Defaults `127.0.0.1` and `42069` |
| Config (server-side) | The path passed to `CreateUserReward`. The server resolves it from its working directory |
| **Reload Config** | Reloads the reward without restarting the server |
| Training Step slider, +1B / +10B / +50B | Previews schedule weights at that step |
| **Force late-schedule step** | Evaluates fully faded-in weights. At step 0 most scheduled terms read zero |
| **Reset Reward State** | Clears per-episode state. It also happens after every goal and freeplay reset |
| **Save Last Ns (.rlpr)** | Writes the server's rolling window to a clip |
| Rolling Avg Memory | Window of the `Avg(raw)` column, in ticks |

The table has five columns: Name, Weight, Raw, Weighted and Avg(raw). Raw is the term's own signal and Weighted is Raw times Weight. A term with weight zero still shows its raw value, so you can see whether it fires before it pays.

***

## Shot Tasks Tab

The task list comes from the server (`--tasks`). Each task has attempt counters and save, scored, miss and next buttons. **Reload Tasks on Server** switches the file, **Reset Shot** and **Reset All Shots** zero the counters. Below the list the tab shows the terms that currently fire.

***

## State Inspector and Clips Tab

*   **Clip length**: 1 to 300 seconds.
*   **Input sensitivity and deadzone**: used when a clip is replayed input-driven.
*   **Folders**: create, rename and move clips inside the clip folder.
*   **Replay**: selecting a clip plays it in the game. **P** plays or pauses, **Left** and **Right** step frames, **Z** restarts, **Esc** stops. The panel has speed, loop, state-driven and input-driven (re-simulated) modes.

***

## Keybinds and Console Commands

Bind the keys once with the buttons under "Active Keybinds" on the plugin's settings page. Each button runs the matching `bind` command.

| Key | Command | What it does |
|---|---|---|
| NumPad 0 | `rewardtester_toggle_menu` | Shows or hides the inspector window |
| X | `rewardtester_save_task_clip` | Saves the rolling window tagged for the active task and counts an attempt |
| NumPad * | `rewardtester_mark_scored` | Marks the active attempt as scored |
| NumPad / | `rewardtester_mark_miss` | Marks the active attempt as missed |
| NumPad + and - | `rewardtester_next_task`, `rewardtester_prev_task` | Changes the active task |
| C | `rewardtester_save_clip [name]` | Saves a generic clip |
| console | `rewardtester_reload` | Reloads the reward config on the server |
| console | `rewardtester_reset` | Clears per-episode reward state on the server |
| console | `rewardtester_autosave [seconds]` | Saves a clip every N seconds of physics. 13 is the maximum, 0 turns it off |
| console | `rewardtester_probe_start [seed] [episodeSeconds] [saveEvery]`, `rewardtester_probe_stop` | Automated physics probe episodes. Developer tool |

Press `rewardtester_mark_scored` after a task clip and it marks that saved attempt without counting it twice.

***

## Files

| Location | What is in it |
|---|---|
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips\generic\` | Generic clips |
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips\tasks\<tag>\` | Task clips, `<tag>_attempt<N>.rlpr` |
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_tester.ini` | Clip length and input slider preferences |

Something not working? The [FAQ](FAQ.md) covers the usual causes.
