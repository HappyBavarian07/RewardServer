---
description: Build variables, command line, task files, clips and offline replay
---

# Server

`reward_server.exe` evaluates your reward for every state the plugin sends, keeps a rolling window of ticks, writes clips, owns the shot task list and replays clips offline. It listens on `127.0.0.1` only. For the reward itself see [Plugging in your reward](INTEGRATION.md).

***

## Build Variables

Set these as normal variables before `add_subdirectory(RewardServer/reward_server)`. The framework target has to exist already.

| Variable | Default | What it does |
|---|---|---|
| `RS_FRAMEWORK_ROOT` | required | GigaLearnCPP checkout: `RLGymCPP/src`, `RLGymCPP/RocketSim/src`, `libsrc/json` |
| `RS_FRAMEWORK_TARGET` | `GigaLearnCPP` | library target that provides the framework sources |
| `RS_INTEGRATION_SOURCES` | `examples/user_reward_example.cpp` | sources defining `rs::CreateUserReward` and `rs::CreateEvalExtension` |
| `RS_EXTRA_SOURCES` | empty | more sources, for example your reward classes |
| `RS_EXTRA_INCLUDES` | empty | more include directories |
| `RS_EXTRA_DEFINITIONS` | empty | more compile definitions |
| `RS_EXTRA_LIBRARIES` | empty | more link libraries |

***

## Command Line

```text
reward_server.exe [--port 42069] [--config <path>] [--tasks <file>] [--meshes <dir>] [--clips <dir>] [--clip-seconds 15]
reward_server.exe --config <path> --replay <clip|dir> [replay options]
reward_server.exe --self-test-timing
reward_server.exe --help
```

| Option | Default | What it does |
|---|---|---|
| `--port` | `42069` | TCP port on `127.0.0.1` |
| `--config` | none | reward config to load at start |
| `--tasks` | none | shot task file. Without it the Shots tab is empty |
| `--meshes` | `collision_meshes` | collision meshes for the arena mirror |
| `--clips` | `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips` | clip folder |
| `--clip-seconds` | `15` | rolling clip window. The overlay can change it (1 to 300) |

Start the server from the folder that contains `collision_meshes`, or pass `--meshes`. Without meshes the arena mirror is off and contact, flip and ball-hit fields keep their defaults. Unknown options are an error.

***

## Shot Task Files

An array of task objects, or an object with `id`, `clipFolder` and `tasks`. The server writes the counters back after every attempt or reset, through a temporary file and a rename.

| Field | What it does |
|---|---|
| `id`, `name` | identify the task |
| `tag` | used in clip names |
| `instructions`, `expectedMode` | text shown in the overlay |
| `targetAttempts` | attempts you want. Default 3 |
| `completedAttempts`, `successfulAttempts` | counters. Zero them before handing a list on |

`examples/example_tasks.json` is a template.

***

## Clips

The server buffers every evaluated tick, so you can save after the moment passed.

| Save | Path |
|---|---|
| generic clip | `<clips>/generic/clip_<timestamp>.rlpr` |
| shot attempt | `<clips>/<tasklist>/<shot id>/attempt_<N>.rlpr` |

Next to each clip is `<clip>.rlpr.json` with the contact source, the frame interval, whether it came from a replay and whether only one car was kept. Folder, rename and move commands only accept paths inside the clip folder.

***

## Offline Replay

`--replay` runs a clip through the loaded reward and prints the weighted terms of one car. A folder replays its clips in name order. It needs `--config`.

```powershell
reward_server.exe --config reward.json --replay clip.rlpr --focus 0 --terms Speed,Touch
```

| Option | Default | What it does |
|---|---|---|
| `--focus` | `0` | car index to report |
| `--terms` | all | only terms whose name contains one of these |
| `--replay-tick-skip` | `8` | evaluation interval in physics ticks |
| `--replay-frame-offset` | `0` | shifts the evaluation grid inside one interval |
| `--trust-touch` | off | use the recorded contact flags |
| `--json` | off | one JSON object with per-tick data and totals |
| `--extra` | off | add your extension's reports to the JSON. `--labels` is an alias |

Contacts are reconstructed from the preceding interval, not read from the game. The JSON says so in `contact_source` and `contact_is_native_rl_truth: false`. `--self-test-timing` compares reconstructed contacts with the simulator's own on a fixed fixture and exits nonzero on a mismatch.

***

## Protocol Check

```powershell
python tests/smoke_client.py --config reward.json --tasks
```

It loads the config, evaluates a canned state, runs the clip and task commands and prints `SMOKE OK`. All commands are in [Protocol](PROTOCOL.md).
