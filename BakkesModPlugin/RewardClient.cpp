#include <winsock2.h>
#include <ws2tcpip.h>
#include "RewardClient.h"
#include <nlohmann/json.hpp>
#include <chrono>
#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;

namespace {
    constexpr uintptr_t kInvalid = ~static_cast<uintptr_t>(0);

    json Vec3(const float *v) { return json::array({v[0], v[1], v[2]}); }

    json RotArr(const float *m) {
        json a = json::array();
        for (int i = 0; i < 9; ++i) a.push_back(m[i]);
        return a;
    }

    json StateRequest(const RewardStateSnapshot &snap, const char *cmd) {
        json cars = json::array();
        for (const auto &c: snap.cars) {
            cars.push_back({
                {"pos", Vec3(c.pos)}, {"vel", Vec3(c.vel)}, {"ang_vel", Vec3(c.angVel)},
                {"rot_mat", RotArr(c.rotMat)}, {"boost", c.boost},
                {"is_on_ground", c.isOnGround}, {"has_jumped", c.hasJumped},
                {"has_double_jumped", c.hasDoubleJumped}, {"has_flipped", c.hasFlipped},
                {"is_flipping", c.isFlipping}, {"is_jumping", c.isJumping},
                {"jump_time", c.jumpTime}, {"flip_time", c.flipTime},
                {"air_time_since_jump", c.airTimeSinceJump}, {"is_supersonic", c.isSupersonic},
                {"handbrake_val", c.handbrakeVal}, {"flip_rel_torque", Vec3(c.flipRelTorque)},
                {"world_contact_has", c.worldContactHas},
                {"world_contact_normal", Vec3(c.worldContactNormal)},
                {
                    "prev_action", {
                        {"throttle", c.act[0]}, {"steer", c.act[1]}, {"pitch", c.act[2]},
                        {"yaw", c.act[3]}, {"roll", c.act[4]}, {"jump", c.act[5]},
                        {"boost", c.act[6]}, {"handbrake", c.act[7]}
                    }
                },
                {"is_demoed", c.isDemoed}, {"demo_respawn_timer", c.demoRespawnTimer},
                {"is_ball_touched", c.isBallTouched}, {"team", c.team},
            });
        }

        return {
            {"cmd", cmd}, {"step", snap.step}, {"force_all", snap.forceAll}, {"debug", snap.debug},
            {"tick_skip", snap.tickSkip}, {"focus", snap.focus},
            {"cars", cars},
            {
                "ball", {
                    {"pos", Vec3(snap.ball.pos)}, {"vel", Vec3(snap.ball.vel)},
                    {"ang_vel", Vec3(snap.ball.angVel)}, {"rot_mat", RotArr(snap.ball.rotMat)}
                }
            },
            {
                "game", {
                    {"blue_score", snap.blueScore}, {"orange_score", snap.orangeScore},
                    {"boost_pad_states", std::vector<int>(34, 1)},
                    {"boost_pad_timers", std::vector<float>(34, 0.0f)}
                }
            }
        };
    }
}

RewardClient::~RewardClient() { Stop(); }

bool RewardClient::DoClipFilesystemCommand(const std::string &cmd, const std::string &a, const std::string &b) {
    json request{{"cmd", cmd}};
    if (cmd == "create_clip_folder") request["path"] = a;
    else if (cmd == "rename_clip_path") {
        request["from"] = a;
        request["to"] = b;
    } else if (cmd == "move_clip") {
        request["source"] = a;
        request["folder"] = b;
    } else return false;

    std::string resp;
    if (!Rpc(request.dump(), resp)) return false;
    try {
        json j = json::parse(resp);
        if (!j.value("ok", false)) SetError(cmd + ": " + j.value("error", std::string("failed")));
        return true;
    } catch (...) {
        return false;
    }
}

void RewardClient::Start(const std::string &host, int port, const std::string &config) {
    Stop();
    host_ = host;
    port_ = port; {
        std::lock_guard<std::mutex> l(cfgMut_);
        wantConfig_ = config;
        needLoad_ = true;
    }
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    running_ = true;
    worker_ = std::thread(&RewardClient::WorkerLoop, this);
}

