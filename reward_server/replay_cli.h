#pragma once
#include <string>
#include <vector>
#include "reward_eval.h"

namespace rs {

struct ReplayOptions {
    int focus = 0;
    int tickSkip = 8;
    int frameOffset = 0;
    bool trustTouch = false;
    bool json = false;
    bool extra = false;
    std::vector<std::string> termFilter;
};

/**
 * Feeds every evaluated tick of one .rlpr clip through the loaded reward like a live session
 * (mirror step, PreStep, Evaluate with debug logging on) and reports the focus car's weighted terms.
 * Ball touches come from the arena mirror unless `trustTouch` is set. Returns a process exit code.
 */
int ReplayClip(RewardEval& eval, const std::string& path, const ReplayOptions& options);

/** Replays each path; a directory expands to its .rlpr files in name order. */
int ReplayPaths(RewardEval& eval, const std::vector<std::string>& paths, const ReplayOptions& options);

/** Compares inferred ball contacts with the simulator's native contacts on a fixed fixture. */
int CheckReconstructionTiming(RewardEval& eval);

}
