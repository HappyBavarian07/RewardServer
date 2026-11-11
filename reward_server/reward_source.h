#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "RLGymCPP/Gamestates/GameState.h"

namespace rs {

/** One evaluation of every reward row for the focus car; the vectors are index-aligned with `names`. */
struct RewardTerms {
    std::vector<std::string> names;
    /** Raw per-reward signal, independent of its weight. */
    std::vector<float> values;
    /** Effective schedule weight; may be empty when the source cannot report one. */
    std::vector<float> weights;
    /** values[i] * weights[i]. */
    std::vector<float> weighted;
    /** Sum of `weighted`. */
    float total = 0.0f;
};

/** Reward-agnostic view the server evaluates each step; any reward implementation can back it. */
class IRewardSource {
public:
    virtual ~IRewardSource() = default;
    virtual const std::vector<std::string>& Names() const = 0;
    /** Clears per-episode reward state. */
    virtual void Reset(const RLGC::GameState& initialState) { (void)initialState; }
    /** Called once per tick before Evaluate, for rewards that track state. */
    virtual void PreStep(const RLGC::GameState& state) = 0;
    /** `focusPlayer` selects which car's breakdown and total to report. */
    virtual void Evaluate(const RLGC::GameState& state, int focusPlayer, RewardTerms& out) = 0;
    virtual void SetDebug(bool enabled) { (void)enabled; }
};

/**
 * Optional hooks around every evaluation, for integrations that derive extra per-tick data (event
 * labels, caches) from the same states the reward sees. The server never inspects what they do.
 */
class IEvalExtension {
public:
    virtual ~IEvalExtension() = default;
    /** A reward source was (re)loaded. */
    virtual void OnLoad(IRewardSource& source) { (void)source; }
    /** Per-episode state must be cleared. */
    virtual void OnReset() {}
    /** Runs after state reconstruction and before the reward's PreStep; may attach `gs.userInfo`. */
    virtual void BeforeEvaluate(RLGC::GameState& gs) { (void)gs; }
    /** Runs after the reward evaluated `gs`. */
    virtual void AfterEvaluate(const RLGC::GameState& gs) { (void)gs; }
    /** Extra fields merged into each tick object of a `--json` replay with `--extra`. */
    virtual nlohmann::json TickReport(int focus) const { (void)focus; return nlohmann::json::object(); }
    /** Extra fields merged into the summary object of a `--json` replay with `--extra`. */
    virtual nlohmann::json ReplayReport() const { return nlohmann::json::object(); }
};

}
