#include "reward_eval.h"
#include "user_reward.h"
#include "Sim/Arena/Arena.h"
#include "Sim/GameEventTracker/GameEventTracker.h"
#include "Sim/BoostPad/BoostPad.h"
#include "RLGymCPP/CommonValues.h"
#include "rlpr/RLPRExporter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace {
using nlohmann::json;

void ReadVec3(const json& j, const char* key, Vec& out) {
    out = {0, 0, 0};
    if (!j.contains(key)) return;
    const auto& a = j[key];
    if (a.size() >= 3) out = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}

void ReadRotMat(const json& j, const char* key, RotMat& out) {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (j.contains(key) && j[key].size() >= 9)
        for (int i = 0; i < 9; ++i) m[i] = j[key][i].get<float>();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r][c] = m[r * 3 + c];
}
}

namespace rs {

RewardEval::~RewardEval() {
    if (tracker_) { delete tracker_; tracker_ = nullptr; }
    if (arena_) { delete arena_; arena_ = nullptr; }
}

int RewardEval::MirrorIndexOf(RocketSim::Car* car) const {
    for (size_t i = 0; i < mirrorCars_.size(); ++i)
        if (mirrorCars_[i] == car) return (int)i;
    return -1;
}

void RewardEval::RecordGoal(RocketSim::Car* scorer, RocketSim::Car* passer) {
    int s = MirrorIndexOf(scorer), p = MirrorIndexOf(passer);
    if (s >= 0 && s < (int)pendingEvents_.size()) pendingEvents_[s].goal = true;
    if (p >= 0 && p < (int)pendingEvents_.size()) pendingEvents_[p].assist = true;
}

void RewardEval::RecordShot(RocketSim::Car* shooter, RocketSim::Car* passer) {
    int s = MirrorIndexOf(shooter), p = MirrorIndexOf(passer);
    if (s >= 0 && s < (int)pendingEvents_.size()) pendingEvents_[s].shot = true;
    if (p >= 0 && p < (int)pendingEvents_.size()) pendingEvents_[p].shotPass = true;
}

void RewardEval::RecordSave(RocketSim::Car* saver) {
    int s = MirrorIndexOf(saver);
    if (s >= 0 && s < (int)pendingEvents_.size()) pendingEvents_[s].save = true;
}

void RewardEval::RecordBump(RocketSim::Car* bumper, RocketSim::Car* victim, bool isDemo) {
    int b = MirrorIndexOf(bumper), v = MirrorIndexOf(victim);
    if (b >= 0 && b < (int)pendingEvents_.size()) { pendingEvents_[b].bump = true; if (isDemo) pendingEvents_[b].demo = true; }
    if (v >= 0 && v < (int)pendingEvents_.size()) { pendingEvents_[v].bumped = true; if (isDemo) pendingEvents_[v].demoed = true; }
}

void RewardEval::ReconstructState(RLGC::GameState& gs, const RLGC::GameState* previous, int tickSkip) {
    if (!mirrorEnabled_) return;
    for (auto& player : gs.players) {
        player.ballTouchedStep = player.ballTouchedTick = false;
        player.ballHitInfo = {};
        player.eventState = {};
    }
    bool continuous = previous && previous->players.size() == gs.players.size();
    const float maxTravel = 400.0f + 10000.0f * std::max(1, tickSkip) / 120.0f;
    if (continuous) {
        continuous = (gs.ball.pos - previous->ball.pos).LengthSq() <= maxTravel * maxTravel;
        for (size_t i = 0; continuous && i < gs.players.size(); ++i) {
            continuous = gs.players[i].carId == previous->players[i].carId &&
                gs.players[i].team == previous->players[i].team &&
                (gs.players[i].pos - previous->players[i].pos).LengthSq() <= maxTravel * maxTravel;
        }
    }
    if (!continuous) {
        if (tracker_) tracker_->ResetPersistentInfo();
        return;
    }
    MirrorArena(gs, *previous, tickSkip);
}

void RewardEval::MirrorArena(RLGC::GameState& gs, const RLGC::GameState& previous, int tickSkip) {
    if (!mirrorEnabled_ || gs.players.empty()) return;
    if (tickSkip < 1) tickSkip = 1;

    if (!arena_ || mirrorCars_.size() != gs.players.size()) {
        if (tracker_) { delete tracker_; tracker_ = nullptr; }
        if (arena_) { delete arena_; arena_ = nullptr; }
        mirrorCars_.clear();
        arena_ = RocketSim::Arena::Create(RocketSim::GameMode::SOCCAR);
        for (const auto& p : gs.players)
            mirrorCars_.push_back(arena_->AddCar(p.team));

        arena_->SetCarBumpCallback(
            [](RocketSim::Arena*, RocketSim::Car* bumper, RocketSim::Car* victim, bool isDemo, void* ui) {
                static_cast<RewardEval*>(ui)->RecordBump(bumper, victim, isDemo);
            }, this);

        tracker_ = new RocketSim::GameEventTracker({});
        tracker_->autoStateSetDetection = false;
        tracker_->SetGoalCallback(
            [](RocketSim::Arena*, RocketSim::Car* scorer, RocketSim::Car* passer, void* ui) {
                static_cast<RewardEval*>(ui)->RecordGoal(scorer, passer);
            }, this);
        tracker_->SetShotCallback(
            [](RocketSim::Arena*, RocketSim::Car* shooter, RocketSim::Car* passer, void* ui) {
                static_cast<RewardEval*>(ui)->RecordShot(shooter, passer);
            }, this);
        tracker_->SetSaveCallback(
            [](RocketSim::Arena*, RocketSim::Car* saver, void* ui) {
                static_cast<RewardEval*>(ui)->RecordSave(saver);
            }, this);

        padIndexMap_.assign(RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT, -1);
        for (int i = 0; i < RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT; ++i) {
            const Vec target = RLGC::CommonValues::BOOST_LOCATIONS[i];
            for (size_t j = 0; j < arena_->_boostPads.size(); ++j) {
                if (arena_->_boostPads[j]->config.pos.DistSq2D(target) < 10.0f) {
                    padIndexMap_[i] = (int)j;
                    break;
                }
            }
        }
    }
    if (mirrorCars_.size() != gs.players.size()) return;

    pendingEvents_.assign(gs.players.size(), PendingEvent{});

    for (size_t i = 0; i < gs.players.size(); ++i) {
        RocketSim::Car* car = mirrorCars_[i];
        RocketSim::CarState cs = car->GetState();
        const RLGC::Player& p = previous.players[i];
        cs.pos = p.pos; cs.vel = p.vel; cs.angVel = p.angVel; cs.rotMat = p.rotMat;
        cs.boost = p.boost;
        cs.isDemoed = p.isDemoed; cs.demoRespawnTimer = p.demoRespawnTimer;
        cs.isOnGround = p.isOnGround;
        cs.hasJumped = p.hasJumped;
        cs.hasDoubleJumped = p.hasDoubleJumped;
        cs.hasFlipped = p.hasFlipped;
        cs.isFlipping = p.isFlipping;
        cs.flipTime = p.flipTime;
        cs.flipRelTorque = p.flipRelTorque;
        cs.isJumping = p.isJumping;
        cs.jumpTime = p.jumpTime;
        cs.airTimeSinceJump = p.airTimeSinceJump;
        cs.ballHitInfo = {};
        cs.worldContact.hasContact = p.worldContact.hasContact;
        cs.worldContact.contactNormal = p.worldContact.contactNormal;
        car->SetState(cs);

        RocketSim::CarControls cc;
        cc.throttle = p.prevAction.throttle; cc.steer = p.prevAction.steer;
        cc.pitch = p.prevAction.pitch; cc.yaw = p.prevAction.yaw; cc.roll = p.prevAction.roll;
        cc.jump = p.prevAction.jump > 0.5f;
        cc.boost = p.prevAction.boost > 0.5f;
        cc.handbrake = p.prevAction.handbrake > 0.5f;
        car->controls = cc;
    }

    RocketSim::BallState bs = arena_->ball->GetState();
    bs.pos = previous.ball.pos; bs.vel = previous.ball.vel; bs.angVel = previous.ball.angVel;
    arena_->ball->SetState(bs);

    for (int i = 0; i < (int)padIndexMap_.size(); ++i) {
        if (padIndexMap_[i] < 0) continue;
        auto pad = arena_->_boostPads[padIndexMap_[i]];
        auto padState = pad->GetState();
        padState.isActive = previous.boostPads[i];
        padState.cooldown = previous.boostPadTimers[i];
        pad->SetState(padState);
    }

    arena_->Step(tickSkip);
    if (tracker_) {
        tracker_->_lastBallUpdateCount = 0;
        tracker_->Update(arena_);
    }

    gs.goalScored = gs.goalScored || arena_->IsBallScored();

    for (int i = 0; i < RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT && i < (int)padIndexMap_.size(); ++i) {
        const int j = padIndexMap_[i];
        if (j < 0 || j >= (int)arena_->_boostPads.size()) continue;
        const auto st = arena_->_boostPads[j]->GetState();
        gs.boostPads[i] = st.isActive;
        gs.boostPadTimers[i] = st.cooldown;
    }

    for (size_t i = 0; i < gs.players.size(); ++i) {
        const RocketSim::CarState cs = mirrorCars_[i]->GetState();
        RLGC::Player& p = gs.players[i];
        p.isOnGround = cs.isOnGround;
        std::memcpy(p.wheelsWithContact, cs.wheelsWithContact, sizeof(p.wheelsWithContact));
        p.hasJumped = cs.hasJumped || p.hasJumped;
        p.hasDoubleJumped = cs.hasDoubleJumped || p.hasDoubleJumped;
        p.hasFlipped = cs.hasFlipped || p.hasFlipped;
        p.isFlipping = cs.isFlipping || p.isFlipping;
        p.isJumping = cs.isJumping || p.isJumping;
        p.jumpTime = cs.jumpTime > 0.0f ? cs.jumpTime : p.jumpTime;
        p.flipTime = cs.flipTime > 0.0f ? cs.flipTime : p.flipTime;
        p.airTimeSinceJump = cs.airTimeSinceJump > 0.0f ? cs.airTimeSinceJump : p.airTimeSinceJump;
        if (p.flipRelTorque.LengthSq() <= 1e-4f && cs.flipRelTorque.LengthSq() > 1e-4f) {
            p.flipRelTorque = cs.flipRelTorque;
        }
        bool hadLiveContact = p.worldContact.hasContact;
        Vec liveContactNormal = p.worldContact.contactNormal;
        p.worldContact = cs.worldContact;
        if (hadLiveContact) {
            p.worldContact.hasContact = true;
            if (liveContactNormal.LengthSq() > 0.01f) {
                p.worldContact.contactNormal = liveContactNormal;
            }
        }
        p.ballHitInfo = cs.ballHitInfo;
        const uint64_t tick = arena_->tickCount;
        p.ballTouchedStep = cs.ballHitInfo.isValid && cs.ballHitInfo.tickCountWhenHit >= tick - (uint64_t)tickSkip;
        p.ballTouchedTick = cs.ballHitInfo.isValid && cs.ballHitInfo.tickCountWhenHit == tick - 1;
        if (p.ballTouchedStep) {
            p.ballHitInfo.tickCountWhenHit = gs.lastTickCount - std::min<uint64_t>(gs.lastTickCount,
                tick - cs.ballHitInfo.tickCountWhenHit);
            gs.lastTouchCarID = p.carId;
        }

        const PendingEvent& e = pendingEvents_[i];
        p.eventState.goal = e.goal;
        p.eventState.assist = e.assist;
        p.eventState.shot = e.shot;
        p.eventState.shotPass = e.shotPass;
        p.eventState.save = e.save;
        p.eventState.bump = e.bump;
        p.eventState.bumped = e.bumped;
        p.eventState.demo = e.demo;
        p.eventState.demoed = e.demoed;
    }
}

bool RewardEval::Load(const std::string& configPath, std::string& err) {
    try {
        auto getStep = [this]() -> uint64_t { return currentStep_; };
        auto next = CreateUserReward(configPath, getStep);
        if (!next) { err = "CreateUserReward returned null"; return false; }
        reward_ = std::move(next);
        if (!extension_) extension_ = CreateEvalExtension();
        if (extension_) extension_->OnLoad(*reward_);
        names_ = reward_->Names();
        havePrev_ = false;
        return true;
    } catch (const std::exception& e) {
        err = std::string("CreateUserReward failed: ") + e.what();
        return false;
    }
}

void RewardEval::Reset() {
    haveCapturePrev_ = false;
    if (extension_) extension_->OnReset();
    if (tracker_) tracker_->ResetPersistentInfo();
    if (captureMirror_ && captureMirror_->tracker_) captureMirror_->tracker_->ResetPersistentInfo();
    if (!reward_) return;
    RLGC::GameState initGs;
    if (havePrev_) {
        initGs = prev_;
        initGs.goalScored = false;
        initGs.prev = nullptr;
        for (auto& p : initGs.players) {
            p.isOnGround = true;
            p.hasJumped = false; p.hasDoubleJumped = false;
            p.hasFlipped = false; p.isFlipping = false;
            p.ballTouchedStep = false; p.ballTouchedTick = false;
            p.eventState = {};
            p.prev = nullptr;
        }
    } else {
        initGs.lastTickCount = tickCounter_;
    }
    reward_->Reset(initGs);
    havePrev_ = false;
    printf("[server] reward state reset (players=%d)\n", (int)initGs.players.size());
}

bool RewardEval::BuildGameState(const json& req, RLGC::GameState& gs) const {
    gs.goalScored = req.value("goal_scored", false);

    const json& ball = req.contains("ball") ? req["ball"] : json::object();
    ReadVec3(ball, "pos", gs.ball.pos);
    ReadVec3(ball, "vel", gs.ball.vel);
    ReadVec3(ball, "ang_vel", gs.ball.angVel);
    ReadRotMat(ball, "rot_mat", gs.ball.rotMat);

    if (req.contains("game")) {
        const json& g = req["game"];
        if (g.contains("boost_pad_states"))
            for (int i = 0; i < 34 && i < (int)g["boost_pad_states"].size(); ++i)
                gs.boostPads[i] = g["boost_pad_states"][i].get<int>();
        if (g.contains("boost_pad_timers"))
            for (int i = 0; i < 34 && i < (int)g["boost_pad_timers"].size(); ++i)
                gs.boostPadTimers[i] = g["boost_pad_timers"][i].get<float>();
    }

    const json& cars = req.contains("cars") ? req["cars"] : json::array();
    if (cars.empty()) return false;
    gs.players.resize(cars.size());
    for (size_t i = 0; i < cars.size(); ++i) {
        const json& c = cars[i];
        auto& p = gs.players[i];
        p.carId = static_cast<int>(i) + 1;
        p.index = static_cast<int>(i);
        p.team = (c.value("team", 0) == 0) ? RocketSim::Team::BLUE : RocketSim::Team::ORANGE;
        ReadVec3(c, "pos", p.pos);
        ReadVec3(c, "vel", p.vel);
        ReadVec3(c, "ang_vel", p.angVel);
        ReadRotMat(c, "rot_mat", p.rotMat);
        p.boost = c.value("boost", 0.0f);
        p.isOnGround = c.value("is_on_ground", 0) != 0;
        p.hasJumped = c.value("has_jumped", 0) != 0;
        p.hasDoubleJumped = c.value("has_double_jumped", 0) != 0;
        p.hasFlipped = c.value("has_flipped", 0) != 0;
        p.isFlipping = c.value("is_flipping", 0) != 0;
        p.isJumping = c.value("is_jumping", 0) != 0;
        p.jumpTime = c.value("jump_time", 0.0f);
        p.flipTime = c.value("flip_time", 0.0f);
        p.airTimeSinceJump = c.value("air_time_since_jump", 0.0f);
        p.isSupersonic = c.value("is_supersonic", 0) != 0;
        p.handbrakeVal = c.value("handbrake_val", 0.0f);
        ReadVec3(c, "flip_rel_torque", p.flipRelTorque);
        p.worldContact.hasContact = c.value("world_contact_has", 0) != 0;
        ReadVec3(c, "world_contact_normal", p.worldContact.contactNormal);
        if (c.contains("prev_action")) {
            const json& a = c["prev_action"];
            p.prevAction.throttle = a.value("throttle", 0.0f);
            p.prevAction.steer = a.value("steer", 0.0f);
            p.prevAction.pitch = a.value("pitch", 0.0f);
            p.prevAction.yaw = a.value("yaw", 0.0f);
            p.prevAction.roll = a.value("roll", 0.0f);
            p.prevAction.jump = a.value("jump", 0.0f);
            p.prevAction.boost = a.value("boost", 0.0f);
            p.prevAction.handbrake = a.value("handbrake", 0.0f);
        }
        p.isDemoed = c.value("is_demoed", 0) != 0;
        p.demoRespawnTimer = c.value("demo_respawn_timer", 0.0f);
        bool touched = c.value("is_ball_touched", 0) != 0;
        p.ballTouchedStep = touched;
        p.ballTouchedTick = touched;
    }
    int explicitTouch = req.value("last_touch_car", -1);
    if (explicitTouch >= 0 && explicitTouch < (int)gs.players.size()) {
        gs.lastTouchCarID = gs.players[explicitTouch].carId;
    } else {
        for (size_t i = 0; i < cars.size(); ++i)
            if (cars[i].value("is_ball_touched", 0) != 0) {
                gs.lastTouchCarID = gs.players[i].carId;
                break;
            }
    }
    return true;
}

bool RewardEval::Evaluate(const json& req, RewardTerms& out) {
    if (!reward_) return false;

    bool forceAll = req.value("force_all", true);
    currentStep_ = forceAll ? 500000000000ULL : req.value("step", (uint64_t)500000000000ULL);
    const int tickSkip = std::max(1, req.value("tick_skip", 8));
    RLGC::GameState gs;
    if (!BuildGameState(req, gs)) return false;
    return EvaluateState(gs, tickSkip, req.value("focus", 0), req.value("debug", false), out);
}

bool RewardEval::Capture(const json& req) {
    RLGC::GameState gs;
    if (!BuildGameState(req, gs)) return false;
    gs.prev = nullptr;
    gs.userInfo = nullptr;
    gs.deltaTime = 1.0f / 120.0f;
    gs.lastTickCount = ++captureTickCounter_;
    if (!captureMirror_) captureMirror_ = std::make_unique<RewardEval>();
    captureMirror_->SetMirrorEnabled(mirrorEnabled_);
    captureMirror_->ReconstructState(gs,
        haveCapturePrev_ && !captureBuffer_.empty() ? &captureBuffer_.back() : nullptr, 1);
    captureBuffer_.push_back(std::move(gs));
    haveCapturePrev_ = true;
    const uint64_t windowTicks = (uint64_t)(clipWindowSeconds_ * 120.0f);
    while (captureBuffer_.size() > 1 &&
           captureBuffer_.back().lastTickCount - captureBuffer_.front().lastTickCount > windowTicks) {
        captureBuffer_.pop_front();
    }
    return true;
}

bool RewardEval::EvaluateState(RLGC::GameState& gs, int tickSkip, int focus, bool debug, RewardTerms& out,
                               bool reconstruct) {
    if (!reward_ || gs.players.empty()) return false;
    tickSkip = std::max(1, tickSkip);

    gs.prev = havePrev_ ? &prev_ : nullptr;
    gs.deltaTime = static_cast<float>(tickSkip) / 120.0f;
    for (size_t i = 0; i < gs.players.size(); ++i)
        gs.players[i].prev = (havePrev_ && i < prev_.players.size()) ? &prev_.players[i] : nullptr;

    tickCounter_ += (uint64_t)tickSkip;
    gs.lastTickCount = tickCounter_;
    reward_->SetDebug(debug);
    if (reconstruct) ReconstructState(gs, havePrev_ ? &prev_ : nullptr, tickSkip);

    if (extension_) extension_->BeforeEvaluate(gs);
    reward_->PreStep(gs);
    reward_->Evaluate(gs, focus, out);
    if (extension_) extension_->AfterEvaluate(gs);

    {
        RLGC::GameState snap = gs;
        snap.prev = nullptr;
        snap.userInfo = nullptr;
        for (auto& player : snap.players) player.prev = nullptr;
        clipBuffer_.push_back(std::move(snap));

        const uint64_t windowTicks = (uint64_t)(clipWindowSeconds_ * 120.0f);
        while (clipBuffer_.size() > 1 &&
               clipBuffer_.back().lastTickCount - clipBuffer_.front().lastTickCount > windowTicks) {
            clipBuffer_.pop_front();
        }
    }

    prev_ = gs;
    prev_.prev = nullptr;
    prev_.userInfo = nullptr;
    for (auto& player : prev_.players) player.prev = nullptr;
    havePrev_ = true;
    return true;
}

bool RewardEval::SaveClip(const std::string& name, std::string& outPath, std::string& err, const std::string& subfolder) {
    const auto& sourceBuffer = captureBuffer_.empty() ? clipBuffer_ : captureBuffer_;
    if (sourceBuffer.empty()) {
        err = "clip buffer is empty";
        return false;
    }

    const std::string baseFolder = clipFolder_.empty() ? std::string("reward_clips") : clipFolder_;
    const std::string folder = subfolder.empty() ? baseFolder : (baseFolder + "/" + subfolder);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    std::string base = name;
    if (base.empty()) {
        const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "clip_%Y%m%d_%H%M%S", &tmv);
        base = buf;
    }
    outPath = folder + "/" + base + ".rlpr";

