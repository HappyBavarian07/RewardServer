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

int ReplayClip(RewardEval& eval, const std::string& path, const ReplayOptions& options);

int ReplayPaths(RewardEval& eval, const std::vector<std::string>& paths, const ReplayOptions& options);

int CheckReconstructionTiming(RewardEval& eval);

}
