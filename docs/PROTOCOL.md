---
description: Every TCP command the reward server understands, with request and reply fields
---

# Protocol

Newline-delimited JSON on `127.0.0.1:<port>`, default 42069. One request object per line, one reply object per line. Every reply has `"ok"`, and failures add `"error"`. A line longer than 16 MiB closes the connection. The server serves one client at a time.

Anything that speaks this can drive the server. `tests/smoke_client.py` is a minimal client. The protocol version is `1`, and `hello` lists every command the running server supports.

## Reward

| cmd | request fields | reply fields |
|---|---|---|
| `hello` | | `server`, `proto`, `commands` |
| `ping` | | |
| `load` | `config` | `count`, `names` |
| `reload` | | `count`, `names` (reuses the last `config`) |
| `eval` | see below | `total`, `names`, `values`, `weights`, `weighted` |
| `capture` | same state fields as `eval` | `frames` (buffers a tick without evaluating) |
| `reset` | | clears per-episode reward state |

### `eval` request

```json
{
  "cmd": "eval", "step": 0, "force_all": true, "tick_skip": 8, "focus": 0, "debug": false,
  "goal_scored": false, "last_touch_car": -1,
  "cars": [ { "...": "car fields" } ],
  "ball": { "pos": [0,0,93], "vel": [0,0,0], "ang_vel": [0,0,0], "rot_mat": [1,0,0, 0,1,0, 0,0,1] },
  "game": { "boost_pad_states": [1], "boost_pad_timers": [0.0] }
}
```

- `force_all` (default true) evaluates at a late training step so every scheduled term is fully weighted;
  set it false to evaluate at `step`.
- `tick_skip` is the number of physics ticks the previous state is advanced for (default 8, minimum 1).
- `focus` selects the car whose per-term breakdown is reported.
- `boost_pad_states` and `boost_pad_timers` hold 34 entries.
- Vectors are `[x, y, z]`; `rot_mat` is row-major with 9 entries.

Car fields (all optional, defaults are zero or identity):

| Field | Meaning |
|---|---|
| `pos`, `vel`, `ang_vel`, `rot_mat` | kinematics |
| `team` | 0 blue, 1 orange |
| `boost` | 0 to 100 |
| `is_on_ground`, `has_jumped`, `has_double_jumped`, `has_flipped`, `is_flipping`, `is_jumping`, `is_supersonic` | flags (0 or 1) |
| `jump_time`, `flip_time`, `air_time_since_jump`, `handbrake_val` | seconds or 0 to 1 |
| `flip_rel_torque` | vector |
| `world_contact_has`, `world_contact_normal` | surface contact |
| `prev_action` | object: `throttle`, `steer`, `pitch`, `yaw`, `roll`, `jump`, `boost`, `handbrake` |
| `is_demoed`, `demo_respawn_timer` | demolition state |
| `is_ball_touched` | the car touched the ball this tick |

## Clips

| cmd | request fields | reply fields |
|---|---|---|
| `save_clip` | `name`, `subfolder` (default `generic`) | `path`, `frames` |
| `list_clips` | | `folder`, `clips` |
| `clip_info` | | `folder`, `window_seconds`, `buffered_frames` |
| `set_clip_window` | `seconds` (minimum 1) | `window_seconds` |
| `set_clip_folder` | `folder` | `folder` |
| `create_clip_folder` | `path` (relative) | |
| `rename_clip_path` | `from`, `to` (relative) | |
| `move_clip` | `source`, `folder` (relative) | |

Paths in `create_clip_folder`, `rename_clip_path` and `move_clip` must be relative to the clip folder and may not
contain `..`; the server refuses anything else.

## Shot tasks

| cmd | request fields | reply fields |
|---|---|---|
| `get_tasks` | | `active_index`, `tasks`, `tasks_path` |
| `set_active_task` | `index` | `active_index`, `tasks` |
| `log_task_attempt` | `scored` | `active_index`, `tasks`, `message`, `last_attempt` |
| `undo_task_attempt` | | `active_index`, `tasks`, `message`, `last_attempt`. Fails with `message: "Nothing to undo"` |
| `reset_task` | `index` (-1 resets all) | `active_index`, `tasks` |
| `reload_tasks` | `path` | `active_index`, `tasks`, `tasks_path` |
| `save_task_clip` | `clip_folder`, `subfolder` | `path`, `active_index`, `tasks`, `message`, `last_attempt` |

Every task reply carries a `message`, one line the overlay shows as a toast. `last_attempt` has `task`, `number`, `outcome` (`none`, `unmarked`, `scored` or `miss`) and `clip`.

`save_task_clip` counts a new attempt and leaves it `unmarked`. `log_task_attempt` then sets the outcome of that attempt. Sending the other outcome changes it, and sending the same one again does nothing, so a wrong key never counts twice. With no attempt to mark, or more than 90 seconds after it, `log_task_attempt` logs a new attempt without a clip. `undo_task_attempt` takes the latest attempt out of the counters and renames its clip to `<name>_undone.rlpr`. Switching task, reloading the list or resetting counters ends the marking window.