    std::vector<const RLGC::GameState*> ticks;
    ticks.reserve(sourceBuffer.size());
    for (const auto& s : sourceBuffer) ticks.push_back(&s);

    RLGC::RLPRExportOptions options;
    if (!captureBuffer_.empty()) {
        options.sourceTickRateHz = 120;
        options.frameIntervalTicks = 1;
    } else {
        options.sourceTickRateHz = 120;
        options.frameIntervalTicks = std::max(1, static_cast<int>(std::lround(sourceBuffer.back().deltaTime * 120.0f)));
    }
    if (!RLGC::ExportClipToRLPR(outPath, ticks,
                                RLGC::rlpr::Vec3{0, 0, 0},
                                RLGC::rlpr::Vec3{0, 0, 0},
                                options)) {
        err = "RLPR export failed (1v1 only, and the clip must be non-empty)";
        return false;
    }
    std::ofstream provenance(outPath + ".json");
    if (!provenance) { err = "RLPR saved, but cannot write contact provenance sidecar"; return false; }
    provenance << json{{"contact_source", mirrorEnabled_ ? "preceding_interval_reconstruction" : "plugin_proximity_heuristic"},
        {"contact_is_native_rl_truth", false}, {"frame_interval_ticks", options.frameIntervalTicks},
        {"kinematics_source", "observed"}}.dump(2);
    if (!provenance) { err = "RLPR saved, but writing contact provenance sidecar failed"; return false; }
    return true;
}

