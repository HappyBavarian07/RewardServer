#include "RewardTesterPlugin.h"
#include "bakkesmod/wrappers/Engine/WorldInfoWrapper.h"
#include "bakkesmod/wrappers/includes.h"
#include "imgui/imgui.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <windows.h>

BAKKESMOD_PLUGIN(RewardTesterPlugin, "Reward Tester & State Inspector", "2.0", PERMISSION_ALL)

static double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void RotatorToRotArray(Rotator rot, float *outMat) {
    float pitch = (float) rot.Pitch * (3.14159265f / 32768.0f);
    float yaw = (float) rot.Yaw * (3.14159265f / 32768.0f);
    float roll = (float) rot.Roll * (3.14159265f / 32768.0f);

    float cp = std::cos(pitch), sp = std::sin(pitch);
    float cy = std::cos(yaw), sy = std::sin(yaw);
    float cr = std::cos(roll), sr = std::sin(roll);

    outMat[0] = cp * cy;
    outMat[1] = cp * sy;
    outMat[2] = sp;

    outMat[3] = sr * sp * cy - cr * sy;
    outMat[4] = sr * sp * sy + cr * cy;
    outMat[5] = -sr * cp;

    outMat[6] = -(cr * sp * cy + sr * sy);
    outMat[7] = cy * sr - cr * sp * sy;
    outMat[8] = cr * cp;
}

static void QuatToRotArray(const Quat &q, float *out) {
    const float x = q.X, y = q.Y, z = q.Z, w = q.W;
    const float n = std::sqrt(x * x + y * y + z * z + w * w);
    const float inv = n > 1e-6f ? 1.0f / n : 1.0f;
    const float qx = x * inv, qy = y * inv, qz = z * inv, qw = w * inv;
    const float r00 = 1.0f - 2.0f * (qy * qy + qz * qz);
    const float r01 = 2.0f * (qx * qy - qw * qz);
    const float r02 = 2.0f * (qx * qz + qw * qy);
    const float r10 = 2.0f * (qx * qy + qw * qz);
    const float r11 = 1.0f - 2.0f * (qx * qx + qz * qz);
    const float r12 = 2.0f * (qy * qz - qw * qx);
    const float r20 = 2.0f * (qx * qz - qw * qy);
    const float r21 = 2.0f * (qy * qz + qw * qx);
    const float r22 = 1.0f - 2.0f * (qx * qx + qy * qy);
    out[0] = r00; out[1] = r10; out[2] = r20;
    out[3] = r01; out[4] = r11; out[5] = r21;
    out[6] = r02; out[7] = r12; out[8] = r22;
}

void RewardTesterPlugin::HandleReplayKeys() {
    if (!replayActive || ImGui::GetIO().WantTextInput) return;
    if ((GetAsyncKeyState('P') & 1) != 0)
        isPlayingClip = !isPlayingClip;
    const float step = replayFrameIntervalSeconds * (ImGui::GetIO().KeyShift ? 10.0f : 1.0f);
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow), true)) {
        replayTime = std::max(0.0f, replayTime - step);
        replayNeedsReseed = true;
    }
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow), true)) {
        replayTime += step;
        replayNeedsReseed = true;
    }
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z), false)) {
        replayTime = 0.0f;
        replayNeedsReseed = true;
    }
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape), false))
        StopReplayClip();
}

void RewardTesterPlugin::onLoad() {
    cvarManager->log("RewardTesterPlugin loaded");
    gameWrapper->SetTimeout([this](GameWrapper *) { PollCommandFile(); }, 2.0f);
    gameWrapper->SetTimeout([this](GameWrapper *) { SampleReplay(); }, 1.0f);

    LoadPreferences();
    client_ = std::make_unique<RewardClient>();
    client_->Start(serverHostBuf, serverPort, configFilePathBuf);
    client_->RequestSetClipWindow(clipDurationSeconds);

    cvarManager->registerNotifier("rewardtester_toggle_menu", [this](std::vector<std::string> args) {
        cvarManager->executeCommand("togglemenu " + GetMenuName());
    }, "Toggle the Reward Tester floating window", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_save_clip", [this](std::vector<std::string> args) {
        SaveClip(args.size() > 1 ? args[1] : std::string());
        gameWrapper->Toast("Reward Tester",
                           "Saving Generic " + std::to_string((int) clipDurationSeconds) + "s Clip...",
                           "default", 2.5f, ToastType_Info);
    }, "Save the last seconds of play as a .rlpr clip", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_save_task_clip", [this](std::vector<std::string> args) {
        if (!client_) return;
        client_->RequestSaveTaskClip({clipOnlyFocus, watch.inReplay ? "replay" : "live"});
        lastTaskSaveTime = NowSeconds();
        if (autoMarkGoals && NowSeconds() - lastGoalTime < 20.0) client_->RequestLogTaskAttempt(true);
    }, "Save the rolling window as the next attempt of the active shot task", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_mark_scored", [this](std::vector<std::string> args) {
        if (client_) client_->RequestLogTaskAttempt(true);
    }, "Mark the latest attempt as scored (changes a miss to scored)", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_mark_miss", [this](std::vector<std::string> args) {
        if (client_) client_->RequestLogTaskAttempt(false);
    }, "Mark the latest attempt as missed (changes a scored to miss)", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_undo_attempt", [this](std::vector<std::string> args) {
        if (client_) client_->RequestUndoTaskAttempt();
    }, "Remove the latest attempt from the counters and keep its clip as _undone", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_next_task", [this](std::vector<std::string> args) {
        if (!client_) return;
        const auto tasks = client_->LatestTasks();
        if (!tasks.empty()) client_->RequestSetActiveTask((client_->ActiveTaskIndex() + 1) % (int) tasks.size());
    }, "Switch to the next shot task in the catalog", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_prev_task", [this](std::vector<std::string> args) {
        if (!client_) return;
        const auto tasks = client_->LatestTasks();
        if (!tasks.empty())
            client_->RequestSetActiveTask((client_->ActiveTaskIndex() - 1 + (int) tasks.size()) % (int) tasks.size());
    }, "Switch to the previous shot task in the catalog", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_probe_start", [this](std::vector<std::string> args) {
        probeSeed = args.size() > 1 ? static_cast<unsigned>(std::strtoul(args[1].c_str(), nullptr, 10)) : 1u;
        probeEpisodeSeconds = args.size() > 2 ? std::clamp(std::strtof(args[2].c_str(), nullptr), 1.0f, 7.0f) : 6.0f;
        probeSaveEvery = args.size() > 3 ? std::max(0, std::atoi(args[3].c_str())) : 2;
        probeRng.seed(probeSeed);
        probeEpisode = 0;
        probeTicksSinceSave = 0;
        probeScript.clear();
        probeTick = 0;
        probeActive = true;
        cvarManager->log("[probe] started: seed " + std::to_string(probeSeed));
    }, "Start automated physics probe episodes: [seed] [episodeSeconds<=7] [saveEvery]", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_autosave", [this](std::vector<std::string> args) {
        const float seconds = args.size() > 1 ? std::clamp(std::strtof(args[1].c_str(), nullptr), 0.0f, 13.0f) : 12.0f;
        autosaveTicks = static_cast<int>(seconds * 120.0f);
        autosaveCounter = 0;
        cvarManager->log("[autosave] every " + std::to_string(seconds) + " s of physics (0 = off)");
    }, "Save a clip every N seconds of physics ticks (<= 13, 0 = off)", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_probe_stop", [this](std::vector<std::string> args) {
        probeActive = false;
        probeScript.clear();
        cvarManager->log("[probe] stopped");
    }, "Stop the automated physics probe", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_reload", [this](std::vector<std::string> args) {
        ReloadConfig();
        gameWrapper->Toast("Reward Tester", "Reloading Reward Config...", "default", 2.5f, ToastType_Info);
    }, "Reload reward config on the server", PERMISSION_ALL);

    cvarManager->registerNotifier("rewardtester_reset", [this](std::vector<std::string> args) {
        SendRewardReset();
        gameWrapper->Toast("Reward Tester", "Reward State Reset", "default", 2.0f, ToastType_Info);
    }, "Reset reward state on the server", PERMISSION_ALL);

    gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.SetVehicleInput",
                                                 [this](CarWrapper caller, void *params, std::string eventName) {
                                                     ServerWrapper server = gameWrapper->GetCurrentGameState();
                                                     if (IsFirstCar(server, caller))
                                                         OnPhysicsTick(server, params);
                                                     if (probeActive && params != nullptr) {
                                                         CarWrapper localCar = gameWrapper->GetLocalCar();
                                                         if (localCar && !localCar.IsNull() &&
                                                             caller.memory_address == localCar.memory_address)
                                                             ProbeApplyInput(caller, server, static_cast<ControllerInput*>(params));
                                                     }
                                                     if (params != nullptr)
                                                         lastAppliedInput[caller.memory_address] = *static_cast<ControllerInput*>(params);
                                                     if (replayActive && params != nullptr) {
                                                         if (!ReplayStateDriven())
                                                             ApplyReplayFrame(server, activeReplayPath, replayTime);
                                                         ApplyReplayInput(caller, static_cast<ControllerInput*>(params));
                                                     }
                                                 });

    gameWrapper->HookEventWithCallerPost<CarWrapper>(
                               "Function TAGame.Car_TA.SetVehicleInput",
                               [this](CarWrapper caller, void *params, std::string eventName) {
                                   if (!replayActive) return;
                                   CarWrapper local = gameWrapper->GetLocalCar();
                                   if (local && !local.IsNull() &&
                                       caller.memory_address != local.memory_address)
                                       return;
                                   ServerWrapper server = gameWrapper->GetGameEventAsServer();
                                   if (!server || server.IsNull()) return;
                                   UpdateReplayPlayback(server, 1.0f / 120.0f);
                               });

    gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.OnScoreUpdated",
                           [this](std::string eventName) {
                               lastGoalTime = NowSeconds();
                               if (autoMarkGoals && client_ && NowSeconds() - lastTaskSaveTime < 10.0 &&
                                   client_->LastAttempt().outcome == "unmarked")
                                   client_->RequestLogTaskAttempt(true);
                               SendRewardReset();
                               cvarManager->log("[RewardTester] Goal scored, reward state reset sent.");
                           });

    gameWrapper->HookEvent("Function TAGame.GameEvent_TA.PlayerResetTraining",
                           [this](std::string eventName) {
                               SendRewardReset();
                           });

}

