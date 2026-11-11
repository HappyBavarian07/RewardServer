---
description: Build, install and use the BakkesMod overlay, keybinds, clip replay and replay watching
---

# Plugin

`RewardTesterPlugin.dll` is the BakkesMod side. It reads the cars and the ball and hands them to `reward_server` from a worker thread. The game thread never waits on the network, so a stuck server shows up as `DISCONNECTED`, not as a frozen game.

***

## Building and Installing

You need Windows, Rocket League with [BakkesMod](https://bakkesmod.com/) (start it once, it unpacks the SDK to `%APPDATA%\bakkesmod\bakkesmod\bakkesmodsdk`), Visual Studio with the C++ workload, CMake and a GigaLearnCPP checkout for the game-state headers.

```powershell
scripts\build_plugin.ps1 -FrameworkRoot <path to GigaLearnCPP> [-NoDeploy] [-Force] [-DataFiles a.json,b.json]
```

| Parameter | What it does |
|---|---|
| `-FrameworkRoot` | GigaLearnCPP checkout. Required |
| `-NoDeploy` | Build only. Leave the installed plugin alone |
| `-Force` | Deploy while Rocket League runs. The loaded DLL is renamed to `.old`. Run `plugin unload rewardtesterplugin` and `plugin load rewardtesterplugin` in the F6 console afterwards |
| `-DataFiles` | Files to copy into BakkesMod's `data` folder, for example your reward config and task list |
| `-Config` | Build configuration. Default `Release` |

Without `-Force` the script refuses to deploy while the game runs, because the game keeps the DLL locked.

Load the plugin from the BakkesMod console (F6) with `plugin load rewardtesterplugin`, or add that line to `%APPDATA%\bakkesmod\bakkesmod\cfg\plugins.cfg`.

***

## Opening the Overlay

1.  Start `reward_server.exe` ([Server](SERVER.md)).
2.  Start Rocket League and enter Freeplay, or open a saved replay.
3.  Press F2, open **Plugins**, **Reward Tester**, and open the inspector window. The `rewardtester_toggle_menu` keybind does the same.

The header is always visible. It shows the connection, the term count, the total reward and whether you are live or watching a replay. **Save last Ns** and **Reset state** sit right under it.

***

## Live Tab

The reward table and nothing else. Columns: Term, Weight, Raw, Weighted, Trend and Avg. Raw is the term's own signal, Weighted is Raw times Weight. A term with weight zero still shows its raw value, so you can see whether it fires before it pays. Trend is the last five seconds of the raw value.

*   **Filter terms** and **active only** shorten a long list.
*   **Setup** (collapsed): training step, force late-schedule step, debug logging, and the window of the Avg column. At step 0 most scheduled terms read zero.

***

## Shots Tab

The shot list is on the left, the active shot on the right.

| Control | What it does |
|---|---|
| Save shot `[X]` | Saves the rolling window as the next attempt. It's unmarked until you mark it |
| Scored `[NumPad *]` / Miss `[NumPad /]` | Sets the outcome of the latest attempt. Pressing the other one changes it |
| Undo `[NumPad .]` | Takes the latest attempt out of the counters and keeps its clip as `_undone` |
| Mark a goal as scored automatically | A goal within ten seconds after a save, or a save within twenty seconds after a goal, marks the attempt scored |
| Go to the next shot after the last attempt | Moves on once the target attempts are done and marked |
| Save only my car | Smaller clips: the ball and one car |

Every key shows a one-line toast with what it did. **Task file** (collapsed) loads another list or zeroes the counters.

***

## Clips Tab

*   **Left:** the clip browser. Search, filter (all, shot attempts, generic clips) and a folder tree. A search or a filter turns the tree into a flat list.
*   **Right:** the selected clip. Play or pause `[P]`, restart `[Z]`, stop `[Esc]`, a scrub bar, frame step buttons, ten-frame jumps and speed presets. **Left** and **Right** step frames, hold to keep stepping, hold Shift for ten frames. **Playback mode** has the teleport and re-simulate options. **File actions** creates folders and renames or moves a clip.

Selecting a clip plays it in the game.

***

## Watch Tab

Open a saved replay in the game and follow a player with the replay camera. The overlay follows the same car:

*   Rewards are evaluated for the car you follow. Its row set is the same as live play.
*   **Save clip: whole game state** keeps every car and the ball.
*   **Save clip: followed car only** keeps the ball and the followed car.
*   The tab shows who is followed, the replay frame and the number of samples sent. If it shows no player followed, choose one with the replay camera.

Replays carry no controller inputs, so input-based terms read zero while you watch. Scrubbing or skipping clears the server's buffer, so a clip never contains a jump in time.

This hasn't been tested against a real replay yet. If the Watch tab stays at zero samples, the game doesn't expose the replay state the way the SDK says it does.

***

## Settings Tab

*   **Connection:** host, port, config and reconnect. An empty config means the config the server started with.
*   **Clips and input:** clip length (1 to 300 s), the three controller sliders, save only my car.
*   **Game speed:** sets `sv_soccar_gamespeed`. Freeplay and training only.
*   **Alerts:** a toast when a term's raw value goes above or below a limit. Alerts last until the game closes.
*   **Keybinds:** **Apply all keybinds** rebinds every key below. NumPad . is BakkesMod's `Decimal` key.

***

## Keybinds and Console Commands

| Key | Command | What it does |
|---|---|---|
| NumPad 0 | `rewardtester_toggle_menu` | Shows or hides the inspector window |
| X | `rewardtester_save_task_clip` | Saves the rolling window as the next attempt of the active shot |
| NumPad * | `rewardtester_mark_scored` | Marks the latest attempt as scored. Changes it if it was a miss |
| NumPad / | `rewardtester_mark_miss` | Marks the latest attempt as missed. Changes it if it was scored |
| NumPad . | `rewardtester_undo_attempt` | Takes the latest attempt out of the counters |
| NumPad + and - | `rewardtester_next_task`, `rewardtester_prev_task` | Changes the active shot |
| C | `rewardtester_save_clip [name]` | Saves a generic clip |
| console | `rewardtester_reload` | Reloads the reward config on the server |
| console | `rewardtester_reset` | Clears per-episode reward state on the server |
| console | `rewardtester_autosave [seconds]` | Saves a clip every N seconds of physics. 13 is the maximum, 0 turns it off |
| console | `rewardtester_probe_start [seed] [episodeSeconds] [saveEvery]`, `rewardtester_probe_stop` | Automated physics probe episodes. Developer tool |

***

## Files

| Location | What is in it |
|---|---|
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips\generic\` | Generic clips |
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips\<tasklist>\<shot id>\` | Shot attempts, `attempt_<N>.rlpr` |
| `<clip>.rlpr.json` | Sidecar: contact source, frame interval, whether it came from a replay, whether only one car was kept |
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_tester.ini` | Clip length and input slider preferences |

Something not working? The [FAQ](FAQ.md) covers the usual causes.