std::vector<std::string> RewardEval::ListClips() const {
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> found;
    const std::string folder = clipFolder_.empty() ? std::string("reward_clips") : clipFolder_;
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) return {};
    for (const auto& e : std::filesystem::recursive_directory_iterator(folder, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() != ".rlpr") continue;
        std::string rel = std::filesystem::relative(e.path(), folder, ec).string();
        std::replace(rel.begin(), rel.end(), '\\', '/');
        found.emplace_back(e.last_write_time(ec), std::move(rel));
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<std::string> names;
    names.reserve(found.size());
    for (auto& f : found) names.push_back(std::move(f.second));
    return names;
}

bool RewardEval::LoadTasks(const std::string& path) {
    std::string p = !path.empty() ? path : tasksPath_;
    if (p.empty()) {
        tasks_.clear();
        return false;
    }

    std::vector<std::string> candidates = {
        p,
        "data/" + p,
        "../" + p,
        "../../" + p
    };

    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        candidates.push_back(std::string(appdata) + "\\bakkesmod\\bakkesmod\\data\\" + p);
    }

    for (const auto& cand : candidates) {
        std::ifstream ifs(cand);
        if (!ifs.is_open()) continue;
        try {
            nlohmann::json j;
            ifs >> j;
            nlohmann::json taskArray = nlohmann::json::array();
            if (j.is_object()) {
                tasksWereObjectFormat_ = true;
                tasklistId_ = j.value("id", j.value("tasklistId", ""));
                std::string customClipFolder = j.value("clipFolder", j.value("clips", ""));
                if (!customClipFolder.empty()) {
                    SetClipFolder(customClipFolder);
                    std::error_code ec;
                    std::filesystem::create_directories(customClipFolder, ec);
                    printf("[server] clipFolder configured from tasklist: %s\n", customClipFolder.c_str());
                }
                if (j.contains("tasks") && j["tasks"].is_array()) {
                    taskArray = j["tasks"];
                }
            } else if (j.is_array()) {
                tasksWereObjectFormat_ = false;
                taskArray = j;
            }

            if (tasklistId_.empty()) {
                std::error_code ec;
                tasklistId_ = std::filesystem::path(cand).stem().string();
            }

            if (!taskArray.empty()) {
                std::vector<ShotTask> loaded;
                for (const auto& item : taskArray) {
                    ShotTask t;
                    t.id = item.value("id", "");
                    t.name = item.value("name", "");
                    t.instructions = item.value("instructions", "");
                    t.expectedMode = item.value("expectedMode", "");
                    t.tag = item.value("tag", "");
                    t.targetAttempts = item.value("targetAttempts", 3);
                    t.completedAttempts = item.value("completedAttempts", 0);
                    t.successfulAttempts = item.value("successfulAttempts", 0);
                    loaded.push_back(t);
                }
                tasks_ = std::move(loaded);
                lastAttempt_ = {};
                taskMessage_ = "Loaded " + std::to_string(tasks_.size()) + " tasks from " + cand;
                tasksPath_ = cand;
                activeTaskIndex_ = tasks_.empty() ? 0 : std::clamp(activeTaskIndex_, 0, (int)tasks_.size() - 1);
                printf("[server] loaded %d shot tasks from %s (tasklist: %s)\n",
                       (int)tasks_.size(), cand.c_str(), tasklistId_.c_str());
                return true;
            }
        } catch (const std::exception& e) {
            printf("[server] error loading tasks from %s: %s\n", cand.c_str(), e.what());
        }
    }

    printf("[server] tasks file '%s' not found or invalid (no tasks loaded)\n", p.c_str());
    tasks_.clear();
    return false;
}