void RewardTesterPlugin::onUnload() {
    pluginUnloading = true;
    SavePreferences();
    gameWrapper->UnhookEvent("Function TAGame.Car_TA.SetVehicleInput");
    gameWrapper->UnhookEvent("Function TAGame.GameEvent_Soccar_TA.OnScoreUpdated");
    gameWrapper->UnhookEvent("Function TAGame.GameEvent_TA.PlayerResetTraining");
    gameWrapper->UnhookEvent("Function TAGame.Car_TA.SetVehicleInput");
    if (client_) client_->Stop();
    client_.reset();
    cvarManager->log("RewardTesterPlugin unloaded");
}

void RewardTesterPlugin::Reconnect() {
    if (!client_) client_ = std::make_unique<RewardClient>();
    client_->Start(serverHostBuf, serverPort, configFilePathBuf);
    cvarManager->log(std::string("Connecting to reward_server at ") + serverHostBuf + ":" + std::to_string(serverPort));
}

void RewardTesterPlugin::ReloadConfig() {
    if (client_) client_->RequestLoad(configFilePathBuf);
    cvarManager->log(std::string("Requested reward config reload: ") + configFilePathBuf);
}

void RewardTesterPlugin::SendRewardReset() {
    if (client_) client_->RequestReset();
}

void RewardTesterPlugin::SaveClip(const std::string &name) {
    if (!client_) return;
    client_->RequestSaveClip(name, {clipOnlyFocus, watch.inReplay ? "replay" : "live"});
    cvarManager->log("Saving clip (.rlpr) from the server's rolling buffer...");
}

void RewardTesterPlugin::LoadPreferences() {
    const char *appdata = std::getenv("APPDATA");
    if (!appdata) return;
    std::ifstream in(std::filesystem::path(appdata) / "bakkesmod/bakkesmod/data/reward_tester.ini");
    if (in) in >> clipDurationSeconds >> inputSteerSensitivity >> inputAerialSensitivity >> inputDeadzone;
    clipDurationSeconds = std::clamp(clipDurationSeconds, 1.0f, 300.0f);
    inputSteerSensitivity = std::clamp(inputSteerSensitivity, 1.0f, 10.0f);
    inputAerialSensitivity = std::clamp(inputAerialSensitivity, 1.0f, 10.0f);
    inputDeadzone = std::clamp(inputDeadzone, 0.0f, 0.5f);
}

float RewardTesterPlugin::ShapeStick(float raw, float sensitivity) const {
    if (std::abs(raw) < inputDeadzone) return 0.0f;
    return std::clamp(raw * sensitivity, -1.0f, 1.0f);
}

void RewardTesterPlugin::SavePreferences() {
    const char *appdata = std::getenv("APPDATA");
    if (!appdata) return;
    const std::filesystem::path path =
        std::filesystem::path(appdata) / "bakkesmod/bakkesmod/data/reward_tester.ini";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (out) out << clipDurationSeconds << ' ' << inputSteerSensitivity << ' ' << inputAerialSensitivity << ' ' << inputDeadzone << '\n';
}

void RewardTesterPlugin::RefreshClipList() {
    clipGroupsInitialized = false;
    if (client_) client_->RequestListClips();
}


namespace {
int ProbeRand(std::mt19937 &r, int a, int b) { return a + static_cast<int>(r() % static_cast<unsigned>(b - a + 1)); }
float ProbeUnit(std::mt19937 &r) { return static_cast<float>(r()) / static_cast<float>(r.max()); }
float ProbeRange(std::mt19937 &r, float a, float b) { return a + (b - a) * ProbeUnit(r); }
float ProbeAnalog(std::mt19937 &r) {
    static const float v[] = {-1.0f, -0.5f, 0.0f, 0.0f, 0.5f, 1.0f};
    return (ProbeUnit(r) < 0.35f) ? ProbeRange(r, -1.0f, 1.0f) : v[r() % 6];
}
float ProbeThrottle(std::mt19937 &r) {
    static const float v[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f};
    return v[r() % 7];
}
void ProbeAppend(std::vector<ControllerInput> &out, const ControllerInput &in, int ticks) {
    for (int i = 0; i < ticks; ++i) out.push_back(in);
}
ControllerInput ProbeBlank() {
    ControllerInput c{};
    return c;
}
void ProbeSetJump(ControllerInput &c, bool jump) { c.Jump = jump ? 1 : 0; }
void ProbeSetBoost(ControllerInput &c, bool boost) {
    c.HoldingBoost = boost ? 1 : 0;
    c.ActivateBoost = boost ? 1 : 0;
}
void ProbeSetAir(ControllerInput &c, float pitch, float yaw, float roll) {
    c.Pitch = pitch;
    c.Yaw = yaw;
    c.Roll = roll;
    c.DodgeForward = -pitch;
    c.DodgeStrafe = yaw;
}
}

