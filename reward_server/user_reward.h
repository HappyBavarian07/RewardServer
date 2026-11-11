#pragma once
#include <functional>
#include <memory>
#include <string>
#include "reward_source.h"

namespace rs {

std::unique_ptr<IRewardSource> CreateUserReward(
    const std::string& configPath,
    std::function<uint64_t()> getTimestep);

std::unique_ptr<IEvalExtension> CreateEvalExtension();

}