bool RewardEval::SaveTasks(const std::string& path) {
    if (tasks_.empty()) return false;
    std::string p = !path.empty() ? path : tasksPath_;
    if (p.empty()) return false;
    const std::string temp = p + ".tmp";
    {
        std::ofstream ofs(temp, std::ios::trunc);
        if (!ofs.is_open()) return false;
        if (tasksWereObjectFormat_) {
            nlohmann::json root = {
                {"id", tasklistId_},
                {"clipFolder", clipFolder_},
                {"tasks", GetTasksJson()}
            };
            ofs << root.dump(4);
        } else {
            ofs << GetTasksJson().dump(4);
        }
        if (!ofs.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(temp, p, ec);
    if (ec) {
        std::filesystem::remove(p, ec);
        std::filesystem::rename(temp, p, ec);
    }
    return !ec;
}

nlohmann::json RewardEval::GetTasksJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tasks_) {
        arr.push_back({
            {"id", t.id},
            {"name", t.name},
            {"instructions", t.instructions},
            {"expectedMode", t.expectedMode},
            {"tag", t.tag},
            {"targetAttempts", t.targetAttempts},
            {"completedAttempts", t.completedAttempts},
            {"successfulAttempts", t.successfulAttempts}
        });
    }
    return arr;
}

bool RewardEval::SetActiveTask(int index) {
    if (tasks_.empty()) return false;
    if (index < 0 || index >= (int)tasks_.size()) return false;
    activeTaskIndex_ = index;
    const auto& t = tasks_[index];
    taskMessage_ = "Task " + std::to_string(index + 1) + "/" + std::to_string(tasks_.size()) + ": " + t.name + " (" +
                   std::to_string(t.completedAttempts) + "/" + std::to_string(t.targetAttempts) + " attempts)";
    printf("[server] active shot task set to [%d] %s\n", activeTaskIndex_, t.name.c_str());
    return true;
}