void RewardTesterPlugin::ProbeBeginEpisode(CarWrapper car, ServerWrapper server) {
    std::mt19937 &r = probeRng;
    static const int kOrder[] = {0, 1, 5, 4, 2, 5, 4, 4, 3, 5, 4, 1, 5, 4, 2, 5};
    const int scenario = kOrder[probeEpisode % 16];
    const int totalTicks = scenario == 4 ? 240 : (scenario == 5 ? 300 : static_cast<int>(probeEpisodeSeconds * 120.0f));
    probeScript.clear();
    probeTick = 0;

    RBState rb;
    rb.Time = 0.0f;
    rb.bSleeping = 0;
    rb.bNewData = 1;
    rb.AngularVelocity = Vector(0.0f, 0.0f, 0.0f);

    const float yaw = ProbeRange(r, -3.14159265f, 3.14159265f);
    float qw = std::cos(yaw * 0.5f), qx = 0.0f, qy = 0.0f, qz = std::sin(yaw * 0.5f);
    float startSpeed = 0.0f;
    float z = 17.0f;
    if (scenario >= 3) {
        float g[4];
        float n = 0.0f;
        for (int i = 0; i < 4; ++i) {
            g[i] = ProbeRange(r, -1.0f, 1.0f);
            n += g[i] * g[i];
        }
        n = std::sqrt(std::max(n, 1e-6f));
        qw = g[0] / n; qx = g[1] / n; qy = g[2] / n; qz = g[3] / n;
        z = ProbeRange(r, 400.0f, 1500.0f);
    } else {
        startSpeed = ProbeUnit(r) < 0.3f ? 0.0f : ProbeRange(r, 0.0f, 2300.0f);
    }
    rb.Quaternion = Quat(qw, qx, qy, qz).normalize();
    rb.Location = Vector(ProbeRange(r, -3000.0f, 3000.0f), ProbeRange(r, -4000.0f, 4000.0f), z);
    const float fx = 1.0f - 2.0f * (qy * qy + qz * qz);
    const float fy = 2.0f * (qx * qy + qw * qz);
    const float fz = 2.0f * (qx * qz - qw * qy);
    if (scenario >= 3) {
        rb.LinearVelocity = Vector(ProbeRange(r, -1200.0f, 1200.0f), ProbeRange(r, -1200.0f, 1200.0f), ProbeRange(r, -600.0f, 600.0f));
        rb.AngularVelocity = Vector(ProbeRange(r, -2.0f, 2.0f), ProbeRange(r, -2.0f, 2.0f), ProbeRange(r, -2.0f, 2.0f));
    } else {
        rb.LinearVelocity = Vector(fx * startSpeed, fy * startSpeed, 0.0f);
    }
    (void) fz;

    float ballX = 3800.0f, ballY = 4700.0f, ballZ = 100.0f;
    float ballVx = 0.0f, ballVy = 0.0f, ballVz = 0.0f;
    float approachTicks = 0.0f;
    bool aerialBall = false;
    if (scenario == 5) {
        aerialBall = ProbeUnit(r) < 0.4f;
        ballX = ProbeRange(r, -2500.0f, 2500.0f);
        ballY = ProbeRange(r, -3500.0f, 3500.0f);
        ballZ = aerialBall ? ProbeRange(r, 250.0f, 800.0f) : 93.0f;
        if (ProbeUnit(r) > 0.4f) {
            ballVx = ProbeRange(r, -900.0f, 900.0f);
            ballVy = ProbeRange(r, -900.0f, 900.0f);
            ballVz = aerialBall ? ProbeRange(r, -200.0f, 300.0f) : 0.0f;
        }
        const float phi = ProbeRange(r, -3.14159265f, 3.14159265f);
        const float dist = ProbeRange(r, 500.0f, 1400.0f);
        const float cx = std::clamp(ballX + dist * std::cos(phi), -3800.0f, 3800.0f);
        const float cy = std::clamp(ballY + dist * std::sin(phi), -4700.0f, 4700.0f);
        const float dx = ballX - cx, dy = ballY - cy;
        const float d2 = std::sqrt(dx * dx + dy * dy);
        const float heading = std::atan2(dy, dx) + ProbeRange(r, -0.12f, 0.12f);
        const float speed = ProbeRange(r, 700.0f, 1700.0f);
        rb.Location = Vector(cx, cy, 17.0f);
        rb.Quaternion = Quat(std::cos(heading * 0.5f), 0.0f, 0.0f, std::sin(heading * 0.5f)).normalize();
        rb.LinearVelocity = Vector(std::cos(heading) * speed, std::sin(heading) * speed, 0.0f);
        rb.AngularVelocity = Vector(0.0f, 0.0f, 0.0f);
        approachTicks = d2 / std::max(speed, 300.0f) * 120.0f * 0.85f;
    }
    car.SetRBState(rb);
    car.SetPhysicsState(rb);
    car.SetbJumped(0);
    car.SetbDoubleJumped(0);
    auto boostComp = car.GetBoostComponent();
    if (boostComp && !boostComp.IsNull()) boostComp.SetCurrentBoostAmount(ProbeRange(r, 0.0f, 1.0f));

    BallWrapper ball = server.GetBall();
    if (ball && !ball.IsNull()) {
        RBState brb;
        brb.Location = Vector(ballX, ballY, ballZ);
        brb.Quaternion = Quat(1.0f, 0.0f, 0.0f, 0.0f);
        brb.LinearVelocity = Vector(ballVx, ballVy, ballVz);
        brb.AngularVelocity = Vector(0.0f, 0.0f, 0.0f);
        brb.Time = 0.0f;
        brb.bSleeping = 0;
        brb.bNewData = 1;
        ball.SetRBState(brb);
        ball.SetPhysicsState(brb);
    }

    bool ballPlanned = false;
    while (static_cast<int>(probeScript.size()) < totalTicks) {
        if (scenario == 5) {
            ControllerInput drive = ProbeBlank();
            drive.Throttle = 1.0f;
            ProbeSetBoost(drive, true);
            if (ballPlanned) {
                ControllerInput tail = drive;
                ProbeSetAir(tail, ProbeAnalog(r), ProbeAnalog(r), ProbeAnalog(r));
                ProbeSetBoost(tail, ProbeUnit(r) < 0.5f);
                ProbeAppend(probeScript, tail, ProbeRand(r, 10, 40));
                continue;
            }
            ballPlanned = true;
            const int tHit = std::max(10, static_cast<int>(approachTicks));
            const float mode = ProbeUnit(r);
            auto dodgeDir = [&](ControllerInput &c) {
                const float m = ProbeUnit(r);
                if (m < 0.5f) ProbeSetAir(c, -1.0f, 0.0f, 0.0f);
                else if (m < 0.7f) ProbeSetAir(c, -1.0f, ProbeRange(r, -1.0f, 1.0f), 0.0f);
                else if (m < 0.85f) ProbeSetAir(c, 0.0f, ProbeRange(r, -1.0f, 1.0f), 0.0f);
                else ProbeSetAir(c, 1.0f, 0.0f, 0.0f);
            };
            if (aerialBall) {
                ProbeAppend(probeScript, drive, std::max(3, static_cast<int>(tHit * ProbeRange(r, 0.25f, 0.5f))));
                ControllerInput jump = drive;
                ProbeSetJump(jump, true);
                ProbeAppend(probeScript, jump, ProbeRand(r, 10, 24));
                ControllerInput air = drive;
                ProbeSetAir(air, -1.0f, 0.0f, 0.0f);
                ProbeAppend(probeScript, air, ProbeRand(r, 8, 20));
                ControllerInput pick = drive;
                ProbeSetAir(pick, ProbeAnalog(r), ProbeAnalog(r), ProbeAnalog(r));
                ProbeAppend(probeScript, pick, ProbeRand(r, 8, 25));
                if (ProbeUnit(r) < 0.6f) {
                    ControllerInput dodge = drive;
                    ProbeSetJump(dodge, true);
                    dodgeDir(dodge);
                    ProbeAppend(probeScript, dodge, ProbeRand(r, 2, 5));
                }
            } else if (mode < 0.4f) {
                ControllerInput straight = drive;
                straight.Steer = ProbeUnit(r) < 0.5f ? 0.0f : ProbeRange(r, -0.3f, 0.3f);
                ProbeAppend(probeScript, straight, tHit + 20);
            } else if (mode < 0.65f) {
                ProbeAppend(probeScript, drive, std::max(2, tHit - ProbeRand(r, 8, 25)));
                ControllerInput jump = drive;
                ProbeSetJump(jump, true);
                ProbeAppend(probeScript, jump, ProbeRand(r, 3, 14));
            } else {
                ProbeAppend(probeScript, drive, std::max(2, tHit - ProbeRand(r, 18, 40)));
                ControllerInput jump = drive;
                ProbeSetJump(jump, true);
                ProbeAppend(probeScript, jump, ProbeRand(r, 1, 6));
                ControllerInput gap = drive;
                ProbeAppend(probeScript, gap, ProbeRand(r, 0, 5));
                ControllerInput dodge = drive;
                ProbeSetJump(dodge, true);
                dodgeDir(dodge);
                ProbeAppend(probeScript, dodge, ProbeRand(r, 2, 5));
            }
            continue;
        }
        if (scenario == 0) {
            ControllerInput c = ProbeBlank();
            c.Throttle = ProbeThrottle(r);
            c.Steer = ProbeAnalog(r);
            c.Handbrake = ProbeUnit(r) < 0.15f ? 1 : 0;
            ProbeSetBoost(c, ProbeUnit(r) < 0.4f);
            ProbeAppend(probeScript, c, ProbeRand(r, 8, 60));
        } else if (scenario == 1 || scenario == 2) {
            ControllerInput drive = ProbeBlank();
            drive.Throttle = 1.0f;
            drive.Steer = ProbeUnit(r) < 0.5f ? 0.0f : ProbeAnalog(r);
            ProbeSetBoost(drive, ProbeUnit(r) < 0.6f);
            ProbeAppend(probeScript, drive, scenario == 1 ? ProbeRand(r, 5, 40) : ProbeRand(r, 3, 25));

            ControllerInput jump = drive;
            ProbeSetJump(jump, true);
            ProbeAppend(probeScript, jump, scenario == 1 ? ProbeRand(r, 1, 30) : ProbeRand(r, 1, 8));

            ControllerInput gap = drive;
            ProbeSetJump(gap, false);
            ProbeSetAir(gap, ProbeAnalog(r) * 0.0f, 0.0f, 0.0f);
            ProbeAppend(probeScript, gap, scenario == 1 ? ProbeRand(r, 0, 25) : ProbeRand(r, 0, 6));

            ControllerInput dodge = gap;
            ProbeSetJump(dodge, true);
            const float mode = ProbeUnit(r);
            if (mode < 0.35f) ProbeSetAir(dodge, -1.0f, 0.0f, 0.0f);
            else if (mode < 0.55f) ProbeSetAir(dodge, 1.0f, 0.0f, 0.0f);
            else if (mode < 0.75f) ProbeSetAir(dodge, -1.0f, ProbeAnalog(r), 0.0f);
            else ProbeSetAir(dodge, ProbeAnalog(r), ProbeAnalog(r), ProbeAnalog(r));
            ProbeAppend(probeScript, dodge, ProbeRand(r, 2, 6));

            ControllerInput after = gap;
            ProbeSetAir(after, ProbeAnalog(r), ProbeAnalog(r), ProbeAnalog(r));
            ProbeSetBoost(after, ProbeUnit(r) < 0.3f);
            ProbeAppend(probeScript, after, ProbeRand(r, 5, scenario == 1 ? 90 : 30));
        } else if (scenario == 4) {
            ControllerInput c = ProbeBlank();
            ProbeSetAir(c, ProbeAnalog(r), ProbeAnalog(r), ProbeAnalog(r));
            ProbeSetBoost(c, ProbeUnit(r) < 0.3f);
            ProbeAppend(probeScript, c, ProbeRand(r, 2, 40));
            ControllerInput press = c;
            auto axis = [&]() { return ProbeUnit(r) < 0.3f ? 0.0f : ProbeRange(r, -1.0f, 1.0f); };
            ProbeSetAir(press, axis(), axis(), axis());
            ProbeSetJump(press, true);
            ProbeAppend(probeScript, press, ProbeRand(r, 1, 4));
            ControllerInput rest = c;
            ProbeSetAir(rest, ProbeAnalog(r), ProbeAnalog(r), ProbeAnalog(r));
            ProbeAppend(probeScript, rest, ProbeRand(r, 30, 200));
        } else {
            ControllerInput c = ProbeBlank();
            ProbeSetAir(c, ProbeAnalog(r), ProbeAnalog(r), ProbeAnalog(r));
            ProbeSetBoost(c, ProbeUnit(r) < 0.35f);
            c.Throttle = ProbeThrottle(r);
            ProbeSetJump(c, ProbeUnit(r) < 0.06f);
            ProbeAppend(probeScript, c, ProbeRand(r, 4, 30));
        }
    }
    probeScript.resize(static_cast<size_t>(totalTicks));
    cvarManager->log("[probe] episode " + std::to_string(probeEpisode) + " scenario " + std::to_string(scenario));
}