void RewardClient::Stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
    Disconnect();
    {
        std::lock_guard<std::mutex> l(stateMut_);
        pendingCaptures_.clear();
        haveState_ = false;
    }
    WSACleanup();
}

void RewardClient::SubmitState(const RewardStateSnapshot &snap) {
    std::lock_guard<std::mutex> l(stateMut_);
    pendingState_ = snap;
    haveState_ = true;
}

void RewardClient::SubmitCapture(const RewardStateSnapshot &snap) {
    std::lock_guard<std::mutex> l(stateMut_);
    pendingCaptures_.push_back(snap);
}

void RewardClient::RequestLoad(const std::string &config) {
    std::lock_guard<std::mutex> l(cfgMut_);
    wantConfig_ = config;
    needLoad_ = true;
}

void RewardClient::RequestReset() {
    std::lock_guard<std::mutex> l(cfgMut_);
    needReset_ = true;
}

void RewardClient::RequestSaveClip(const std::string &name) { {
        std::lock_guard<std::mutex> l(clipMut_);
        clip_.pending = true;
    }
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingClipName_ = name;
    needSaveClip_ = true;
}

void RewardClient::RequestListClips() {
    std::lock_guard<std::mutex> l(cfgMut_);
    needListClips_ = true;
}

void RewardClient::RequestCreateClipFolder(const std::string &path) {
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingClipFilesystemCommand_ = "create_clip_folder";
    pendingClipFilesystemA_ = path;
    pendingClipFilesystemB_.clear();
    needClipFilesystemCommand_ = true;
}

void RewardClient::RequestRenameClipPath(const std::string &from, const std::string &to) {
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingClipFilesystemCommand_ = "rename_clip_path";
    pendingClipFilesystemA_ = from;
    pendingClipFilesystemB_ = to;
    needClipFilesystemCommand_ = true;
}

void RewardClient::RequestMoveClip(const std::string &source, const std::string &folder) {
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingClipFilesystemCommand_ = "move_clip";
    pendingClipFilesystemA_ = source;
    pendingClipFilesystemB_ = folder;
    needClipFilesystemCommand_ = true;
}

void RewardClient::RequestSetClipWindow(float seconds) {
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingClipWindowSeconds_ = seconds;
    needSetClipWindow_ = true;
}

RewardClient::ClipState RewardClient::LatestClipState() {
    std::lock_guard<std::mutex> l(clipMut_);
    return clip_;
}

void RewardClient::RequestGetTasks() {
    std::lock_guard<std::mutex> l(cfgMut_);
    needGetTasks_ = true;
}

void RewardClient::RequestSetActiveTask(int index) {
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingSetActiveTaskIndex_ = index;
    needSetActiveTask_ = true;
}

void RewardClient::RequestLogTaskAttempt(bool scored) {
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingLogAttemptScored_ = scored;
    needLogTaskAttempt_ = true;
}

void RewardClient::RequestResetTask(int index) {
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingResetTaskIndex_ = index;
    needResetTask_ = true;
}

void RewardClient::RequestReloadTasks(const std::string &path) {
    std::lock_guard<std::mutex> l(cfgMut_);
    pendingReloadTasksPath_ = path;
    needReloadTasks_ = true;
}

void RewardClient::RequestSaveTaskClip() { {
        std::lock_guard<std::mutex> l(clipMut_);
        clip_.pending = true;
    }
    std::lock_guard<std::mutex> l(cfgMut_);
    needSaveTaskClip_ = true;
}

std::vector<ShotTask> RewardClient::LatestTasks() {
    std::lock_guard<std::mutex> l(taskMut_);
    return tasks_;
}

int RewardClient::ActiveTaskIndex() {
    std::lock_guard<std::mutex> l(taskMut_);
    return activeTaskIndex_;
}