namespace {
constexpr auto kRemarkWindow = std::chrono::seconds(90);
const char* OutcomeName(int outcome) { return outcome == 1 ? "scored" : outcome == 2 ? "miss" : "unmarked"; }
}

bool RewardEval::LogTaskAttempt(bool scored) {
    if (tasks_.empty()) return false;
    if (activeTaskIndex_ < 0 || activeTaskIndex_ >= (int)tasks_.size()) return false;
    auto& t = tasks_[activeTaskIndex_];
    const int outcome = scored ? 1 : 2;
    const auto now = std::chrono::steady_clock::now();

    const bool remark = lastAttempt_.task == activeTaskIndex_ && lastAttempt_.number > 0 &&
                        now - lastAttempt_.at <= kRemarkWindow;
    if (remark) {
        if (lastAttempt_.outcome == outcome) {
            taskMessage_ = "Attempt #" + std::to_string(lastAttempt_.number) + " is already " + OutcomeName(outcome) + " (" +
                           t.name + ")";
            return true;
        }
        if (outcome == 1) t.successfulAttempts++;
        else if (lastAttempt_.outcome == 1) t.successfulAttempts = std::max(0, t.successfulAttempts - 1);
        const bool changed = lastAttempt_.outcome != 0;
        lastAttempt_.outcome = outcome;
        taskMessage_ = std::string("Attempt #") + std::to_string(lastAttempt_.number) + (changed ? " changed to " : " marked ") +
                       OutcomeName(outcome) + " (" + t.name + ", " + std::to_string(t.successfulAttempts) + "/" +
                       std::to_string(t.completedAttempts) + " scored)";
    } else {
        t.completedAttempts++;
        if (scored) t.successfulAttempts++;
        lastAttempt_ = {activeTaskIndex_, t.completedAttempts, outcome, std::string(), now};
        taskMessage_ = "Attempt #" + std::to_string(t.completedAttempts) + " logged as " + OutcomeName(outcome) + " without a clip (" +
                       t.name + ")";
    }
    SaveTasks();
    printf("[server] %s\n", taskMessage_.c_str());
    return true;
}