void RewardTesterPlugin::ProbeApplyInput(CarWrapper car, ServerWrapper server, ControllerInput *input) {
    if (!input) return;
    if (probeScript.empty() || probeTick >= static_cast<int>(probeScript.size())) {
        if (!probeScript.empty()) {
            probeTicksSinceSave += static_cast<int>(probeScript.size());
            if (probeSaveEvery > 0 && probeTicksSinceSave >= 1100) {
                SaveClip("probe_" + std::to_string(probeSeed) + "_ep" + std::to_string(probeEpisode));
                probeTicksSinceSave = 0;
            }
            ++probeEpisode;
        }
        ProbeBeginEpisode(car, server);
    }
    *input = probeScript[static_cast<size_t>(probeTick++)];
}

void RewardTesterPlugin::AutoAdvanceTask() {
    if (!autoAdvance || !client_) return;
    const LastAttemptInfo last = client_->LastAttempt();
    if (last.number <= 0 || last.outcome == "unmarked" || last.outcome == "none") return;
    const auto tasks = client_->LatestTasks();
    const int active = client_->ActiveTaskIndex();
    if (active < 0 || active >= (int) tasks.size() || tasks.size() < 2) return;
    const int key = active * 1000 + last.number;
    if (key == lastAdvancedKey) return;
    if (tasks[active].completedAttempts < tasks[active].targetAttempts) return;
    lastAdvancedKey = key;
    client_->RequestSetActiveTask((active + 1) % (int) tasks.size());
}

void RewardTesterPlugin::PollCommandFile() {
    if (pluginUnloading) return;
    if (client_) {
        const TaskNotice notice = client_->LatestTaskNotice();
        if (notice.seq != lastTaskNoticeSeq) {
            lastTaskNoticeSeq = notice.seq;
            gameWrapper->Toast("Reward Tester", notice.text, "default", 3.0f, ToastType_Info);
        }
        AutoAdvanceTask();
    }
    const char *appdata = std::getenv("APPDATA");
    if (appdata) {
        const std::filesystem::path path =
            std::filesystem::path(appdata) / "bakkesmod/bakkesmod/data/rewardtester_commands.txt";
        std::vector<std::string> lines;
        {
            std::ifstream in(path);
            std::string line;
            while (in && std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (!line.empty()) lines.push_back(line);
            }
        }
        if (!lines.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            for (const auto &line : lines) {
                cvarManager->log("[cmd] " + line);
                cvarManager->executeCommand(line, false);
            }
        }
    }
    gameWrapper->SetTimeout([this](GameWrapper *) { PollCommandFile(); }, 0.5f);
}

bool RewardTesterPlugin::IsFirstCar(ServerWrapper server, CarWrapper caller) const {
    if (!server || server.IsNull() || !caller || caller.IsNull()) return false;
    ArrayWrapper<CarWrapper> cars = server.GetCars();
    for (int i = 0; i < cars.Count(); ++i) {
        CarWrapper car = cars.Get(i);
        if (car && !car.IsNull()) return car.memory_address == caller.memory_address;
    }
    return false;
}

void RewardTesterPlugin::OnPhysicsTick(ServerWrapper server, void *params) {
    if (!server || server.IsNull()) {
        server = gameWrapper->GetCurrentGameState();
    }
    if (!server || server.IsNull()) return;

    physicsTickCount++;
    if (!client_) return;

    if (watch.inReplay) return;

    CarWrapper localCar = gameWrapper->GetLocalCar();
    ArrayWrapper<CarWrapper> carsArray = server.GetCars();
    if ((!localCar || localCar.IsNull()) && carsArray.Count() > 0) localCar = carsArray.Get(0);
    if (!localCar || localCar.IsNull()) return;
    SubmitSnapshot(server, localCar, 1, false);
}

