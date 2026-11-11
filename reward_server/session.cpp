#include "session.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace rs {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

json Fail(const std::string& error) { return {{"ok", false}, {"error", error}}; }
json Ok() { return {{"ok", true}}; }

bool ResolveClipPath(const RewardEval& eval, const std::string& relative, fs::path& out) {
    if (relative.empty()) return false;
    const fs::path rel(relative);
    if (rel.is_absolute()) return false;
    for (const auto& part : rel) {
        if (part == "..") return false;
    }
    out = fs::path(eval.ClipFolder()) / rel;
    return true;
}

RewardEval::ClipOptions ClipOptionsFrom(const json& req) {
    return {req.value("only_focus", false), req.value("source", std::string("live"))};
}

json RenameResult(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    return ec ? Fail(ec.message()) : Ok();
}

}

Session::Session(RewardEval& eval, std::string initialConfig)
    : eval_(eval), lastConfig_(std::move(initialConfig)) {
    handlers_["hello"] = [this](const json&) {
        json commands = json::array();
        for (const auto& entry : handlers_) commands.push_back(entry.first);
        std::sort(commands.begin(), commands.end());
        return json{{"ok", true}, {"server", "reward_server"}, {"proto", kProtocolVersion}, {"commands", commands}};
    };
    handlers_["ping"] = [](const json&) { return Ok(); };
    RegisterRewardCommands();
    RegisterClipCommands();
    RegisterTaskCommands();
}

json Session::TaskReply(bool includePath) const {
    json reply = {{"ok", true}, {"active_index", eval_.ActiveTaskIndex()}, {"tasks", eval_.GetTasksJson()},
                  {"message", eval_.TaskMessage()}, {"last_attempt", eval_.LastAttemptJson()}};
    if (includePath) reply["tasks_path"] = eval_.TasksPath();
    return reply;
}

void Session::RegisterRewardCommands() {
    auto load = [this](const json& req, const std::string& fallback) {
        const std::string config = req.value("config", fallback);
        lastConfig_ = config;
        std::string err;
        if (!eval_.Load(config, err)) return Fail(err);
        return json{{"ok", true}, {"count", eval_.Count()}, {"names", eval_.Names()}};
    };
    handlers_["load"] = [load](const json& req) { return load(req, std::string()); };
    handlers_["reload"] = [this, load](const json& req) { return load(req, lastConfig_); };

    handlers_["eval"] = [this](const json& req) {
        if (!eval_.Ready()) return Fail("no reward loaded; send load first");
        RewardTerms terms;
        if (!eval_.Evaluate(req, terms)) return Fail("eval failed (no cars or not ready)");
        return json{{"ok", true}, {"total", terms.total}, {"names", terms.names}, {"values", terms.values},
                    {"weights", terms.weights}, {"weighted", terms.weighted}};
    };
    handlers_["capture"] = [this](const json& req) {
        if (!eval_.Capture(req)) return Fail("capture failed (no cars)");
        return json{{"ok", true}, {"frames", eval_.BufferedFrames()}};
    };
    handlers_["reset"] = [this](const json& req) {
        eval_.Reset();
        if (req.value("clear_buffer", false)) eval_.ClearBuffers();
        return Ok();
    };
}