bool RewardEval::UndoTaskAttempt() {
    const bool valid = lastAttempt_.number > 0 && lastAttempt_.task >= 0 && lastAttempt_.task < (int)tasks_.size();
    if (!valid) {
        taskMessage_ = "Nothing to undo";
        return false;
    }
    auto& t = tasks_[lastAttempt_.task];
    t.completedAttempts = std::max(0, t.completedAttempts - 1);
    if (lastAttempt_.outcome == 1) t.successfulAttempts = std::max(0, t.successfulAttempts - 1);
    t.successfulAttempts = std::min(t.successfulAttempts, t.completedAttempts);
    std::string kept;
    if (!lastAttempt_.clip.empty()) {
        std::filesystem::path from(lastAttempt_.clip);
        std::filesystem::path to = from;
        to.replace_filename(from.stem().string() + "_undone" + from.extension().string());
        std::error_code ec;
        std::filesystem::rename(from, to, ec);
        if (!ec) kept = " (clip kept as " + to.filename().string() + ")";
    }
    taskMessage_ = "Undid attempt #" + std::to_string(lastAttempt_.number) + " of " + t.name + kept;
    lastAttempt_ = {};
    SaveTasks();
    printf("[server] %s\n", taskMessage_.c_str());
    return true;
}

nlohmann::json RewardEval::LastAttemptJson() const {
    const bool valid = lastAttempt_.number > 0;
    return {{"task", valid ? lastAttempt_.task : -1},
            {"number", valid ? lastAttempt_.number : 0},
            {"outcome", valid ? OutcomeName(lastAttempt_.outcome) : "none"},
            {"clip", lastAttempt_.clip}};
}