void RewardTesterPlugin::SubmitSnapshot(ServerWrapper server, CarWrapper localCar, int tickAdvance, bool fromReplay) {
    ArrayWrapper<CarWrapper> carsArray = server.GetCars();
    const int carCount = carsArray.Count();
    BallWrapper ball = server.GetBall();
    if (!ball || ball.IsNull()) return;

    std::vector<CarWrapper> orderedCars;
    orderedCars.push_back(localCar);

    for (int i = 0; i < carCount; ++i) {
        CarWrapper other = carsArray.Get(i);
        if (!other || other.IsNull()) continue;
        if (other.memory_address == localCar.memory_address) continue;
        orderedCars.push_back(other);
    }

    RewardStateSnapshot snap;
    snap.step = currentTrainingStep;
    snap.forceAll = forceAllPhases;
    snap.debug = debugRewards;
    snap.cars.resize(orderedCars.size());

    RewardBallSnapshot &b = snap.ball;
    const RBState ballRB = ball.GetRBState();
    b.pos[0] = ballRB.Location.X;
    b.pos[1] = ballRB.Location.Y;
    b.pos[2] = ballRB.Location.Z;
    b.vel[0] = ballRB.LinearVelocity.X;
    b.vel[1] = ballRB.LinearVelocity.Y;
    b.vel[2] = ballRB.LinearVelocity.Z;
    b.angVel[0] = ballRB.AngularVelocity.X;
    b.angVel[1] = ballRB.AngularVelocity.Y;
    b.angVel[2] = ballRB.AngularVelocity.Z;
    QuatToRotArray(ballRB.Quaternion, b.rotMat);

    for (size_t carIdx = 0; carIdx < orderedCars.size(); ++carIdx) {
        CarWrapper car = orderedCars[carIdx];
        RewardCarSnapshot &c = snap.cars[carIdx];
        bool isLocal = (carIdx == 0);

        const RBState carRB = car.GetRBState();
        c.pos[0] = carRB.Location.X;
        c.pos[1] = carRB.Location.Y;
        c.pos[2] = carRB.Location.Z;
        c.vel[0] = carRB.LinearVelocity.X;
        c.vel[1] = carRB.LinearVelocity.Y;
        c.vel[2] = carRB.LinearVelocity.Z;
        c.angVel[0] = carRB.AngularVelocity.X;
        c.angVel[1] = carRB.AngularVelocity.Y;
        c.angVel[2] = carRB.AngularVelocity.Z;
        QuatToRotArray(carRB.Quaternion, c.rotMat);
        if (carIdx == 0 && physicsTickCount % 1200 == 1) {
            float legacy[9];
            RotatorToRotArray(car.GetRotation(), legacy);
            float worst = 0.0f;
            for (int i = 0; i < 9; ++i) worst = std::max(worst, std::abs(legacy[i] - c.rotMat[i]));
            cvarManager->log("[rot-check] max |rotator - quaternion| = " + std::to_string(worst));
        }

        auto boostComp = car.GetBoostComponent();
        c.boost = (boostComp ? boostComp.GetCurrentBoostAmount() : 1.0f) * 100.0f;
        c.isOnGround = (car.GetNumWheelContacts() >= 3) ? 1 : 0;
        c.hasJumped = car.GetbJumped() ? 1 : 0;
        c.hasDoubleJumped = car.GetbDoubleJumped() ? 1 : 0;

        auto dodgeComp = car.GetDodgeComponent();
        if (dodgeComp && !dodgeComp.IsNull() && dodgeComp.GetbActive()) {
            c.isFlipping = 1;
            c.hasFlipped = 1;
            c.flipTime = dodgeComp.GetActiveTime();
            Vector dir = dodgeComp.GetDodgeDirection();
            c.flipRelTorque[0] = -dir.Y;
            c.flipRelTorque[1] = dir.X;
            c.flipRelTorque[2] = 0.0f;
        } else {
            c.isFlipping = 0;
            c.hasFlipped = 0;
            c.flipTime = 0.0f;
            c.flipRelTorque[0] = 0.0f;
            c.flipRelTorque[1] = 0.0f;
            c.flipRelTorque[2] = 0.0f;
        }
        c.isSupersonic = car.GetbSuperSonic() ? 1 : 0;
        c.isDemoed = 0;
        c.demoRespawnTimer = 0.0f;
        c.team = car.GetTeamNum2();

        PriWrapper pri = car.GetPRI();
        const bool isBot = pri && !pri.IsNull() && pri.GetbBot();
        const auto stashed = lastAppliedInput.find(car.memory_address);
        const bool exactInput = isBot || probeActive;
        ControllerInput ci{};
        if (!fromReplay) ci = (exactInput && stashed != lastAppliedInput.end()) ? stashed->second : car.GetInput();
        const bool shape = !exactInput;
        c.act[0] = ci.Throttle;
        c.act[1] = shape ? ShapeStick(ci.Steer, inputSteerSensitivity) : ci.Steer;
        c.act[2] = shape ? ShapeStick(ci.Pitch, inputAerialSensitivity) : ci.Pitch;
        c.act[3] = shape ? ShapeStick(ci.Yaw, inputAerialSensitivity) : ci.Yaw;
        c.act[4] = shape ? ShapeStick(ci.Roll, inputAerialSensitivity) : ci.Roll;
        c.act[5] = ci.Jump ? 1.0f : 0.0f;
        c.act[6] = (ci.HoldingBoost || ci.ActivateBoost) ? 1.0f : 0.0f;
        c.act[7] = ci.Handbrake ? 1.0f : 0.0f;
        c.handbrakeVal = c.act[7];

        WorldContactData wc = car.GetWorldContact();
        if (isLocal) {
            if (wc.bHasContact) {
                latchedWorldContactHas = true;
                float nMag = wc.Normal.magnitude();
                if (nMag > 1e-4f) {
                    latchedWorldContactNormal[0] = wc.Normal.X / nMag;
                    latchedWorldContactNormal[1] = wc.Normal.Y / nMag;
                    latchedWorldContactNormal[2] = wc.Normal.Z / nMag;
                }
            }

            if (latchedWorldContactHas || wc.bHasContact) {
                c.worldContactHas = 1;
                if (wc.bHasContact) {
                    float nMag = wc.Normal.magnitude();
                    if (nMag > 1e-4f) {
                        c.worldContactNormal[0] = wc.Normal.X / nMag;
                        c.worldContactNormal[1] = wc.Normal.Y / nMag;
                        c.worldContactNormal[2] = wc.Normal.Z / nMag;
                    } else {
                        c.worldContactNormal[0] = latchedWorldContactNormal[0];
                        c.worldContactNormal[1] = latchedWorldContactNormal[1];
                        c.worldContactNormal[2] = latchedWorldContactNormal[2];
                    }
                } else {
                    c.worldContactNormal[0] = latchedWorldContactNormal[0];
                    c.worldContactNormal[1] = latchedWorldContactNormal[1];
                    c.worldContactNormal[2] = latchedWorldContactNormal[2];
                }
            } else if (c.isOnGround) {
                c.worldContactHas = 1;
                if (c.pos[2] > 1900.0f) {
                    c.worldContactNormal[0] = 0;
                    c.worldContactNormal[1] = 0;
                    c.worldContactNormal[2] = -1;
                } else if (std::abs(c.pos[0]) > 3900.0f) {
                    c.worldContactNormal[0] = c.pos[0] > 0 ? -1.0f : 1.0f;
                    c.worldContactNormal[1] = 0;
                    c.worldContactNormal[2] = 0;
                } else if (std::abs(c.pos[1]) > 4900.0f) {
                    c.worldContactNormal[0] = 0;
                    c.worldContactNormal[1] = c.pos[1] > 0 ? -1.0f : 1.0f;
                    c.worldContactNormal[2] = 0;
                } else {
                    c.worldContactNormal[0] = 0;
                    c.worldContactNormal[1] = 0;
                    c.worldContactNormal[2] = 1;
                }
            } else {
                c.worldContactHas = 0;
                c.worldContactNormal[0] = 0.0f;
                c.worldContactNormal[1] = 0.0f;
                c.worldContactNormal[2] = 0.0f;
            }
        } else {
            if (wc.bHasContact) {
                c.worldContactHas = 1;
                float nMag = wc.Normal.magnitude();
                if (nMag > 1e-4f) {
                    c.worldContactNormal[0] = wc.Normal.X / nMag;
                    c.worldContactNormal[1] = wc.Normal.Y / nMag;
                    c.worldContactNormal[2] = wc.Normal.Z / nMag;
                } else {
                    c.worldContactNormal[0] = 0;
                    c.worldContactNormal[1] = 0;
                    c.worldContactNormal[2] = 1;
                }
            } else if (c.isOnGround) {
                c.worldContactHas = 1;
                if (c.pos[2] > 1900.0f) {
                    c.worldContactNormal[0] = 0;
                    c.worldContactNormal[1] = 0;
                    c.worldContactNormal[2] = -1;
                } else if (std::abs(c.pos[0]) > 3900.0f) {
                    c.worldContactNormal[0] = c.pos[0] > 0 ? -1.0f : 1.0f;
                    c.worldContactNormal[1] = 0;
                    c.worldContactNormal[2] = 0;
                } else if (std::abs(c.pos[1]) > 4900.0f) {
                    c.worldContactNormal[0] = 0;
                    c.worldContactNormal[1] = c.pos[1] > 0 ? -1.0f : 1.0f;
                    c.worldContactNormal[2] = 0;
                } else {
                    c.worldContactNormal[0] = 0;
                    c.worldContactNormal[1] = 0;
                    c.worldContactNormal[2] = 1;
                }
            } else {
                c.worldContactHas = 0;
                c.worldContactNormal[0] = 0.0f;
                c.worldContactNormal[1] = 0.0f;
                c.worldContactNormal[2] = 0.0f;
            }
        }

        float dx = c.pos[0] - b.pos[0], dy = c.pos[1] - b.pos[1], dz = c.pos[2] - b.pos[2];
        c.isBallTouched = (std::sqrt(dx * dx + dy * dy + dz * dz) < 210.0f) ? 1 : 0;
    }

    if (snap.tickSkip < 1) snap.tickSkip = 1;
    if (fromReplay) {
        snap.tickSkip = std::max(1, tickAdvance);
        snap.captureTicks = snap.tickSkip;
        client_->SubmitCapture(snap);
        if (watchEvaluate) client_->SubmitState(snap);
    } else {
        client_->SubmitCapture(snap);
        if (autosaveTicks > 0 && ++autosaveCounter >= autosaveTicks) {
            autosaveCounter = 0;
            SaveClip("match_" + std::to_string(autosaveIndex++));
        }
        if (physicsTickCount % (uint64_t) snap.tickSkip == 0) {
            client_->SubmitState(snap);
            latchedWorldContactHas = false;
            latchedWorldContactNormal[0] = 0.0f;
            latchedWorldContactNormal[1] = 0.0f;
            latchedWorldContactNormal[2] = 1.0f;
        }
    }

    RewardResult res = client_->LatestResult();
    numRewardTerms = (int) res.names.size();
    totalWeightedReward = res.total;

    if (rollingAvgValues.size() != res.values.size())
        rollingAvgValues.assign(res.values.size(), 0.0f);

    if (res.valid && !res.values.empty()) {
        float fadeAlpha = 1.0f / std::max(1.0f, (float) fadingTimesteps);
        for (size_t i = 0; i < res.values.size(); ++i)
            rollingAvgValues[i] = rollingAvgValues[i] * (1.0f - fadeAlpha) + res.values[i] * fadeAlpha;
    }
    RecordTermHistory(res);
    CheckAlerts(res);
}

