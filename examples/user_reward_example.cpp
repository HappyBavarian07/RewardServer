#include "user_reward.h"
#include <cmath>
#include <utility>

namespace {
using namespace RLGC;

/**
 * Example source with two hand-written terms. Replace the body of Evaluate with calls into your own
 * reward classes: one row per reward, e.g. out.values[i] = myReward.GetReward(player, state, false).
 */
class ExampleSource : public rs::IRewardSource {
public:
    explicit ExampleSource(std::function<uint64_t()> getTimestep) : getStep_(std::move(getTimestep)) {}

    const std::vector<std::string>& Names() const override { return names_; }

    void PreStep(const GameState& state) override { (void)state; }

    void Evaluate(const GameState& state, int focusPlayer, rs::RewardTerms& out) override {
        out.names = names_;
        out.values.assign(names_.size(), 0.0f);
        out.weights = weights_;
        out.weighted.assign(names_.size(), 0.0f);
        out.total = 0.0f;
        if (state.players.empty()) return;

        const int f = (focusPlayer >= 0 && focusPlayer < (int)state.players.size()) ? focusPlayer : 0;
        const Player& p = state.players[f];

        Vec toBall = state.ball.pos - p.pos;
        const float dist = toBall.Length();
        float velToBall = 0.0f;
        if (dist > 1.0f) velToBall = p.vel.Dot(toBall / dist) / 2300.0f;

        out.values[0] = velToBall;
        out.values[1] = p.ballTouchedStep ? 1.0f : 0.0f;

        for (size_t i = 0; i < names_.size(); ++i) {
            out.weighted[i] = out.values[i] * weights_[i];
            out.total += out.weighted[i];
        }
    }

private:
    std::function<uint64_t()> getStep_;
    std::vector<std::string> names_ = {"VelocityToBall", "TouchBall"};
    std::vector<float> weights_ = {1.0f, 5.0f};
};
}

namespace rs {

std::unique_ptr<IRewardSource> CreateUserReward(const std::string& configPath,
                                                std::function<uint64_t()> getTimestep) {
    (void)configPath;
    return std::make_unique<ExampleSource>(std::move(getTimestep));
}

std::unique_ptr<IEvalExtension> CreateEvalExtension() { return nullptr; }

}