void Session::RegisterClipCommands() {
    handlers_["save_clip"] = [this](const json& req) {
        std::string path, err;
        if (!eval_.SaveClip(req.value("name", std::string()), path, err, req.value("subfolder", "generic"), ClipOptionsFrom(req)))
            return Fail(err);
        printf("[server] saved clip: %s (%d frames)\n", path.c_str(), eval_.BufferedFrames());
        return json{{"ok", true}, {"path", path}, {"frames", eval_.BufferedFrames()}};
    };
    handlers_["list_clips"] = [this](const json&) {
        return json{{"ok", true}, {"folder", eval_.ClipFolder()}, {"clips", eval_.ListClips()}};
    };
    handlers_["clip_info"] = [this](const json&) {
        return json{{"ok", true}, {"folder", eval_.ClipFolder()}, {"window_seconds", eval_.ClipWindowSeconds()},
                    {"buffered_frames", eval_.BufferedFrames()}};
    };
    handlers_["set_clip_window"] = [this](const json& req) {
        eval_.SetClipWindowSeconds(std::max(1.0f, req.value("seconds", 15.0f)));
        return json{{"ok", true}, {"window_seconds", eval_.ClipWindowSeconds()}};
    };
    handlers_["set_clip_folder"] = [this](const json& req) {
        const std::string folder = req.value("folder", "");
        if (folder.empty()) return Fail("empty folder");
        eval_.SetClipFolder(folder);
        std::error_code ec;
        fs::create_directories(folder, ec);
        return json{{"ok", true}, {"folder", folder}};
    };
    handlers_["create_clip_folder"] = [this](const json& req) {
        fs::path folder;
        std::error_code ec;
        const bool valid = ResolveClipPath(eval_, req.value("path", std::string()), folder);
        if (valid && fs::create_directories(folder, ec)) return Ok();
        return Fail(ec ? ec.message() : "folder already exists or path is invalid");
    };
    handlers_["rename_clip_path"] = [this](const json& req) {
        fs::path from, to;
        std::error_code ec;
        const bool valid = ResolveClipPath(eval_, req.value("from", std::string()), from) &&
                           ResolveClipPath(eval_, req.value("to", std::string()), to);
        if (!valid || !fs::exists(from, ec) || fs::exists(to, ec))
            return Fail("invalid source, destination exists, or path is outside the clip folder");
        return RenameResult(from, to);
    };
    handlers_["move_clip"] = [this](const json& req) {
        fs::path source, folder;
        std::error_code ec;
        const bool valid = ResolveClipPath(eval_, req.value("source", std::string()), source) &&
                           ResolveClipPath(eval_, req.value("folder", std::string()), folder);
        const fs::path destination = folder / source.filename();
        if (!valid || !fs::is_regular_file(source, ec) || !fs::is_directory(folder, ec) || fs::exists(destination, ec))
            return Fail("invalid clip, destination folder, or destination already exists");
        return RenameResult(source, destination);
    };
}

void Session::RegisterTaskCommands() {
    handlers_["get_tasks"] = [this](const json&) { return TaskReply(true); };
    handlers_["set_active_task"] = [this](const json& req) {
        if (!eval_.SetActiveTask(req.value("index", 0))) return Fail("invalid task index");
        return TaskReply();
    };
    handlers_["log_task_attempt"] = [this](const json& req) {
        if (!eval_.LogTaskAttempt(req.value("scored", false))) return Fail("failed to log attempt");
        return TaskReply();
    };
    handlers_["undo_task_attempt"] = [this](const json&) {
        if (!eval_.UndoTaskAttempt()) {
            json reply = Fail(eval_.TaskMessage());
            reply["message"] = eval_.TaskMessage();
            return reply;
        }
        return TaskReply();
    };
    handlers_["reset_task"] =[this](const json& req) {
        if (!eval_.ResetTask(req.value("index", -1))) return Fail("failed to reset task");
        return TaskReply();
    };
    handlers_["reload_tasks"] = [this](const json& req) {
        if (!eval_.LoadTasks(req.value("path", ""))) return Fail("failed to reload tasks");
        return TaskReply(true);
    };
    handlers_["save_task_clip"] = [this](const json& req) {
        if (req.contains("clip_folder") && req["clip_folder"].is_string())
            eval_.SetClipFolder(req["clip_folder"].get<std::string>());
        std::string path, err;
        if (!eval_.SaveTaskClip(path, err, req.value("subfolder", ""), ClipOptionsFrom(req))) return Fail(err);
        json reply = TaskReply();
        reply["path"] = path;
        return reply;
    };
}

json Session::Handle(const json& request) {
    if (!request.is_object()) return Fail("request must be a json object");
    const std::string cmd = request.value("cmd", "");
    const auto it = handlers_.find(cmd);
    if (it == handlers_.end()) return Fail("unknown cmd: " + cmd);
    try {
        return it->second(request);
    } catch (const std::exception& e) {
        return Fail(std::string("bad request: ") + e.what());
    }
}

void Session::Serve(LineSocket& client) {
    std::string line;
    while (client.RecvLine(line)) {
        json reply;
        try {
            reply = Handle(json::parse(line));
        } catch (const json::parse_error&) {
            reply = Fail("bad json");
        }
        if (!client.SendLine(reply.dump())) break;
    }
    printf("[server] client disconnected\n");
}

}
