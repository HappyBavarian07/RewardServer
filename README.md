# RewardServer

Tuning a reward by training and hoping is slow. RewardServer lets you drive the mechanic yourself in Rocket League freeplay and watch every reward term fire while you do it.

A BakkesMod plugin sends your car and the ball to a small local server. The server runs your reward code and sends back one row per term: raw value, weight, weighted value and a rolling average. You can also:

*   **Save clips**: keep the last seconds of play as an `.rlpr` file, after the interesting moment already happened.
*   **Work through shot lists**: tag clips against a list of test shots and count attempts, scored and missed.
*   **Replay clips**: play a saved clip back in the game, frame by frame.
*   **Audit offline**: run any clip through the same reward code from the command line and get per-term totals.

RewardServer has no training code in it. It needs a GigaLearnCPP checkout for the game state and RocketSim, the BakkesMod SDK and MSVC. Your reward plugs in through one source file.

***

## Getting Started

1.  Add the server to a CMake project that already has the `GigaLearnCPP` target:

    ```cmake
    set(RS_FRAMEWORK_ROOT "${CMAKE_SOURCE_DIR}/GigaLearnCPP")
    add_subdirectory(RewardServer/reward_server)
    ```

2.  Replace `examples/user_reward_example.cpp` with your own reward ([Plugging in your reward](docs/INTEGRATION.md)), or build with the example to see the plumbing work.
3.  Build and install the plugin with Rocket League closed:

    ```powershell
    scripts\build_plugin.ps1 -FrameworkRoot <path to GigaLearnCPP>
    ```

4.  Start the server from the folder that contains `collision_meshes`, then start the game and open the overlay from F2, Plugins, Reward Tester:

    ```powershell
    reward_server.exe --config reward.json --tasks examples/example_tasks.json
    ```

The title line of the overlay reads `CONNECTED` once it found the server. If it doesn't, the [FAQ](docs/FAQ.md) has the usual causes.

***

## Where to Go Next

*   [Plugging in your reward](docs/INTEGRATION.md) - Write the two functions the server calls, build it, check it works.
*   [Server](docs/SERVER.md) - Command line, build variables, task files, clips, offline replay.
*   [Plugin](docs/PLUGIN.md) - Overlay tabs, keybinds, console commands, building and deploying.
*   [Protocol](docs/PROTOCOL.md) - Every TCP command, so you can write your own client.
*   [Architecture](docs/ARCHITECTURE.md) - What runs where, and what the arena mirror does.
*   [FAQ](docs/FAQ.md) - The things that go wrong.
*   [Roadmap](docs/ROADMAP.md) - Known limits and what I'd build next.
*   [Bundle](bundle/README.md) - The hand-out for someone who only wants the prebuilt plugin.

***

## Layout

| Folder | What is in it |
|---|---|
| `reward_server/` | The TCP server: session, evaluation, clip buffer, tasks, offline replay |
| `BakkesModPlugin/` | The plugin: overlay, keybinds, clip replay |
| `rlpr/` | `.rlpr` reader and writer, used by both |
| `examples/` | A two-term integration and a task list template |
| `bundle/` | Hand-out text for people using the prebuilt plugin |
| `scripts/` | Plugin build and deploy, bundle packaging |
| `tests/` | Protocol smoke test for a running server |