void RewardTesterPlugin::RecordTermHistory(const RewardResult &res) {
    if (!res.valid || res.values.empty()) return;
    if (++historyCounter % 4 != 0) return;
    std::lock_guard<std::mutex> lock(historyMut);
    if (termHistory.size() != res.values.size()) termHistory.assign(res.values.size(), std::deque<float>());
    for (size_t i = 0; i < res.values.size(); ++i) {
        termHistory[i].push_back(res.values[i]);
        if (termHistory[i].size() > 150) termHistory[i].pop_front();
    }
}

void RewardTesterPlugin::CheckAlerts(const RewardResult &res) {
    if (alerts.empty() || !res.valid) return;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (auto &alert : alerts) {
        const auto it = std::find(res.names.begin(), res.names.end(), alert.term);
        if (it == res.names.end()) continue;
        const size_t index = static_cast<size_t>(it - res.names.begin());
        if (index >= res.values.size()) continue;
        const float value = res.values[index];
        const bool hit = alert.above ? value > alert.value : value < alert.value;
        if (!hit || now - alert.lastFired < 3.0) continue;
        alert.lastFired = now;
        gameWrapper->Toast("Reward Tester", alert.term + (alert.above ? " above " : " below ") + std::to_string(alert.value) +
                           " (now " + std::to_string(value) + ")", "default", 3.0f, ToastType_Warning);
    }
}

void RewardTesterPlugin::SampleReplay() {
    if (pluginUnloading) return;
    gameWrapper->SetTimeout([this](GameWrapper *) { SampleReplay(); }, 0.016f);

    watch.inReplay = gameWrapper->IsInReplay();
    if (!watch.inReplay) {
        watch.frame = -1;
        watch.lastSentFrame = -1;
        return;
    }
    if (!client_ || replayActive) return;

    ReplayServerWrapper replay = gameWrapper->GetGameEventAsReplay();
    if (!replay || replay.IsNull()) return;

    const int frame = replay.GetCurrentReplayFrame();
    const int fps = std::max(1, replay.GetReplayFPS());
    watch.frame = frame;
    watch.fps = fps;

    ArrayWrapper<CarWrapper> cars = replay.GetCars();
    watch.cars = cars.Count();
    if (watch.cars == 0) return;

    ActorWrapper target = replay.GetViewTarget();
    CarWrapper subject(0);
    int subjectIndex = -1;
    for (int i = 0; i < cars.Count(); ++i) {
        CarWrapper car = cars.Get(i);
        if (!car || car.IsNull()) continue;
        PriWrapper pri = car.GetPRI();
        const bool followed = target && !target.IsNull() &&
                              (car.memory_address == target.memory_address ||
                               (pri && !pri.IsNull() && pri.memory_address == target.memory_address));
        if (followed) {
            subject = car;
            subjectIndex = i;
            break;
        }
    }
    if (subjectIndex < 0) {
        subject = cars.Get(0);
        subjectIndex = 0;
        watch.followedName = "(no player followed, using car 0)";
    } else {
        PriWrapper pri = subject.GetPRI();
        watch.followedName = pri && !pri.IsNull() ? pri.GetPlayerName().ToString() : "car " + std::to_string(subjectIndex);
    }
    watch.followedIndex = subjectIndex;
    if (!subject || subject.IsNull()) return;

    {
        const RBState rb = subject.GetRBState();
        ++watchDiagCalls;
        if (frame != watchDiagLastFrame) ++watchDiagFrameChanges;
        else if (std::abs(rb.Location.X - watchDiagLastPos[0]) + std::abs(rb.Location.Y - watchDiagLastPos[1]) +
                 std::abs(rb.Location.Z - watchDiagLastPos[2]) > 0.01f)
            ++watchDiagMovedWithinFrame;
        watchDiagLastFrame = frame;
        watchDiagLastPos[0] = rb.Location.X;
        watchDiagLastPos[1] = rb.Location.Y;
        watchDiagLastPos[2] = rb.Location.Z;
        const double now = NowSeconds();
        if (now - watchDiagLast >= 1.0) {
            char line[200];
            std::snprintf(line, sizeof(line), "[watch-diag] calls/s=%d frame_changes/s=%d moved_within_frame/s=%d fps=%d frame=%d",
                          watchDiagCalls, watchDiagFrameChanges, watchDiagMovedWithinFrame, fps, frame);
            cvarManager->log(line);
            watchDiagLast = now;
            watchDiagCalls = watchDiagFrameChanges = watchDiagMovedWithinFrame = 0;
        }
    }

    if (frame == watch.lastSentFrame) return;
    const int ticksPerFrame = std::max(1, static_cast<int>(std::lround(120.0f / fps)));
    const int delta = frame - watch.lastSentFrame;
    int advance = ticksPerFrame;
    if (watch.lastSentFrame < 0 || delta < 1 || delta > 4) {
        client_->RequestReset(true);
    } else {
        advance = delta * ticksPerFrame;
    }
    watch.lastSentFrame = frame;
    ++watch.samples;
    SubmitSnapshot(replay, subject, advance, true);
}


void RewardTesterPlugin::BuildClipGroups(std::vector<ClipGroupEntry>& groups) {
    auto state = client_ ? client_->LatestClipState() : RewardClient::ClipState{};
    auto addClip = [&](auto&& self, std::vector<ClipGroupEntry>& level,
                       const std::vector<std::string>& parts, size_t depth,
                       const std::string& clip) -> void {
        auto it = std::find_if(level.begin(), level.end(), [&](const ClipGroupEntry& entry) {
            return entry.group == parts[depth];
        });
        if (it == level.end()) {
            level.push_back({ parts[depth], {}, {} });
            it = std::prev(level.end());
        }
        if (depth + 1 == parts.size())
            it->items.push_back(clip);
        else
            self(self, it->children, parts, depth + 1, clip);
    };

    for (const auto& p : state.clips) {
        std::filesystem::path fp(p);
        std::vector<std::string> parts;
        for (const auto& part : fp.parent_path()) {
            if (!part.string().empty()) parts.push_back(part.string());
        }
        if (parts.empty()) parts.push_back("generic");
        if (parts.size() >= 2 && parts.front() == "tasks") {
            const std::string& taskFolder = parts[1];
            const size_t categoryEnd = taskFolder.find_first_of("0123456789_");
            const std::string category = taskFolder.substr(0, categoryEnd);
            parts.insert(parts.begin() + 1, category.empty() ? "Other" : category);
        }
        addClip(addClip, groups, parts, 0, p);
    }

    auto sortGroups = [&](auto&& self, std::vector<ClipGroupEntry>& current) -> void {
        std::sort(current.begin(), current.end(), [](const ClipGroupEntry& a, const ClipGroupEntry& b) {
            if (a.group == "generic") return true;
            if (b.group == "generic") return false;
            return a.group < b.group;
        });
        for (auto& group : current) self(self, group.children);
    };
    sortGroups(sortGroups, groups);
}

void RewardTesterPlugin::OpenReplayClip(const std::string& path) {
    if (!LoadReplayFrames(path)) return;
    activeReplayPath = path;
    replayActive = true;
    replayTime = 0.0f;
    replayFrameIndex = 0;
    replayLastWorldTime = -1.0f;
    replayNeedsReseed = true;
    replayCarAddresses.clear();
    replayCarStateIndices.clear();
    replayBotRequests = 0;
    replayBotCooldownUntil = 0.0;
    isPlayingClip = true;
    cvarManager->log("[RewardTester] Loaded replay clip: " + path +
                     " (" + std::to_string(replayFrames.size()) + " frames)");
    gameWrapper->Toast("Reward Tester", "Replay loaded", "default", 2.0f, ToastType_Info);
}

void RewardTesterPlugin::StopReplayClip() {
    replayActive = false;
    isPlayingClip = false;
    activeReplayPath.clear();
    loadedReplayPath.clear();
    replayFrames.clear();
    replayFrameIntervalSeconds = 8.0f / 120.0f;
    replayTime = 0.0f;
    replayFrameIndex = 0;
    replayLastWorldTime = -1.0f;
    replayNeedsReseed = true;
    replayCarAddresses.clear();
    replayCarStateIndices.clear();
    replayStateDrivenFallback = false;
}

