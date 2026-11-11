---
description: Build, install and use the BakkesMod overlay
---

# Plugin

`RewardTesterPlugin.dll` reads the cars and the ball and sends them to `reward_server` from a worker thread. The game thread never waits on the network. A stuck server shows up as `DISCONNECTED`, not as a frozen game.

***

## Building and Installing

You need Windows, Rocket League with [BakkesMod](https://bakkesmod.com/) (start it once to unpack the SDK), Visual Studio with the C++ workload, CMake and a GigaLearnCPP checkout.

```powershell
scripts\build_plugin.ps1 -FrameworkRoot <path to GigaLearnCPP> [-NoDeploy] [-Force] [-DataFiles a.json,b.json]
```

| Parameter | What it does |
|---|---|
| `-FrameworkRoot` | GigaLearnCPP checkout. Required |
| `-NoDeploy` | build only |
| `-Force` | deploy while the game runs. The loaded DLL is renamed to `.old`. Then run `plugin unload rewardtesterplugin` and `plugin load rewardtesterplugin` in the F6 console |
| `-DataFiles` | files to copy into BakkesMod's `data` folder |
| `-Config` | build configuration. Default `Release` |

Without `-Force` the script refuses to deploy while the game runs. To load the plugin at startup, add `plugin load rewardtesterplugin` to `%APPDATA%\bakkesmod\bakkesmod\cfg\plugins.cfg`.

Start the server first ([Server](SERVER.md)), then the game. Press F2, open Plugins, Reward Tester. `rewardtester_toggle_menu` toggles the window.

***

## Header

Always visible: connection, term count, total reward, and live or replay. **Save last Ns** and **Reset state** sit below it.

***

## Live Tab

The reward table: Term, Weight, Raw, Weighted, Trend and Avg. Raw is the term's own signal, Weighted is Raw times Weight. A term with weight zero still shows its raw value. Trend is the last five seconds.

*   **Filter** and **active only** shorten a long list.
*   **Setup** (collapsed) has the training step, force late-schedule step, debug logging and the Avg window. At step 0 most scheduled terms read zero.

***

## Shots Tab

Shot list on the left, active shot on the right.

| Control | What it does |
|---|---|
| Save shot `[X]` | saves the rolling window as the next attempt, unmarked |
| Scored `[NumPad *]`, Miss `[NumPad /]` | sets the outcome of the latest attempt. The other key changes it |
| Undo `[NumPad .]` | removes the latest attempt and keeps its clip as `_undone` |
| Mark a goal as scored automatically | a goal within 10 s after a save, or a save within 20 s after a goal, marks it scored |
| Go to the next shot after the last attempt | moves on once the target is done and marked |
| Save only my car | clips keep the ball and one car |

Every key shows a toast with what it did. **Task file** (collapsed) loads another list or zeroes the counters.

***

## Clips Tab

*   **Left:** search, filter (all, shot attempts, generic) and a folder tree. A search or filter shows a flat list.
*   **Right:** play or pause `[P]`, restart `[Z]`, stop `[Esc]`, scrub bar, frame and ten-frame steps, speed presets. Left and Right arrows step frames. Hold to repeat, hold Shift for ten frames. **File actions** creates folders and renames or moves clips.

Selecting a clip plays it in the game.

***

## Watch Tab

Open a saved replay and follow a player with the replay camera. The overlay follows the same car and evaluates rewards for it. Two clip buttons: the whole game state, or the followed car and the ball only. The tab shows who is followed, the replay frame and how many samples were sent.

Replays have no controller inputs, so input-based terms read zero. Skipping or scrubbing clears the server's buffer, so a clip never jumps in time. This is not tested against enough replays yet.

***

## Settings Tab

*   **Connection:** host, port, config. An empty config means the one the server started with.
*   **Clips and input:** clip length (1 to 300 s), controller sliders, save only my car.
*   **Game speed:** sets `sv_soccar_gamespeed`. Freeplay and training only.
*   **Alerts:** a toast when a term's raw value crosses a limit. Not saved between sessions.
*   **Keybinds:** **Apply all keybinds** rebinds the keys below.

***

## Keybinds and Console Commands

| Key | Command | What it does |
|---|---|---|
| NumPad 0 | `rewardtester_toggle_menu` | show or hide the window |
| X | `rewardtester_save_task_clip` | save the shot as the next attempt |
| NumPad * | `rewardtester_mark_scored` | mark the latest attempt scored |
| NumPad / | `rewardtester_mark_miss` | mark the latest attempt missed |
| NumPad . | `rewardtester_undo_attempt` | remove the latest attempt (BakkesMod key `Decimal`) |
| NumPad + and - | `rewardtester_next_task`, `rewardtester_prev_task` | change shot |
| C | `rewardtester_save_clip [name]` | save a generic clip |
| console | `rewardtester_reload` | reload the reward config on the server |
| console | `rewardtester_reset` | clear per-episode reward state |
| console | `rewardtester_autosave [seconds]` | save a clip every N seconds of physics. Max 13, 0 is off |
| console | `rewardtester_probe_start [seed] [episodeSeconds] [saveEvery]`, `rewardtester_probe_stop` | physics probe episodes. Developer tool |

***

## Files

| Location | Content |
|---|---|
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips\generic\` | generic clips |
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips\<tasklist>\<shot id>\` | shot attempts |
| `<clip>.rlpr.json` | sidecar: contact source, frame interval, source, cars kept |
| `%APPDATA%\bakkesmod\bakkesmod\data\reward_tester.ini` | clip length and input sliders |

During clip playback the plugin logs a `[replay-diag]` line every second to the BakkesMod log, and a `[watch-diag]` line while watching a replay.