std::string RewardClient::TasksPath() {
    std::lock_guard<std::mutex> l(taskMut_);
    return tasksPath_;
}

bool RewardClient::ApplyTaskReply(const std::string &resp, bool notify) {
    try {
        const json j = json::parse(resp);
        const std::string message = j.value("message", std::string());
        std::lock_guard<std::mutex> l(taskMut_);
        if (j.value("ok", false)) {
            activeTaskIndex_ = j.value("active_index", activeTaskIndex_);
            tasksPath_ = j.value("tasks_path", tasksPath_);
            if (j.contains("tasks")) {
                std::vector<ShotTask> tasks;
                for (const auto &item: j["tasks"]) {
                    ShotTask t;
                    t.id = item.value("id", "");
                    t.name = item.value("name", "");
                    t.instructions = item.value("instructions", "");
                    t.expectedMode = item.value("expectedMode", "");
                    t.tag = item.value("tag", "");
                    t.targetAttempts = item.value("targetAttempts", 3);
                    t.completedAttempts = item.value("completedAttempts", 0);
                    t.successfulAttempts = item.value("successfulAttempts", 0);
                    tasks.push_back(t);
                }
                tasks_ = std::move(tasks);
            }
            if (j.contains("last_attempt")) {
                lastAttempt_.number = j["last_attempt"].value("number", 0);
                lastAttempt_.outcome = j["last_attempt"].value("outcome", std::string("none"));
            }
        }
        if (notify && !message.empty()) {
            taskNotice_ = message;
            ++taskNoticeSeq_;
        }
    } catch (...) { return false; }
    return true;
}

TaskNotice RewardClient::LatestTaskNotice() {
    std::lock_guard<std::mutex> l(taskMut_);
    return {taskNotice_, taskNoticeSeq_};
}

LastAttemptInfo RewardClient::LastAttempt() {
    std::lock_guard<std::mutex> l(taskMut_);
    return lastAttempt_;
}

void RewardClient::RequestUndoTaskAttempt() {
    std::lock_guard<std::mutex> l(cfgMut_);
    needUndoTaskAttempt_ = true;
}

bool RewardClient::DoGetTasks() {
    std::string resp;
    return Rpc(json{{"cmd", "get_tasks"}}.dump(), resp) && ApplyTaskReply(resp, false);
}

bool RewardClient::DoSetActiveTask(int index) {
    std::string resp;
    return Rpc(json{{"cmd", "set_active_task"}, {"index", index}}.dump(), resp) && ApplyTaskReply(resp, true);
}

bool RewardClient::DoLogTaskAttempt(bool scored) {
    std::string resp;
    return Rpc(json{{"cmd", "log_task_attempt"}, {"scored", scored}}.dump(), resp) && ApplyTaskReply(resp, true);
}

bool RewardClient::DoUndoTaskAttempt() {
    std::string resp;
    return Rpc(json{{"cmd", "undo_task_attempt"}}.dump(), resp) && ApplyTaskReply(resp, true);
}

bool RewardClient::DoResetTask(int index) {
    std::string resp;
    return Rpc(json{{"cmd", "reset_task"}, {"index", index}}.dump(), resp) && ApplyTaskReply(resp, true);
}

bool RewardClient::DoReloadTasks(const std::string &path) {
    std::string resp;
    return Rpc(json{{"cmd", "reload_tasks"}, {"path", path}}.dump(), resp) && ApplyTaskReply(resp, true);
}

bool RewardClient::DoSaveTaskClip() {
    std::string resp;
    if (!Rpc(json{{"cmd", "save_task_clip"}}.dump(), resp)) return false;
    try {
        const json j = json::parse(resp);
        {
            std::lock_guard<std::mutex> l(clipMut_);
            clip_.pending = false;
            if (j.value("ok", false)) clip_.lastSavedPath = j.value("path", std::string());
            else SetError("save_task_clip: " + j.value("error", std::string("failed")));
        }
        return ApplyTaskReply(resp, true);
    } catch (...) {
        std::lock_guard<std::mutex> l(clipMut_);
        clip_.pending = false;
        return false;
    }
}