void RewardTesterPlugin::UpdateReplayPlayback(ServerWrapper server, float dt) {
    if (!replayActive || activeReplayPath.empty() || !server || server.IsNull()) return;
    if (isPlayingClip) {
        replayTime += dt * replayRate;
        const float duration = replayFrames.empty()
                                   ? 0.0f
                                   : static_cast<float>(replayFrames.size() - 1) * replayFrameIntervalSeconds;
        if (duration > 0.0f && replayTime > duration) {
            if (replayLoop) {
                replayTime = 0.0f;
                replayNeedsReseed = true;
            }
            else {
                replayTime = duration;
                isPlayingClip = false;
            }
        }
    }
    const auto begin = std::chrono::steady_clock::now();
    ApplyReplayFrame(server, activeReplayPath, replayTime);
    replayDiagMicros += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
    ++replayDiagTicks;
    const double now = NowSeconds();
    if (now - replayDiagLast >= 1.0) {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "[replay-diag] ticks/s=%d avg_apply_us=%.1f clip_cars=%zu live_cars=%zu bots_requested=%d playing=%d rate=%.2f",
                      replayDiagTicks, replayDiagTicks ? replayDiagMicros / replayDiagTicks : 0.0, replayCarCount,
                      replayLiveCars, replayBotRequests, isPlayingClip ? 1 : 0, replayRate);
        cvarManager->log(line);
        replayDiagLast = now;
        replayDiagTicks = 0;
        replayDiagMicros = 0.0;
    }
}

bool RewardTesterPlugin::LoadReplayFrames(const std::string &path) {
    if (!replayFrames.empty() && path == activeReplayPath) return true;
    std::vector<RLGC::GameState> frames;
    std::vector<std::string> candidates;
    candidates.push_back(path);
    if (client_) {
        const auto clipState = client_->LatestClipState();
        if (!clipState.folder.empty())
            candidates.push_back((std::filesystem::path(clipState.folder) / path).string());
    }
    if (const char *appdata = std::getenv("APPDATA")) {
        candidates.push_back((std::filesystem::path(appdata) / "bakkesmod/bakkesmod/data/reward_clips" / path).string());
    }

    std::string resolvedPath;
    for (const auto &candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            resolvedPath = candidate;
            break;
        }
    }
    RLGC::RLPRMetadata metadata;
    if (resolvedPath.empty() || !RLGC::ImportClipFromRLPR(resolvedPath, frames, &metadata) || frames.empty()) {
        cvarManager->log("[RewardTester] Failed to load replay clip: " + path);
        gameWrapper->Toast("Reward Tester", "Failed to load replay clip", "default", 3.0f, ToastType_Error);
        activeReplayPath.clear();
        return false;
    }
    replayFrames = std::move(frames);
    replayCarCount = replayFrames.front().players.size();
    replayStateDrivenFallback = true;
    for (const auto& frame : replayFrames) {
        for (const auto& player : frame.players) {
            for (float value : player.prevAction) {
                if (std::abs(value) > 0.001f) {
                    replayStateDrivenFallback = false;
                    break;
                }
            }
            if (!replayStateDrivenFallback) break;
        }
        if (!replayStateDrivenFallback) break;
    }
    replayFrameIntervalSeconds = metadata.frameIntervalSeconds > 0.0f
                                     ? metadata.frameIntervalSeconds
                                     : 8.0f / 120.0f;
    loadedReplayPath = resolvedPath;
    return true;
}

static Quat RotMatToQuat(const RocketSim::RotMat &m) {
    btQuaternion bulletQuat;
    ((btMatrix3x3)m).getRotation(bulletQuat);
    return Quat(bulletQuat.w(), bulletQuat.x(), bulletQuat.y(), bulletQuat.z()).normalize();
}

static RocketSim::RotMat InterpolateRotMat(const RocketSim::RotMat& from,
                                           const RocketSim::RotMat& to,
                                           float ratio) {
    btQuaternion fromQuat;
    btQuaternion toQuat;
    ((btMatrix3x3)from).getRotation(fromQuat);
    ((btMatrix3x3)to).getRotation(toQuat);
    return btMatrix3x3(fromQuat.slerp(toQuat, ratio));
}

template <typename State>
static RBState ReplayRBState(const State &state) {
    RBState rb;
    rb.Quaternion = RotMatToQuat(state.rotMat);
    rb.Location = Vector(state.pos.x, state.pos.y, state.pos.z);
    rb.LinearVelocity = Vector(state.vel.x, state.vel.y, state.vel.z);
    rb.AngularVelocity = Vector(state.angVel.x, state.angVel.y, state.angVel.z);
    rb.Time = 0.0f;
    rb.bSleeping = 0;
    rb.bNewData = 1;
    return rb;
}

void RewardTesterPlugin::ApplyReplayFrame(ServerWrapper server, const std::string &path, float t) {
    if (!LoadReplayFrames(path) || replayFrames.empty()) return;
    const float framePosition = std::max(0.0f, t) / replayFrameIntervalSeconds;
    const size_t fromFrame = std::min(replayFrames.size() - 1, static_cast<size_t>(framePosition));
    const size_t toFrame = std::min(replayFrames.size() - 1, fromFrame + 1);
    const float ratio = std::clamp(framePosition - static_cast<float>(fromFrame), 0.0f, 1.0f);
    replayFrameIndex = fromFrame;

    RLGC::GameState state = replayFrames[fromFrame];
    if (toFrame != fromFrame && state.players.size() == replayFrames[toFrame].players.size()) {
        const RLGC::GameState& next = replayFrames[toFrame];
        auto lerp = [ratio](float a, float b) { return a + (b - a) * ratio; };
        auto lerpVec = [&](RocketSim::Vec& out, const RocketSim::Vec& a, const RocketSim::Vec& b) {
            out.x = lerp(a.x, b.x);
            out.y = lerp(a.y, b.y);
            out.z = lerp(a.z, b.z);
        };
        for (size_t i = 0; i < state.players.size(); ++i) {
            lerpVec(state.players[i].pos, replayFrames[fromFrame].players[i].pos, next.players[i].pos);
            lerpVec(state.players[i].vel, replayFrames[fromFrame].players[i].vel, next.players[i].vel);
            lerpVec(state.players[i].angVel, replayFrames[fromFrame].players[i].angVel, next.players[i].angVel);
            state.players[i].rotMat = InterpolateRotMat(
                replayFrames[fromFrame].players[i].rotMat, next.players[i].rotMat, ratio);
        }
        lerpVec(state.ball.pos, replayFrames[fromFrame].ball.pos, next.ball.pos);
        lerpVec(state.ball.vel, replayFrames[fromFrame].ball.vel, next.ball.vel);
        lerpVec(state.ball.angVel, replayFrames[fromFrame].ball.angVel, next.ball.angVel);
        state.ball.rotMat = InterpolateRotMat(
            replayFrames[fromFrame].ball.rotMat, next.ball.rotMat, ratio);
    }
    if (funReplayMode) {
        for (auto& player : state.players) {
            const RocketSim::Vec forward = player.rotMat.forward;
            const RocketSim::Vec right = player.rotMat.right;
            player.rotMat.forward = {right.x, -forward.y, right.z};
            player.rotMat.right = {-forward.x, right.y, -forward.z};
            player.rotMat.up = {-player.rotMat.up.x, player.rotMat.up.y, -player.rotMat.up.z};
        }
    }
    ApplyReplayState(server, state);
}

void RewardTesterPlugin::ApplyReplayState(ServerWrapper server, const RLGC::GameState &state) {
    if (!server || server.IsNull() || state.players.empty()) return;

    ArrayWrapper<CarWrapper> liveCars = server.GetCars();
    std::vector<CarWrapper> ordered;
    CarWrapper local = gameWrapper->GetLocalCar();
    if (local && !local.IsNull()) ordered.push_back(local);
    for (int i = 0; i < liveCars.Count(); ++i) {
        CarWrapper car = liveCars.Get(i);
        if (!car || car.IsNull()) continue;
        bool alreadyAdded = false;
        for (const auto &existing : ordered)
            if (existing.memory_address == car.memory_address) alreadyAdded = true;
        if (!alreadyAdded) ordered.push_back(car);
    }

    replayLiveCars = ordered.size();
    if (ordered.size() < state.players.size() && server.CanSpawnBots()) {
        const double now = NowSeconds();
        if (now >= replayBotCooldownUntil && replayBotRequests < 6) {
            for (size_t i = ordered.size(); i < state.players.size(); ++i) {
                server.SpawnBot(0, "RewardReplay");
                ++replayBotRequests;
            }
            replayBotCooldownUntil = now + 3.0;
        }
    }

    if (!ReplayStateDriven() && !replayNeedsReseed &&
        replayCarAddresses.size() == state.players.size())
        return;

    std::vector<bool> used(state.players.size(), false);
    replayCarAddresses.clear();
    replayCarStateIndices.clear();
    for (size_t liveIdx = 0; liveIdx < ordered.size(); ++liveIdx) {
        CarWrapper car = ordered[liveIdx];
        int stateIdx = -1;
        if (liveIdx == 0) {
            const int liveTeam = car.GetTeamNum2();
            for (size_t i = 0; i < state.players.size(); ++i) {
                const int replayTeam = state.players[i].team == RocketSim::Team::ORANGE ? 1 : 0;
                if (replayTeam == liveTeam) {
                    stateIdx = static_cast<int>(i);
                    break;
                }
            }
            if (stateIdx < 0) stateIdx = 0;
            used[stateIdx] = true;
        }
        else {
            const int liveTeam = car.GetTeamNum2();
            for (size_t i = 0; i < state.players.size(); ++i) {
                if (used[i]) continue;
                const int replayTeam = state.players[i].team == RocketSim::Team::ORANGE ? 1 : 0;
                if (replayTeam == liveTeam) {
                    stateIdx = static_cast<int>(i);
                    break;
                }
            }
            if (stateIdx < 0)
                for (size_t i = 0; i < state.players.size(); ++i)
                    if (!used[i]) {
                        stateIdx = static_cast<int>(i);
                        break;
                    }
        }

        if (stateIdx >= 0) {
            used[stateIdx] = true;
            replayCarAddresses.push_back(car.memory_address);
            replayCarStateIndices.push_back(static_cast<size_t>(stateIdx));
            if (ReplayStateDriven() || replayNeedsReseed) {
                RBState rb = ReplayRBState(state.players[stateIdx]);
                car.SetRBState(rb);
                car.SetPhysicsState(rb);
            }
        } else {
            if (ReplayStateDriven() || replayNeedsReseed) {
                RBState parked = car.GetRBState();
                parked.Location = Vector(0.0f, 0.0f, -5000.0f);
                parked.LinearVelocity = Vector(0.0f, 0.0f, 0.0f);
                parked.AngularVelocity = Vector(0.0f, 0.0f, 0.0f);
                parked.bNewData = 1;
                car.SetRBState(parked);
                car.SetPhysicsState(parked);
            }
        }
    }

    if (ReplayStateDriven() || replayNeedsReseed) {
        BallWrapper ball = server.GetBall();
        if (ball && !ball.IsNull()) {
            RBState rb = ReplayRBState(state.ball);
            ball.SetRBState(rb);
            ball.SetPhysicsState(rb);
        }
        if (!ReplayStateDriven())
            replayNeedsReseed = false;
    }
}

