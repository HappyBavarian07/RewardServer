#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>

struct RewardCarSnapshot {
    float pos[3] = {0, 0, 0};
    float vel[3] = {0, 0, 0};
    float angVel[3] = {0, 0, 0};
    float rotMat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float boost = 0.0f;
    int isOnGround = 1;
    int hasJumped = 0;
    int hasDoubleJumped = 0;
    int hasFlipped = 0;
    int isFlipping = 0;
    int isJumping = 0;
    float jumpTime = 0.0f;
    float flipTime = 0.0f;
    float airTimeSinceJump = 0.0f;
    int isSupersonic = 0;
    float handbrakeVal = 0.0f;
    float flipRelTorque[3] = {0, 0, 0};
    int worldContactHas = 0;
    float worldContactNormal[3] = {0, 0, 1};
    float act[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int isDemoed = 0;
    float demoRespawnTimer = 0.0f;
    int isBallTouched = 0;
    int team = 0;
};

struct RewardBallSnapshot {
    float pos[3] = {0, 0, 93.0f};
    float vel[3] = {0, 0, 0};
    float angVel[3] = {0, 0, 0};
    float rotMat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

struct RewardStateSnapshot {
    std::vector<RewardCarSnapshot> cars;
    RewardBallSnapshot ball;
    uint64_t step = 0;
    bool forceAll = false;
    bool debug = false;
    int tickSkip = 8;
    int captureTicks = 1;
    int focus = 0;
    int blueScore = 0;
    int orangeScore = 0;
};

struct RewardResult {
    bool valid = false;
    float total = 0.0f;
    std::vector<std::string> names;
    std::vector<float> values;
    std::vector<float> weights;
    std::vector<float> weighted;
};

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

struct ClipRequest {
    bool onlyFocus = false;
    std::string source = "live";
};

struct TaskNotice {
    std::string text;
    uint64_t seq = 0;
};

struct LastAttemptInfo {
    int number = 0;
    std::string outcome = "none";
};

class RewardClient {
public:
    RewardClient() = default;

    ~RewardClient();

    RewardClient(const RewardClient &) = delete;

    RewardClient &operator=(const RewardClient &) = delete;

    void Start(const std::string &host, int port, const std::string &config);

    void Stop();

    void SubmitState(const RewardStateSnapshot &snap);

    void SubmitCapture(const RewardStateSnapshot &snap);

    void RequestLoad(const std::string &config);

    void RequestReset(bool clearBuffer = false);

    void RequestSaveClip(const std::string &name = std::string(), ClipRequest request = {});

    void RequestListClips();

    void RequestCreateClipFolder(const std::string &path);

    void RequestRenameClipPath(const std::string &from, const std::string &to);

    void RequestMoveClip(const std::string &source, const std::string &folder);

    void RequestSetClipWindow(float seconds);

    struct ClipState {
        std::string lastSavedPath;
        std::string folder;
        std::vector<std::string> clips;
        int bufferedFrames = 0;
        bool pending = false;
    };

    ClipState LatestClipState();

    void RequestGetTasks();

    void RequestSetActiveTask(int index);

    void RequestLogTaskAttempt(bool scored);

    void RequestUndoTaskAttempt();

    TaskNotice LatestTaskNotice();

    LastAttemptInfo LastAttempt();

    void RequestResetTask(int index = -1);

    void RequestReloadTasks(const std::string &path = "");

    void RequestSaveTaskClip(ClipRequest request = {});

    std::vector<ShotTask> LatestTasks();

    int ActiveTaskIndex();

    std::string TasksPath();

    RewardResult LatestResult();

    bool Connected() const { return connected_.load(); }
    int RewardCount() const { return count_.load(); }

    std::string LastError();

private:
    void WorkerLoop();

    bool EnsureConnected();

    bool DoLoad(const std::string &config);

    bool DoEval(const RewardStateSnapshot &snap);

    bool DoCapture(const RewardStateSnapshot &snap);

    bool DoSaveClip(const std::string &name, const ClipRequest &request);

    bool DoListClips();

    bool DoClipFilesystemCommand(const std::string &cmd, const std::string &a, const std::string &b);

    bool DoSetClipWindow(float seconds);

    bool DoGetTasks();

    bool DoSetActiveTask(int index);

    bool DoLogTaskAttempt(bool scored);

    bool DoUndoTaskAttempt();

    bool ApplyTaskReply(const std::string &resp, bool notify);

    bool DoResetTask(int index);

    bool DoReloadTasks(const std::string &path);

    bool DoSaveTaskClip(const ClipRequest &request);

    bool Rpc(const std::string &reqLine, std::string &respLine);

    void Disconnect();

    void SetError(const std::string &e);

    std::mutex clipMut_;
    ClipState clip_;
    bool needSaveClip_ = false;
    bool needListClips_ = false;
    bool needClipFilesystemCommand_ = false;
    std::string pendingClipFilesystemCommand_;
    std::string pendingClipFilesystemA_;
    std::string pendingClipFilesystemB_;
    bool needSetClipWindow_ = false;
    float pendingClipWindowSeconds_ = 15.0f;
    std::string pendingClipName_;
    ClipRequest pendingClipRequest_;
    ClipRequest pendingTaskClipRequest_;
    bool pendingResetClear_ = false;

    std::mutex taskMut_;
    std::vector<ShotTask> tasks_;
    std::string tasksPath_;
    int activeTaskIndex_ = 0;
    bool needGetTasks_ = true;
    bool needSetActiveTask_ = false;
    int pendingSetActiveTaskIndex_ = 0;
    bool needLogTaskAttempt_ = false;
    bool pendingLogAttemptScored_ = false;
    bool needUndoTaskAttempt_ = false;
    std::string taskNotice_;
    uint64_t taskNoticeSeq_ = 0;
    LastAttemptInfo lastAttempt_;
    bool needResetTask_ = false;
    int pendingResetTaskIndex_ = -1;
    bool needReloadTasks_ = false;
    std::string pendingReloadTasksPath_;
    bool needSaveTaskClip_ = false;

    std::string host_ = "127.0.0.1";
    int port_ = 42069;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<int> count_{0};

    uintptr_t sock_ = ~static_cast<uintptr_t>(0);
    std::string recvBuf_;

    std::mutex stateMut_;
    RewardStateSnapshot pendingState_;
    bool haveState_ = false;
    std::deque<RewardStateSnapshot> pendingCaptures_;

    std::mutex resultMut_;
    RewardResult latest_;

    std::mutex cfgMut_;
    std::string wantConfig_;
    bool needLoad_ = true;
    bool needReset_ = false;

    std::mutex errMut_;
    std::string lastError_;
};
