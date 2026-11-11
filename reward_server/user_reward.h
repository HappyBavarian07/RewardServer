#pragma once
#include <functional>
#include <memory>
#include <string>
#include "reward_source.h"

namespace rs {

/**
 * The single extension point of the reward server. Return the reward your training uses.
 * `getTimestep` is polled on every evaluation so schedule-fading rewards advance; the server drives it
 * from the training-step value the plugin sends.
 */
std::unique_ptr<IRewardSource> CreateUserReward(
    const std::string& configPath,
    std::function<uint64_t()> getTimestep);

/** Optional evaluation hooks; return null when the integration has none. Defined next to CreateUserReward. */
std::unique_ptr<IEvalExtension> CreateEvalExtension();

}
