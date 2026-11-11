---
description: Build variables, command line, task files, clips and offline replay of reward_server
---

# Server

`reward_server.exe` evaluates your reward for every game state the plugin sends. It also keeps a rolling window of ticks, writes clips, owns the shot task list and replays clips offline. It listens on `127.0.0.1` only.

To get your own reward in, read [Plugging in your reward](INTEGRATION.md) first. This page is the reference.

***

## Build Variables

The server needs a CMake project that already defines the framework library target. Set the variables as normal variables before `add_subdirectory(RewardServer/reward_server)`.

| Variable | Default | What it does |
|---|---|---|
| `RS_FRAMEWORK_ROOT` | none, required | Root of a GigaLearnCPP checkout. Needs `RLGymCPP/src`, `RLGymCPP/RocketSim/src` and `libsrc/json` |
| `RS_FRAMEWORK_TARGET` | `GigaLearnCPP` | Library target that provides the framework sources |
| `RS_INTEGRATION_SOURCES` | `examples/user_reward_example.cpp` | Sources that define `rs::CreateUserReward` and `rs::CreateEvalExtension` |
| `RS_EXTRA_SOURCES` | empty | More sources, for example your reward classes |
| `RS_EXTRA_INCLUDES` | empty | More include directories |
| `RS_EXTRA_DEFINITIONS` | empty | More compile definitions |
| `RS_EXTRA_LIBRARIES` | empty | More link libraries |

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
| `--port` | `42069` | TCP port on `127.0.0.1`. The overlay's port field has to match |
| `--config` | none | Reward config to load at start. The overlay can also load one later |
| `--tasks` | none | Shot task file. Without it the Shot Tasks tab stays empty |
| `--meshes` | `collision_meshes` | Folder with the RocketSim collision meshes |
| `--clips` | `%APPDATA%\bakkesmod\bakkesmod\data\reward_clips` | Where clips are written |
| `--clip-seconds` | `15` | Length of the rolling clip window. The overlay can change it (1 to 300) |

Start the server from the folder that contains `collision_meshes`, or pass `--meshes`. Without meshes the arena mirror is off, and contact, flip and ball-hit fields keep their defaults. Unknown options are an error.

***

## Shot Task Files

A task file is an array of task objects, or an object with `id`, `clipFolder` and `tasks`. The server writes the counters back after every logged attempt or reset. It writes to a temporary file first and renames it, so a crash doesn't cost you the list.

| Field | What it does |
|---|---|
| `id`, `name` | Identify the task in the overlay |
| `tag` | Used in clip file names |
| `instructions`, `expectedMode` | Free text the overlay shows |
| `targetAttempts` | Attempts you want. Default 3 |
| `completedAttempts`, `successfulAttempts` | Counters. Zero them before you hand a list to someone else |

`examples/example_tasks.json` is a template.

***

## Clips

The server buffers every evaluated tick. A save writes the buffer as `.rlpr`, so you can save after the interesting moment already happened.

| Save | Path |
|---|---|
| Generic clip | `<clips>/generic/clip_<timestamp>.rlpr` |
| Task clip | `<clips>/tasks/<tag>/<tag>_attempt<N>.rlpr` |

Folder, rename and move commands only accept paths inside the clip folder. Absolute paths and `..` are refused.

***

## Offline Replay

`--replay` runs every evaluated tick of a clip through the loaded reward the way a live session does, and prints the weighted terms of one car. A folder replays its `.rlpr` files in name order. It needs `--config`.

```powershell
reward_server.exe --config reward.json --replay clip.rlpr --focus 0 --terms Speed,Touch
```

```text
[replay] clip.rlpr: 180 ticks, 1 cars, 12.0s
[replay] t= 4.20s carZ= 742 ballZ= 810 touch=1 flip=0  Touch=1.500
[replay] totals:  Touch=1.50
```

| Option | Default | What it does |
|---|---|---|
| `--focus` | `0` | Car index to report |
| `--terms` | all | Comma list. Only terms whose name contains one of these are printed |
| `--replay-tick-skip` | `8` | Evaluation interval in physics ticks |
| `--replay-frame-offset` | `0` | Shifts the evaluation grid inside one interval. Must be smaller than the stride |
| `--trust-touch` | off | Use the recorded contact flags instead of reconstructing them |
| `--json` | off | One JSON object with per-tick data and totals |
| `--extra` | off | Adds your extension's `TickReport` and `ReplayReport` to the JSON. `--labels` is an alias |

Contacts are reconstructed from the preceding interval. They are evidence, not native game contacts, and the JSON says so: `contact_source` names the method and `contact_is_native_rl_truth` is `false`.

`--self-test-timing` compares reconstructed contacts with the simulator's native contacts on a fixed fixture and returns a nonzero exit code on a mismatch.

***

## Protocol Check

```powershell
python tests/smoke_client.py --config reward.json --tasks
```

Run it against a running server. It loads the config, evaluates a canned state, exercises the clip and task commands, and prints `SMOKE OK`. Every command is in [Protocol](PROTOCOL.md).
