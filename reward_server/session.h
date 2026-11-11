#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include "line_socket.h"
#include "reward_eval.h"

namespace rs {

inline constexpr int kProtocolVersion = 1;

/** Command dispatcher for one reward_server instance; owns the config path used by `reload`. */
class Session {
public:
    Session(RewardEval& eval, std::string initialConfig);

    /** Answers one request object; unknown commands and malformed input yield `{"ok":false,...}`. */
    nlohmann::json Handle(const nlohmann::json& request);

    /** Serves requests until the client disconnects. */
    void Serve(LineSocket& client);

private:
    using Handler = std::function<nlohmann::json(const nlohmann::json&)>;

    void RegisterRewardCommands();
    void RegisterClipCommands();
    void RegisterTaskCommands();
    nlohmann::json TaskReply(bool includePath = false) const;

    RewardEval& eval_;
    std::string lastConfig_;
    std::unordered_map<std::string, Handler> handlers_;
};

}