bool RewardEval::ResetTask(int index) {
    if (tasks_.empty()) return false;
    lastAttempt_ = {};
    if (index >= 0 && index < (int)tasks_.size()) {
        tasks_[index].completedAttempts = 0;
        tasks_[index].successfulAttempts = 0;
        SaveTasks();
        taskMessage_ = "Reset counters of " + tasks_[index].name;
        printf("[server] reset attempt counters for [%d] %s\n", index, tasks_[index].name.c_str());
        return true;
    } else if (index == -1) {
        for (auto& t : tasks_) {
            t.completedAttempts = 0;
            t.successfulAttempts = 0;
        }
        SaveTasks();
        taskMessage_ = "Reset all shot counters";
        printf("[server] reset all shot task counters\n");
        return true;
    }
    return false;
}

bool RewardEval::SaveTaskClip(std::string& outPath, std::string& err, const std::string& overrideSubfolder) {
    if (tasks_.empty()) {
        err = "no shot tasks loaded on server (pass --tasks <path> on server boot)";
        return false;
    }
    if (activeTaskIndex_ < 0 || activeTaskIndex_ >= (int)tasks_.size()) {
        err = "invalid active task index";
        return false;
    }
    auto& cur = tasks_[activeTaskIndex_];

    std::string tlistId = tasklistId_;
    if (tlistId.empty() && !tasksPath_.empty()) {
        std::error_code ec;
        tlistId = std::filesystem::path(tasksPath_).stem().string();
    }
    if (tlistId.empty()) tlistId = "default";

    std::string shotId = !cur.id.empty() ? cur.id : cur.tag;
    if (shotId.empty()) shotId = "shot_" + std::to_string(activeTaskIndex_);

    std::string subfolder = !overrideSubfolder.empty() ? overrideSubfolder : (tlistId + "/" + shotId);
    const std::filesystem::path folder = std::filesystem::path(clipFolder_.empty() ? "reward_clips" : clipFolder_) / subfolder;
    int number = cur.completedAttempts + 1;
    while (std::filesystem::exists(folder / ("attempt_" + std::to_string(number) + ".rlpr")) ||
           std::filesystem::exists(folder / ("attempt_" + std::to_string(number) + "_undone.rlpr"))) ++number;

    if (!SaveClip("attempt_" + std::to_string(number), outPath, err, subfolder)) {
        return false;
    }
    cur.completedAttempts++;
    lastAttempt_ = {activeTaskIndex_, cur.completedAttempts, 0, outPath, std::chrono::steady_clock::now()};
    taskMessage_ = "Saved attempt #" + std::to_string(cur.completedAttempts) + " of " + cur.name + ": press scored or miss to mark it";
    SaveTasks();
    printf("[server] saved task clip [%d] %s -> %s (%d/%d attempts)\n",
           activeTaskIndex_, cur.name.c_str(), outPath.c_str(), cur.completedAttempts, cur.targetAttempts);
    return true;
}

}
