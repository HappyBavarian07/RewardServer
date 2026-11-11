#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "RLGymCPP/Gamestates/GameState.h"

namespace rs {

struct RewardTerms {
    std::vector<std::string> names;
    std::vector<float> values;
    std::vector<float> weights;
    std::vector<float> weighted;
    float total = 0.0f;
};

class IRewardSource {
public:
    virtual ~IRewardSource() = default;
    virtual const std::vector<std::string>& Names() const = 0;
    virtual void Reset(const RLGC::GameState& initialState) { (void)initialState; }
    virtual void PreStep(const RLGC::GameState& state) = 0;
    virtual void Evaluate(const RLGC::GameState& state, int focusPlayer, RewardTerms& out) = 0;
    virtual void SetDebug(bool enabled) { (void)enabled; }
};

class IEvalExtension {
public:
    virtual ~IEvalExtension() = default;
    virtual void OnLoad(IRewardSource& source) { (void)source; }
    virtual void OnReset() {}
    virtual void BeforeEvaluate(RLGC::GameState& gs) { (void)gs; }
    virtual void AfterEvaluate(const RLGC::GameState& gs) { (void)gs; }
    virtual nlohmann::json TickReport(int focus) const { (void)focus; return nlohmann::json::object(); }
    virtual nlohmann::json ReplayReport() const { return nlohmann::json::object(); }
};

}