bool RewardClient::DoSaveClip(const std::string &name) {
    std::string resp;
    if (!Rpc(json{{"cmd", "save_clip"}, {"name", name}}.dump(), resp)) return false;
    try {
        json j = json::parse(resp);
        std::lock_guard<std::mutex> l(clipMut_);
        clip_.pending = false;
        if (j.value("ok", false)) {
            clip_.lastSavedPath = j.value("path", std::string());
            clip_.bufferedFrames = j.value("frames", 0);
        } else {
            SetError("save_clip: " + j.value("error", std::string("failed")));
        }
    } catch (...) {
        std::lock_guard<std::mutex> l(clipMut_);
        clip_.pending = false;
        return false;
    }
    return true;
}

bool RewardClient::DoListClips() {
    std::string resp;
    if (!Rpc(json{{"cmd", "list_clips"}}.dump(), resp)) return false;
    try {
        json j = json::parse(resp);
        if (!j.value("ok", false)) return true;
        std::lock_guard<std::mutex> l(clipMut_);
        clip_.folder = j.value("folder", std::string());
        clip_.clips = j.value("clips", std::vector<std::string>{});
    } catch (...) { return false; }
    return true;
}

bool RewardClient::DoSetClipWindow(float seconds) {
    std::string resp;
    if (!Rpc(json{{"cmd", "set_clip_window"}, {"seconds", seconds}}.dump(), resp)) return false;
    return json::parse(resp).value("ok", false);
}

RewardResult RewardClient::LatestResult() {
    std::lock_guard<std::mutex> l(resultMut_);
    return latest_;
}

std::string RewardClient::LastError() {
    std::lock_guard<std::mutex> l(errMut_);
    return lastError_;
}

void RewardClient::SetError(const std::string &e) {
    std::lock_guard<std::mutex> l(errMut_);
    lastError_ = e;
}

void RewardClient::Disconnect() {
    if (sock_ != kInvalid) {
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = kInvalid;
    }
    recvBuf_.clear();
    connected_ = false;
}

bool RewardClient::EnsureConnected() {
    if (sock_ != kInvalid) return true;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        SetError("socket() failed");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port_));
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        closesocket(s);
        SetError("bad host: " + host_);
        return false;
    }
    if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        SetError("connect failed (is reward_server running?)");
        return false;
    }

    sock_ = static_cast<uintptr_t>(s);
    recvBuf_.clear();

    std::string resp;
    if (!Rpc(json{{"cmd", "hello"}}.dump(), resp)) {
        Disconnect();
        return false;
    }
    try {
        if (!json::parse(resp).value("ok", false)) {
            Disconnect();
            SetError("hello rejected");
            return false;
        }
    } catch (...) {
        Disconnect();
        SetError("bad hello reply");
        return false;
    }

    connected_ = true; {
        std::lock_guard<std::mutex> l(cfgMut_);
        needLoad_ = true;
        needGetTasks_ = true;
        needListClips_ = true;
    }
    SetError("");
    return true;
}

