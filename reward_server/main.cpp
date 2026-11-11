#include "replay_cli.h"
#include "reward_eval.h"
#include "session.h"
#include "RocketSim.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr const char* kUsage =
    "reward_server - live reward evaluation for the BakkesMod Reward Tester plugin\n"
    "\n"
    "Serving:\n"
    "  --port <N>            TCP port on 127.0.0.1 (default 42069)\n"
    "  --config <path>       reward config to preload\n"
    "  --tasks <file>        shot task list (counters are written back to it)\n"
    "  --meshes <dir>        RocketSim collision meshes (default ./collision_meshes)\n"
    "  --clips <dir>         clip folder (default %APPDATA%\\bakkesmod\\bakkesmod\\data\\reward_clips)\n"
    "  --clip-seconds <S>    rolling clip window in seconds (default 15)\n"
    "\n"
    "Offline replay (needs --config):\n"
    "  --replay <clip|dir>   evaluate .rlpr clips, repeatable\n"
    "  --focus <N>           car index to report (default 0)\n"
    "  --terms <a,b>         only print terms whose name contains one of these\n"
    "  --replay-tick-skip N  evaluation interval in physics ticks (default 8)\n"
    "  --replay-frame-offset N  phase of the evaluation grid inside one interval\n"
    "  --trust-touch         use recorded contact flags instead of reconstructing them\n"
    "  --json                machine-readable output\n"
    "  --extra               add the integration's extension report to --json output (alias --labels)\n"
    "\n"
    "Checks:\n"
    "  --self-test-timing    compare reconstructed and native ball contacts\n"
    "  --help                this text\n";

struct Options {
    int port = 42069;
    std::string config;
    std::string meshes;
    std::string clips;
    std::string tasks;
    float clipSeconds = 15.0f;
    std::vector<std::string> replayPaths;
    rs::ReplayOptions replay;
    bool checkTiming = false;
    bool help = false;
};

std::vector<std::string> SplitCommas(const std::string& list) {
    std::vector<std::string> parts;
    for (size_t begin = 0, end; begin < list.size(); begin = end + 1) {
        end = list.find(',', begin);
        if (end == std::string::npos) end = list.size();
        if (end > begin) parts.push_back(list.substr(begin, end - begin));
    }
    return parts;
}

bool ParseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "--help" || a == "-h") o.help = true;
        else if (a == "--trust-touch") o.replay.trustTouch = true;
        else if (a == "--self-test-timing") o.checkTiming = true;
        else if (a == "--json") o.replay.json = true;
        else if (a == "--extra" || a == "--labels") o.replay.extra = true;
        else if (!hasValue) { printf("missing value for %s\n", a.c_str()); return false; }
        else if (a == "--port") o.port = atoi(argv[++i]);
        else if (a == "--config") o.config = argv[++i];
        else if (a == "--tasks") o.tasks = argv[++i];
        else if (a == "--meshes") o.meshes = argv[++i];
        else if (a == "--clips") o.clips = argv[++i];
        else if (a == "--clip-seconds") o.clipSeconds = (float)atof(argv[++i]);
        else if (a == "--replay") o.replayPaths.push_back(argv[++i]);
        else if (a == "--focus") o.replay.focus = atoi(argv[++i]);
        else if (a == "--replay-tick-skip") o.replay.tickSkip = atoi(argv[++i]);
        else if (a == "--replay-frame-offset") o.replay.frameOffset = atoi(argv[++i]);
        else if (a == "--terms") o.replay.termFilter = SplitCommas(argv[++i]);
        else { printf("unknown option %s\n", a.c_str()); return false; }
    }
    return true;
}

void ConfigureMirror(rs::RewardEval& eval, const std::string& requested) {
    const std::string mesh = !requested.empty() ? requested : "collision_meshes";
    if (!std::filesystem::exists(mesh)) {
        printf("[server] collision_meshes not found; arena mirror disabled "
               "(contact/flip/ball-hit fields will be defaults). Pass --meshes <dir> to enable.\n");
        return;
    }
    RocketSim::Init(mesh, true);
    eval.SetMirrorEnabled(true);
    printf("[server] RocketSim arena mirror enabled (meshes: %s)\n", mesh.c_str());
}

void ConfigureClips(rs::RewardEval& eval, std::string folder, float seconds) {
    if (folder.empty()) {
        const char* appdata = std::getenv("APPDATA");
        folder = appdata ? std::string(appdata) + "\\bakkesmod\\bakkesmod\\data\\reward_clips" : "reward_clips";
    }
    eval.SetClipFolder(folder);
    eval.SetClipWindowSeconds(seconds);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    printf("[server] clips -> %s (rolling %.0fs)\n", folder.c_str(), seconds);
}

void ConfigureTasks(rs::RewardEval& eval, const std::string& path) {
    if (path.empty()) {
        printf("[server] no --tasks specified; task suite inactive (pass --tasks <file.json> to load a tasklist)\n");
    } else if (eval.LoadTasks(path)) {
        printf("[server] preloaded tasks '%s' (%d tasks)\n", path.c_str(), (int)eval.GetTasksJson().size());
    } else {
        printf("[server] failed to load tasks from '%s'\n", path.c_str());
    }
}

void ConfigureReward(rs::RewardEval& eval, const std::string& config) {
    if (config.empty()) return;
    std::string err;
    if (eval.Load(config, err)) printf("[server] preloaded config '%s' (%d terms)\n", config.c_str(), eval.Count());
    else printf("[server] preload failed: %s\n", err.c_str());
}

int Listen(rs::RewardEval& eval, const Options& o) {
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)o.port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int yes = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) != 0) { printf("bind failed on port %d\n", o.port); return 1; }
    listen(listenSock, 1);
    printf("[server] listening on 127.0.0.1:%d\n", o.port);

    rs::Session session(eval, o.config);
    for (;;) {
        SOCKET c = accept(listenSock, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        printf("[server] client connected\n");
        rs::LineSocket client(c);
        session.Serve(client);
    }
}

}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    Options options;
    if (!ParseArgs(argc, argv, options)) return 2;
    if (options.help) { printf("%s", kUsage); return 0; }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("WSAStartup failed\n"); return 1; }

    rs::RewardEval eval;
    ConfigureMirror(eval, options.meshes);
    ConfigureClips(eval, options.clips, options.clipSeconds);
    ConfigureTasks(eval, options.tasks);
    ConfigureReward(eval, options.config);

    if (options.checkTiming) return rs::CheckReconstructionTiming(eval);
    if (!options.replayPaths.empty()) {
        if (!eval.Ready()) { printf("[replay] --replay needs --config\n"); return 1; }
        return rs::ReplayPaths(eval, options.replayPaths, options.replay);
    }
    return Listen(eval, options);
}
