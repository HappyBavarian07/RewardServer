#pragma once
#include <nlohmann/json.hpp>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "reward_source.h"
#include "RLGymCPP/Gamestates/GameState.h"

namespace RocketSim { class Arena; class Car; struct GameEventTracker; }

namespace rs {

/**
 * Owns the live reward source and the rolling previous game state so deltas and schedule fading
 * behave exactly as in training. One instance per server.
 */
class RewardEval {
public:
    ~RewardEval();

    /** Only enable after RocketSim::Init succeeded; Arena::Create aborts without collision meshes. */
    void SetMirrorEnabled(bool on) { mirrorEnabled_ = on; }
    bool MirrorEnabled() const { return mirrorEnabled_; }

    bool Load(const std::string& configPath, std::string& err);
    bool Ready() const { return reward_ != nullptr; }
    int Count() const { return (int)names_.size(); }
    const std::vector<std::string>& Names() const { return names_; }

    /** Clears all per-player reward state; call on every episode reset (goal, freeplay reset). */
    void Reset();

    /** Parses one eval request into a GameState and fills the per-term output; false if it has no cars. */
    bool Evaluate(const nlohmann::json& req, RewardTerms& out);

    /** Buffers one tick for the rolling clip window without evaluating any reward. */
    bool Capture(const nlohmann::json& req);

    /**
     * Evaluates an already-built GameState (kinematics, flags, controls) exactly like the JSON path: arena mirror,
     * extension hooks, PreStep and Evaluate. `gs` is overlaid with the mirror-derived fields and retained as the
     * next tick's previous state.
     */
    bool EvaluateState(RLGC::GameState& gs, int tickSkip, int focus, bool debug, RewardTerms& out,
                       bool reconstruct = true);

    /** Infers contacts for the observed preceding interval only; they are evidence, never native RL contacts. */
    void ReconstructState(RLGC::GameState& gs, const RLGC::GameState* previous, int tickSkip);

    void SetExtension(std::unique_ptr<IEvalExtension> extension) { extension_ = std::move(extension); }
    const IEvalExtension* Extension() const { return extension_.get(); }

    /** Training step the schedule is evaluated at for EvaluateState; the JSON path takes it from the request. */
    void SetStep(uint64_t step) { currentStep_ = step; }

    void SetClipFolder(const std::string& folder) { clipFolder_ = folder; }
    const std::string& ClipFolder() const { return clipFolder_; }
    void SetClipWindowSeconds(float s) { clipWindowSeconds_ = std::max(1.0f, s); }
    float ClipWindowSeconds() const { return clipWindowSeconds_; }

    /// Write the buffered window to <clipFolder>/<subfolder>/<name>.rlpr. Empty name auto-timestamps.
    /// Returns the written path in outPath, or false plus a reason in err.
    bool SaveClip(const std::string& name, std::string& outPath, std::string& err, const std::string& subfolder = "generic");

    /// .rlpr files currently present in the clip folder, newest first.
    std::vector<std::string> ListClips() const;

    int BufferedFrames() const {
        return captureBuffer_.empty() ? (int)clipBuffer_.size() : (int)captureBuffer_.size();
    }

    struct ShotTask {
        std::string id;
        std::string name;
        std::string instructions;
        std::string expectedMode;
        std::string tag;
        int targetAttempts = 3;
        int completedAttempts = 0;
        int successfulAttempts = 0;
    };

    void SetTasksPath(const std::string& path) { tasksPath_ = path; }
    const std::string& TasksPath() const { return tasksPath_; }
    void SetTasklistId(const std::string& id) { tasklistId_ = id; }
    const std::string& TasklistId() const { return tasklistId_; }
    bool LoadTasks(const std::string& path = "");
    bool SaveTasks(const std::string& path = "");
    nlohmann::json GetTasksJson() const;
    bool SetActiveTask(int index);
    bool LogTaskAttempt(bool scored);
    bool ResetTask(int index);
    bool SaveTaskClip(std::string& outPath, std::string& err, const std::string& overrideSubfolder = "");
    int ActiveTaskIndex() const { return activeTaskIndex_; }

private:
    bool BuildGameState(const nlohmann::json& req, RLGC::GameState& gs) const;

    /**
     * Mirrors the preceding observed kinematics into a persistent arena, steps it tickSkip ticks with the
     * preceding controls, and overlays the simulator-derived fields BakkesMod cannot supply (contact, wheels,
     * flip/jump state, ball-hit info, goal/shot/save/bump/demo events, goalScored) onto the GameState.
     */
    void MirrorArena(RLGC::GameState& gs, const RLGC::GameState& previous, int tickSkip);
    int MirrorIndexOf(RocketSim::Car* car) const;
    void RecordGoal(RocketSim::Car* scorer, RocketSim::Car* passer);
    void RecordShot(RocketSim::Car* shooter, RocketSim::Car* passer);
    void RecordSave(RocketSim::Car* saver);
    void RecordBump(RocketSim::Car* bumper, RocketSim::Car* victim, bool isDemo);

    struct PendingEvent {
        bool goal = false, assist = false, shot = false, shotPass = false, save = false;
        bool bump = false, bumped = false, demo = false, demoed = false;
    };

    std::unique_ptr<IRewardSource> reward_;
    std::unique_ptr<IEvalExtension> extension_;
    std::vector<std::string> names_;
    uint64_t currentStep_ = 0;
    RLGC::GameState prev_;
    bool havePrev_ = false;

    bool mirrorEnabled_ = false;
    RocketSim::Arena* arena_ = nullptr;
    RocketSim::GameEventTracker* tracker_ = nullptr;
    std::vector<RocketSim::Car*> mirrorCars_;
    std::vector<PendingEvent> pendingEvents_;
    std::vector<int> padIndexMap_;
    uint64_t tickCounter_ = 0;

    std::deque<RLGC::GameState> clipBuffer_;
    std::deque<RLGC::GameState> captureBuffer_;
    std::unique_ptr<RewardEval> captureMirror_;
    bool haveCapturePrev_ = false;
    std::string clipFolder_;
    float clipWindowSeconds_ = 15.0f;
    uint64_t captureTickCounter_ = 0;

    std::vector<ShotTask> tasks_;
    int activeTaskIndex_ = 0;
    std::string tasksPath_ = "data/shot_tasks.json";
    std::string tasklistId_;
    bool tasksWereObjectFormat_ = false;
    bool lastActionWasSaveClip_ = false;
};

}