bool RewardClient::Rpc(const std::string &reqLine, std::string &respLine) {
    if (sock_ == kInvalid) return false;
    SOCKET s = static_cast<SOCKET>(sock_);

    std::string line = reqLine;
    line.push_back('\n');
    size_t sent = 0;
    while (sent < line.size()) {
        int n = send(s, line.data() + static_cast<int>(sent), static_cast<int>(line.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }

    for (;;) {
        size_t nl = recvBuf_.find('\n');
        if (nl != std::string::npos) {
            respLine = recvBuf_.substr(0, nl);
            recvBuf_.erase(0, nl + 1);
            return true;
        }
        char tmp[16384];
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        recvBuf_.append(tmp, n);
    }
}

bool RewardClient::DoLoad(const std::string &config) {
    std::string resp;
    if (!Rpc(json{{"cmd", "load"}, {"config", config}}.dump(), resp)) {
        Disconnect();
        return false;
    }
    try {
        json j = json::parse(resp);
        if (!j.value("ok", false)) {
            SetError("load failed: " + j.value("error", std::string("?")));
            return false;
        }
        std::vector<std::string> names = j.value("names", std::vector<std::string>{});
        count_ = static_cast<int>(names.size());
        std::lock_guard<std::mutex> l(resultMut_);
        latest_.names = names;
        latest_.values.assign(names.size(), 0.0f);
        latest_.weights.assign(names.size(), 0.0f);
        latest_.total = 0.0f;
        latest_.valid = false;
    } catch (...) {
        SetError("bad load reply");
        return false;
    }
    return true;
}

bool RewardClient::DoEval(const RewardStateSnapshot &snap) {
    json req = StateRequest(snap, "eval");

    std::string resp;
    if (!Rpc(req.dump(), resp)) {
        Disconnect();
        return false;
    }
    try {
        json j = json::parse(resp);
        if (!j.value("ok", false)) {
            SetError("eval: " + j.value("error", std::string("?")));
            return false;
        }
        RewardResult r;
        r.valid = true;
        r.total = j.value("total", 0.0f);
        r.names = j.value("names", std::vector<std::string>{});
        r.values = j.value("values", std::vector<float>{});
        r.weights = j.value("weights", std::vector<float>{});
        r.weighted = j.value("weighted", std::vector<float>{});
        count_ = static_cast<int>(r.names.size());
        std::lock_guard<std::mutex> l(resultMut_);
        latest_ = std::move(r);
    } catch (...) {
        SetError("bad eval reply");
        return false;
    }
    return true;
}

bool RewardClient::DoCapture(const RewardStateSnapshot &snap) {
    std::string resp;
    if (!Rpc(StateRequest(snap, "capture").dump(), resp)) return false;
    try {
        if (!json::parse(resp).value("ok", false)) {
            SetError("capture failed");
            return false;
        }
    } catch (...) {
        SetError("bad capture reply");
        return false;
    }
    return true;
}

void RewardClient::WorkerLoop() {
    size_t captureBurst = 0;
    while (running_) {
        if (!EnsureConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        RewardStateSnapshot capture;
        bool haveCapture = false;
        {
            std::lock_guard<std::mutex> l(stateMut_);
            const bool prioritizeEval = haveState_ && captureBurst >= 8;
            if (!prioritizeEval && !pendingCaptures_.empty()) {
                capture = std::move(pendingCaptures_.front());
                pendingCaptures_.pop_front();
                haveCapture = true;
            }
        }
        if (haveCapture) {
            if (!DoCapture(capture)) {
                std::lock_guard<std::mutex> l(stateMut_);
                pendingCaptures_.push_front(std::move(capture));
                Disconnect();
            } else {
                ++captureBurst;
            }
            continue;
        }

        std::string cfg;
        bool doLoad = false;
        bool doReset = false;
        bool doSaveClip = false;
        bool doListClips = false;
        bool doClipFilesystemCommand = false;
        std::string clipFilesystemCommand;
        std::string clipFilesystemA;
        std::string clipFilesystemB;
        bool doSetClipWindow = false;
        float clipWindowSeconds = 15.0f;
        bool doGetTasks = false;
        bool doSetActiveTask = false;
        int setActiveIndex = 0;
        bool doLogTaskAttempt = false;
        bool logAttemptScored = false;
        bool doUndoTaskAttempt = false;
        bool doResetTask = false;
        int resetTaskIndex = -1;
        bool doReloadTasks = false;
        std::string reloadTasksPath;
        bool doSaveTaskClip = false;
        std::string clipName; {
            std::lock_guard<std::mutex> l(cfgMut_);
            if (needLoad_) {
                cfg = wantConfig_;
                doLoad = true;
                needLoad_ = false;
            } else if (needReset_) {
                doReset = true;
                needReset_ = false;
            } else if (needSaveClip_) {
                doSaveClip = true;
                clipName = pendingClipName_;
                needSaveClip_ = false;
            } else if (needSaveTaskClip_) {
                doSaveTaskClip = true;
                needSaveTaskClip_ = false;
            } else if (needGetTasks_) {
                doGetTasks = true;
                needGetTasks_ = false;
            } else if (needSetActiveTask_) {
                doSetActiveTask = true;
                setActiveIndex = pendingSetActiveTaskIndex_;
                needSetActiveTask_ = false;
            } else if (needLogTaskAttempt_) {
                doLogTaskAttempt = true;
                logAttemptScored = pendingLogAttemptScored_;
                needLogTaskAttempt_ = false;
            } else if (needUndoTaskAttempt_) {
                doUndoTaskAttempt = true;
                needUndoTaskAttempt_ = false;
            } else if (needResetTask_) {
                doResetTask = true;
                resetTaskIndex = pendingResetTaskIndex_;
                needResetTask_ = false;
            } else if (needReloadTasks_) {
                doReloadTasks = true;
                reloadTasksPath = pendingReloadTasksPath_;
                needReloadTasks_ = false;
            } else if (needListClips_) {
                doListClips = true;
                needListClips_ = false;
            } else if (needClipFilesystemCommand_) {
                doClipFilesystemCommand = true;
                clipFilesystemCommand = pendingClipFilesystemCommand_;
                clipFilesystemA = pendingClipFilesystemA_;
                clipFilesystemB = pendingClipFilesystemB_;
                needClipFilesystemCommand_ = false;
            } else if (needSetClipWindow_) {
                doSetClipWindow = true;
                clipWindowSeconds = pendingClipWindowSeconds_;
                needSetClipWindow_ = false;
            }
        }
        if (doLoad) {
            if (!DoLoad(cfg)) { std::this_thread::sleep_for(std::chrono::milliseconds(300)); }
            continue;
        }
        if (doReset) {
            std::string resp;
            if (!Rpc(json{{"cmd", "reset"}}.dump(), resp)) { Disconnect(); }
            continue;
        }
        if (doSaveClip) {
            if (!DoSaveClip(clipName)) Disconnect();
            else {
                std::lock_guard<std::mutex> l(cfgMut_);
                needListClips_ = true;
            }
            continue;
        }
        if (doSaveTaskClip) {
            if (!DoSaveTaskClip()) Disconnect();
            else {
                std::lock_guard<std::mutex> l(cfgMut_);
                needListClips_ = true;
            }
            continue;
        }
        if (doGetTasks) {
            if (!DoGetTasks()) Disconnect();
            continue;
        }
        if (doSetActiveTask) {
            if (!DoSetActiveTask(setActiveIndex)) Disconnect();
            continue;
        }
        if (doLogTaskAttempt) {
            if (!DoLogTaskAttempt(logAttemptScored)) Disconnect();
            continue;
        }
        if (doUndoTaskAttempt) {
            if (!DoUndoTaskAttempt()) Disconnect();
            continue;
        }
        if (doResetTask) {
            if (!DoResetTask(resetTaskIndex)) Disconnect();
            continue;
        }
        if (doReloadTasks) {
            if (!DoReloadTasks(reloadTasksPath)) Disconnect();
            continue;
        }
        if (doListClips) {
            if (!DoListClips()) Disconnect();
            continue;
        }
        if (doClipFilesystemCommand) {
            if (!DoClipFilesystemCommand(clipFilesystemCommand, clipFilesystemA, clipFilesystemB)) Disconnect();
            else {
                std::lock_guard<std::mutex> l(cfgMut_);
                needListClips_ = true;
            }
            continue;
        }
        if (doSetClipWindow) {
            if (!DoSetClipWindow(clipWindowSeconds)) Disconnect();
            continue;
        }

        RewardStateSnapshot snap;
        bool have = false; {
            std::lock_guard<std::mutex> l(stateMut_);
            if (haveState_) {
                snap = pendingState_;
                haveState_ = false;
                have = true;
            }
        }

        if (have && !snap.cars.empty()) {
            DoEval(snap);
            captureBurst = 0;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}
