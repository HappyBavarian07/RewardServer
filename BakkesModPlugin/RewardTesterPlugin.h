#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/pluginsettingswindow.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <random>

#include "RewardClient.h"
#include "rlpr/RLPRExporter.h"

struct ClipGroupEntry {
    std::string group;
    std::vector<std::string> items;
    std::vector<ClipGroupEntry> children;
};

class RewardTesterPlugin : public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginSettingsWindow,
                           public BakkesMod::Plugin::PluginWindow {
public:
    virtual void onLoad() override;

    virtual void onUnload() override;

    virtual void RenderSettings() override;

    virtual std::string GetPluginName() override;

    virtual void Render() override;

    virtual std::string GetMenuName() override;

    virtual std::string GetMenuTitle() override;

    virtual bool ShouldBlockInput() override;

    virtual bool IsActiveOverlay() override;

    virtual void OnOpen() override;

    virtual void OnClose() override;

    virtual void SetImGuiContext(uintptr_t ctx) override;

private:
    void OnPhysicsTick(ServerWrapper server, void *params);

    void Reconnect();

    void ReloadConfig();

    void SendRewardReset();

    void SaveClip(const std::string &name = std::string());

    void RefreshClipList();

    void OpenReplayClip(const std::string &path);

    void StopReplayClip();

    void UpdateReplayPlayback(ServerWrapper server, float dt);

    void ApplyReplayFrame(ServerWrapper server, const std::string &path, float t);

    void BuildClipGroups(std::vector<ClipGroupEntry> &groups);

    bool LoadReplayFrames(const std::string &path);

    void ApplyReplayState(ServerWrapper server, const RLGC::GameState &state);

    void ApplyReplayInput(CarWrapper car, ControllerInput *input);

    void LoadPreferences();

    void SavePreferences();

    void HandleReplayKeys();

    bool replayActive = false;
    std::string activeReplayPath;
    float replayTime = 0.0f;
    float replayRate = 1.0f;
    bool funReplayMode = false;
    std::vector<RLGC::GameState> replayFrames;
    float replayFrameIntervalSeconds = 8.0f / 120.0f;
    std::string loadedReplayPath;
    size_t replayFrameIndex = 0;
    size_t replayCarCount = 0;
    std::vector<uintptr_t> replayCarAddresses;
    std::vector<size_t> replayCarStateIndices;
    bool replayNeedsReseed = true;
    bool replayStateDrivenFallback = false;
    bool replayInputDriven = false;
    bool replayLoop = true;
    /// Teleport car and ball to the recorded state every tick; inputs are applied only for visuals.
    bool ReplayStateDriven() const { return funReplayMode || replayStateDrivenFallback || !replayInputDriven; }
    float replayLastWorldTime = -1.0f;
    float clipDurationSeconds = 15.0f;
    /// In-game controller pipeline (deadzone, then sensitivity, then clamp) applied to the raw
    /// stick BakkesMod reports, so recorded controls match what the car received.
    float inputSteerSensitivity = 3.0f;
    float inputAerialSensitivity = 3.0f;
    float inputDeadzone = 0.07f;
    float ShapeStick(float raw, float sensitivity) const;

    std::unique_ptr<RewardClient> client_;

    int numRewardTerms = 0;
    std::vector<float> rollingAvgValues;
    float totalWeightedReward = 0.0f;

    int selectedClipIndex = -1;
    bool clipGroupsInitialized = false;
    std::string selectedClipPath;
    char newClipFolderBuf[512] = {};
    char renameClipBuf[512] = {};
    char moveClipBuf[512] = {};

    bool isPlayingClip = false;

    uint64_t currentTrainingStep = 0;
    bool forceAllPhases = false;
    bool debugRewards = true;
    int fadingTimesteps = 120;
    char serverHostBuf[128] = "127.0.0.1";
    int serverPort = 42069;
    char configFilePathBuf[512] = "";

    uint64_t physicsTickCount = 0;
    std::unordered_map<uintptr_t, ControllerInput> lastAppliedInput;

    void PollCommandFile();
    bool pluginUnloading = false;
    uint64_t lastTaskNoticeSeq = 0;

    int autosaveTicks = 0;
    int autosaveCounter = 0;
    int autosaveIndex = 0;

    bool probeActive = false;
    unsigned probeSeed = 1;
    int probeEpisode = 0;
    int probeTicksSinceSave = 0;
    int probeTick = 0;
    int probeSaveEvery = 2;
    float probeEpisodeSeconds = 6.0f;
    std::mt19937 probeRng{1};
    std::vector<ControllerInput> probeScript;
    void ProbeBeginEpisode(CarWrapper car, ServerWrapper server);
    void ProbeApplyInput(CarWrapper car, ServerWrapper server, ControllerInput *input);
    bool IsFirstCar(ServerWrapper server, CarWrapper caller) const;
    bool latchedWorldContactHas = false;
    float latchedWorldContactNormal[3] = {0.0f, 0.0f, 1.0f};

    char tasksJsonPathBuf[512] = "";
    std::string syncedTasksJsonPath;

    bool isWindowOpen = true;
};