void RewardTesterPlugin::ApplyReplayInput(CarWrapper car, ControllerInput *input) {
    if (!input || !replayActive || replayFrames.empty() || funReplayMode || replayStateDrivenFallback) {
        if (input) *input = {};
        return;
    }

    auto it = std::find(replayCarAddresses.begin(), replayCarAddresses.end(), car.memory_address);
    if (it == replayCarAddresses.end()) {
        *input = {};
        return;
    }

    const size_t bindingIndex = static_cast<size_t>(std::distance(replayCarAddresses.begin(), it));
    if (bindingIndex >= replayCarStateIndices.size()) {
        *input = {};
        return;
    }

    const size_t frame = std::min(
        replayFrames.size() - 1,
        static_cast<size_t>(std::max(0.0f, replayTime) / replayFrameIntervalSeconds));
    const size_t playerIndex = replayCarStateIndices[bindingIndex];
    if (playerIndex >= replayFrames[frame].players.size()) {
        *input = {};
        return;
    }

    const size_t actionFrame = std::min(replayFrames.size() - 1, frame + 1);
    const RLGC::Action &action = replayFrames[actionFrame].players[playerIndex].prevAction;
    input->Throttle = action.throttle;
    input->Steer = action.steer;
    input->Pitch = action.pitch;
    input->Yaw = action.yaw;
    input->Roll = action.roll;
    input->DodgeForward = -action.pitch;
    input->DodgeStrafe = action.yaw;
    input->Jump = action.jump > 0.5f;
    input->HoldingBoost = action.boost > 0.5f;
    input->ActivateBoost = input->HoldingBoost;
    input->Handbrake = action.handbrake > 0.5f;
}

std::string RewardTesterPlugin::GetPluginName() { return "Reward Tester"; }

void RewardTesterPlugin::RenderSettings() {
    if (ImGui::GetCurrentContext() == nullptr) return;

    ImGui::TextUnformatted("Reward Tester & State Inspector (TCP)");
    ImGui::Separator();

    if (ImGui::Button("Open Floating Reward & State Inspector Window")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("togglemenu " + GetMenuName());
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Config")) ReloadConfig();

    ImGui::Separator();
    bool connected = client_ && client_->Connected();
    if (connected)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Server: CONNECTED (%d terms)",
                           client_ ? client_->RewardCount() : 0);
    else
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Server: DISCONNECTED -- %s",
                           client_ ? client_->LastError().c_str() : "no client");
    ImGui::Text("Physics Ticks Evaluated: %llu", (unsigned long long) physicsTickCount);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "Active Keybinds (your current binds):");
    ImGui::TextDisabled("These match your configured BakkesMod bindings. Use the Apply buttons to re-apply if lost.");

    if (ImGui::Button("NumPad0 -> Toggle Window")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("bind NumPadZero \"rewardtester_toggle_menu\"");
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("X -> Save Task Clip")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("bind X \"rewardtester_save_task_clip\"");
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("C -> Save Generic Clip")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("bind C \"rewardtester_save_clip\"");
        });
    }

    if (ImGui::Button("NumPad+ -> Next Task")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("bind NumPadAdd \"rewardtester_next_task\"");
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("NumPad- -> Prev Task")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("bind NumPadSubtract \"rewardtester_prev_task\"");
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("NumPad* -> Mark Scored")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("bind NumPadMultiply \"rewardtester_mark_scored\"");
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("NumPad/ -> Mark Miss")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("bind NumPadDivide \"rewardtester_mark_miss\"");
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("NumPad. -> Undo Attempt")) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("bind Decimal \"rewardtester_undo_attempt\"");
        });
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Keybind Reference:");
    if (ImGui::BeginChild("KeybindRef", ImVec2(0, 175), true)) {
        ImGui::Columns(3, "keybinds", true);
        ImGui::TextUnformatted("Key");       ImGui::NextColumn();
        ImGui::TextUnformatted("Command");   ImGui::NextColumn();
        ImGui::TextUnformatted("Description"); ImGui::NextColumn();
        ImGui::Separator();

        struct BindRow { const char *key; const char *cmd; const char *desc; };
        static const BindRow rows[] = {
            { "NumPad 0",       "rewardtester_toggle_menu",    "Toggle the floating Inspector window"       },
            { "X",              "rewardtester_save_task_clip",  "Save 15s clip tagged for active shot task"  },
            { "NumPad *",       "rewardtester_mark_scored",     "Mark active attempt as Scored"              },
            { "NumPad /",       "rewardtester_mark_miss",       "Mark active attempt as Miss"                },
            { "NumPad .",       "rewardtester_undo_attempt",    "Remove the latest attempt from the counters" },
            { "NumPad +",       "rewardtester_next_task",       "Switch to next task in catalog"             },
            { "NumPad -",       "rewardtester_prev_task",       "Switch to previous task in catalog"         },
            { "C",              "rewardtester_save_clip",       "Save generic timestamped 15s clip"          },
            { "(console only)", "rewardtester_reset",           "Clear per-episode reward state"           },
            { "(console only)", "rewardtester_reload",          "Reload reward config JSON on server"        },
        };
        for (const auto &r : rows) {
            ImGui::TextUnformatted(r.key);        ImGui::NextColumn();
            ImGui::TextDisabled("%s", r.cmd);     ImGui::NextColumn();
            ImGui::TextUnformatted(r.desc);       ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::EndChild();
    }
}

std::string RewardTesterPlugin::GetMenuName() { return "rewardtesterplugin"; }
std::string RewardTesterPlugin::GetMenuTitle() { return "Reward & State Inspector"; }

bool RewardTesterPlugin::ShouldBlockInput() {
    if (!isWindowOpen) return false;
    if (ImGui::GetCurrentContext() == nullptr) return false;
    return ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
}

bool RewardTesterPlugin::IsActiveOverlay() { return true; }
void RewardTesterPlugin::OnOpen() { isWindowOpen = true; }
void RewardTesterPlugin::OnClose() { isWindowOpen = false; }

void RewardTesterPlugin::SetImGuiContext(uintptr_t ctx) {
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext *>(ctx));
}

void RewardTesterPlugin::Render() {
    if (ImGui::GetCurrentContext() == nullptr || !isWindowOpen) return;

    HandleReplayKeys();

    ImGui::SetNextWindowSize(ImVec2(680, 760), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Reward & State Inspector", &isWindowOpen, ImGuiWindowFlags_NoCollapse)) {
        DrawHeader();

        if (ImGui::BeginTabBar("InspectorTabs")) {
            if (ImGui::BeginTabItem("Live")) {
                DrawLiveTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Shots")) {
                DrawShotsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Clips")) {
                DrawClipsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(watch.inReplay ? "Watch *" : "Watch")) {
                DrawWatchTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                DrawSettingsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    DrawReplayOverlay();

    if (!isWindowOpen) {
        gameWrapper->Execute([this](GameWrapper *gw) {
            cvarManager->executeCommand("togglemenu " + GetMenuName());
        });
    }
}

