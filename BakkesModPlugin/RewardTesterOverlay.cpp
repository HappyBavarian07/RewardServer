#include "RewardTesterPlugin.h"
#include "bakkesmod/wrappers/includes.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <filesystem>

namespace {

const ImVec4 kGreen(0.4f, 1.0f, 0.4f, 1.0f);
const ImVec4 kRed(1.0f, 0.4f, 0.4f, 1.0f);
const ImVec4 kAmber(1.0f, 0.85f, 0.2f, 1.0f);
const ImVec4 kDim(0.65f, 0.65f, 0.65f, 1.0f);

double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool ContainsInsensitive(const std::string &haystack, const char *needle) {
    if (!needle || !*needle) return true;
    const std::string n(needle);
    const auto it = std::search(haystack.begin(), haystack.end(), n.begin(), n.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != haystack.end();
}

bool ColoredButton(const char *label, ImVec4 color, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 1.2f, color.y * 1.2f, color.z * 1.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x * 0.8f, color.y * 0.8f, color.z * 0.8f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

}

void RewardTesterPlugin::ApplyKeybinds() {
    gameWrapper->Execute([this](GameWrapper *) {
        static const char *binds[] = {
            "bind NumPadZero \"rewardtester_toggle_menu\"",
            "bind X \"rewardtester_save_task_clip\"",
            "bind C \"rewardtester_save_clip\"",
            "bind NumPadAdd \"rewardtester_next_task\"",
            "bind NumPadSubtract \"rewardtester_prev_task\"",
            "bind NumPadMultiply \"rewardtester_mark_scored\"",
            "bind NumPadDivide \"rewardtester_mark_miss\"",
            "bind Decimal \"rewardtester_undo_attempt\"",
        };
        for (const char *bind : binds) cvarManager->executeCommand(bind);
    });
}

void RewardTesterPlugin::DrawHeader() {
    const bool connected = client_ && client_->Connected();
    if (connected)
        ImGui::TextColored(kGreen, "CONNECTED");
    else
        ImGui::TextColored(kRed, "DISCONNECTED");
    ImGui::SameLine();
    if (connected)
        ImGui::TextDisabled("%d terms  |  total %.3f  |  %s", client_->RewardCount(), totalWeightedReward,
                            watch.inReplay ? "watching a replay" : "live");
    else
        ImGui::TextDisabled("%s", client_ ? client_->LastError().c_str() : "no client");

    if (ImGui::Button(("Save last " + std::to_string((int) clipDurationSeconds) + "s").c_str())) SaveClip(std::string());
    ImGui::SameLine();
    if (ImGui::Button("Reset state")) SendRewardReset();
    if (client_) {
        const auto cs = client_->LatestClipState();
        ImGui::SameLine();
        if (cs.pending)
            ImGui::TextColored(kAmber, "saving...");
        else if (!cs.lastSavedPath.empty())
            ImGui::TextDisabled("saved %s", std::filesystem::path(cs.lastSavedPath).filename().string().c_str());
    }
    ImGui::Separator();
}

void RewardTesterPlugin::DrawLiveTab() {
    if (ImGui::CollapsingHeader("Setup")) {
        uint64_t stepMin = 0;
        uint64_t stepMax = 1000000000000ULL;
        ImGui::SliderScalar("Training step", ImGuiDataType_U64, &currentTrainingStep, &stepMin, &stepMax, "%llu");
        if (ImGui::Button("0")) currentTrainingStep = 0;
        ImGui::SameLine();
        if (ImGui::Button("+1B")) currentTrainingStep += 1000000000ULL;
        ImGui::SameLine();
        if (ImGui::Button("+10B")) currentTrainingStep += 10000000000ULL;
        ImGui::SameLine();
        if (ImGui::Button("+50B")) currentTrainingStep += 50000000000ULL;
        ImGui::Checkbox("Force late-schedule step (fully faded-in weights)", &forceAllPhases);
        ImGui::Checkbox("Reward debug logging (server console)", &debugRewards);
        ImGui::SliderInt("Rolling average window (ticks)", &fadingTimesteps, 10, 1200);
    }

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##termfilter", "filter terms", termFilterBuf, sizeof(termFilterBuf));
    ImGui::SameLine();
    ImGui::Checkbox("active only", &termActiveOnly);

    const RewardResult res = client_ ? client_->LatestResult() : RewardResult{};
    std::vector<std::vector<float>> history;
    {
        std::lock_guard<std::mutex> lock(historyMut);
        history.reserve(termHistory.size());
        for (const auto &series : termHistory) history.emplace_back(series.begin(), series.end());
    }

    if (ImGui::BeginChild("RewardList", ImVec2(0, 0), true)) {
        ImGui::Columns(6, "terms", true);
        ImGui::SetColumnWidth(0, 210.0f);
        ImGui::SetColumnWidth(1, 60.0f);
        for (const char *title : {"Term", "Weight", "Raw", "Weighted", "Trend", "Avg"}) {
            ImGui::TextUnformatted(title);
            ImGui::NextColumn();
        }
        ImGui::Separator();
        for (size_t i = 0; i < res.names.size(); ++i) {
            const float v = i < res.values.size() ? res.values[i] : 0.0f;
            if (!ContainsInsensitive(res.names[i], termFilterBuf)) continue;
            if (termActiveOnly && std::abs(v) <= 1e-3f) continue;
            const float w = i < res.weights.size() ? res.weights[i] : 0.0f;
            const float wv = i < res.weighted.size() ? res.weighted[i] : 0.0f;
            const float avg = i < rollingAvgValues.size() ? rollingAvgValues[i] : 0.0f;
            const ImVec4 col = v > 0 ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f)
                                     : v < 0 ? ImVec4(1.0f, 0.5f, 0.5f, 1.0f) : kDim;
            ImGui::TextUnformatted(res.names[i].c_str());
            ImGui::NextColumn();
            ImGui::Text("%.3f", w);
            ImGui::NextColumn();
            ImGui::TextColored(col, "%.4f", v);
            ImGui::NextColumn();
            ImGui::Text("%.4f", wv);
            ImGui::NextColumn();
            if (i < history.size() && history[i].size() > 1) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::PlotLines("##trend", history[i].data(), static_cast<int>(history[i].size()), 0, nullptr, FLT_MAX,
                                 FLT_MAX, ImVec2(ImGui::GetContentRegionAvail().x, 16.0f));
                ImGui::PopID();
            }
            ImGui::NextColumn();
            ImGui::Text("%.4f", avg);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }
    ImGui::EndChild();
}

void RewardTesterPlugin::DrawShotsTab() {
    if (!client_) return;
    const std::vector<ShotTask> tasks = client_->LatestTasks();
    const int active = client_->ActiveTaskIndex();

    if (tasks.empty()) {
        ImGui::TextColored(kAmber, "No shot tasks loaded. Start the server with --tasks <file>, or load one under Task file.");
    }

    const float listWidth = ImGui::GetContentRegionAvail().x * 0.36f;
    if (ImGui::BeginChild("tasklist", ImVec2(listWidth, ImGui::GetContentRegionAvail().y - 4.0f), true)) {
        for (size_t i = 0; i < tasks.size(); ++i) {
            const auto &t = tasks[i];
            const bool done = t.completedAttempts >= t.targetAttempts;
            ImGui::PushStyleColor(ImGuiCol_Text, done ? kGreen : t.completedAttempts > 0 ? kAmber : kDim);
            const std::string label = t.name + "  " + std::to_string(t.completedAttempts) + "/" +
                                      std::to_string(t.targetAttempts);
            if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == active)) client_->RequestSetActiveTask(static_cast<int>(i));
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    if (active >= 0 && active < (int) tasks.size()) {
        const auto &cur = tasks[active];
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%s", cur.name.c_str());
        if (!cur.expectedMode.empty()) ImGui::TextDisabled("%s", cur.expectedMode.c_str());
        ImGui::TextWrapped("%s", cur.instructions.c_str());
        ImGui::Spacing();
        const float fraction = cur.targetAttempts > 0
            ? std::min(1.0f, static_cast<float>(cur.completedAttempts) / static_cast<float>(cur.targetAttempts)) : 0.0f;
        const std::string progress = std::to_string(cur.completedAttempts) + " / " + std::to_string(cur.targetAttempts) +
                                     " attempts, " + std::to_string(cur.successfulAttempts) + " scored";
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 18.0f), progress.c_str());
        ImGui::Spacing();

        const ImVec2 big(ImGui::GetContentRegionAvail().x * 0.5f - 4.0f, 38.0f);
        if (ColoredButton("Save shot  [X]", ImVec4(0.2f, 0.4f, 0.7f, 1.0f), big)) {
            client_->RequestSaveTaskClip({clipOnlyFocus, watch.inReplay ? "replay" : "live"});
            lastTaskSaveTime = NowSeconds();
            if (autoMarkGoals && NowSeconds() - lastGoalTime < 20.0) client_->RequestLogTaskAttempt(true);
        }
        ImGui::SameLine();
        if (ColoredButton("Undo  [NumPad .]", ImVec4(0.35f, 0.35f, 0.4f, 1.0f), big)) client_->RequestUndoTaskAttempt();
        if (ColoredButton("Scored  [NumPad *]", ImVec4(0.15f, 0.55f, 0.25f, 1.0f), big)) client_->RequestLogTaskAttempt(true);
        ImGui::SameLine();
        if (ColoredButton("Miss  [NumPad /]", ImVec4(0.65f, 0.25f, 0.2f, 1.0f), big)) client_->RequestLogTaskAttempt(false);

        const LastAttemptInfo last = client_->LastAttempt();
        ImGui::Spacing();
        if (last.number > 0) {
            const ImVec4 col = last.outcome == "scored" ? kGreen : last.outcome == "miss" ? kRed : kAmber;
            ImGui::TextColored(col, "Attempt #%d: %s", last.number,
                               last.outcome == "unmarked" ? "saved, not marked yet (counts as a miss)" : last.outcome.c_str());
        } else {
            ImGui::TextDisabled("No attempt yet. Save a shot, then mark it.");
        }
        ImGui::Spacing();
        ImGui::Checkbox("Mark a goal as scored automatically", &autoMarkGoals);
        ImGui::Checkbox("Go to the next shot after the last attempt", &autoAdvance);
        ImGui::Checkbox("Save only my car (smaller clips)", &clipOnlyFocus);
        ImGui::Spacing();
        if (ImGui::Button("Previous shot [NumPad -]")) client_->RequestSetActiveTask((active - 1 + (int) tasks.size()) % (int) tasks.size());
        ImGui::SameLine();
        if (ImGui::Button("Next shot [NumPad +]")) client_->RequestSetActiveTask((active + 1) % (int) tasks.size());
        ImGui::SameLine();
        if (ImGui::Button("Reset this shot")) client_->RequestResetTask(active);
    }

    if (ImGui::CollapsingHeader("Task file")) {
        const std::string serverPath = client_->TasksPath();
        if (!serverPath.empty() && serverPath != syncedTasksJsonPath) {
            syncedTasksJsonPath = serverPath;
            std::snprintf(tasksJsonPathBuf, sizeof(tasksJsonPathBuf), "%s", serverPath.c_str());
        }
        ImGui::InputText("##tasksjson", tasksJsonPathBuf, sizeof(tasksJsonPathBuf));
        if (ImGui::Button("Load")) client_->RequestReloadTasks(tasksJsonPathBuf);
        ImGui::SameLine();
        if (ImGui::Button("Reset all counters")) client_->RequestResetTask(-1);
    }
    ImGui::EndGroup();
}

void RewardTesterPlugin::DrawClipsTab() {
    if (!client_) return;
    const auto cs = client_->LatestClipState();

    if (ImGui::Button("Refresh")) RefreshClipList();
    ImGui::SameLine();
    ImGui::TextDisabled("%d clips  |  %s", (int) cs.clips.size(), cs.folder.c_str());

    const float leftWidth = ImGui::GetContentRegionAvail().x * 0.42f;
    const float height = ImGui::GetContentRegionAvail().y - 4.0f;

    if (ImGui::BeginChild("clipbrowser", ImVec2(leftWidth, height), true)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##clipsearch", "search", clipSearchBuf, sizeof(clipSearchBuf));
        static const char *filters[] = {"All clips", "Shot attempts", "Generic clips"};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##clipfilter", &clipFilter, filters, 3);

        auto accepts = [&](const std::string &clip) {
            if (!ContainsInsensitive(clip, clipSearchBuf)) return false;
            if (clipFilter == 1) return clip.find("attempt_") != std::string::npos;
            if (clipFilter == 2) return clip.rfind("generic", 0) == 0;
            return true;
        };
        auto select = [&](const std::string &clip) {
            const auto it = std::find(cs.clips.begin(), cs.clips.end(), clip);
            selectedClipIndex = it == cs.clips.end() ? -1 : static_cast<int>(std::distance(cs.clips.begin(), it));
            selectedClipPath = clip;
            OpenReplayClip(clip);
        };

        if (ImGui::BeginChild("cliptree", ImVec2(0, 0), false)) {
            if (clipSearchBuf[0] != '\0' || clipFilter != 0) {
                for (const auto &clip : cs.clips) {
                    if (!accepts(clip)) continue;
                    if (ImGui::Selectable(clip.c_str(), clip == selectedClipPath)) select(clip);
                }
            } else {
                size_t signature = cs.clips.size();
                for (const auto &clip : cs.clips) signature = signature * 1000003u + std::hash<std::string>()(clip);
                if (signature != clipGroupSignature) {
                    clipGroupCache.clear();
                    BuildClipGroups(clipGroupCache);
                    clipGroupSignature = signature;
                }
                const std::vector<ClipGroupEntry> &groups = clipGroupCache;
                auto draw = [&](auto &&self, const std::vector<ClipGroupEntry> &level, const std::string &prefix) -> void {
                    for (const auto &group : level) {
                        ImGui::PushID((prefix + group.group).c_str());
                        if (ImGui::TreeNode(group.group.c_str())) {
                            for (const auto &clip : group.items) {
                                const std::string name = std::filesystem::path(clip).filename().string();
                                if (ImGui::Selectable(name.c_str(), clip == selectedClipPath)) select(clip);
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", clip.c_str());
                            }
                            self(self, group.children, prefix + group.group + "/");
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                };
                draw(draw, groups, std::string());
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    if (selectedClipPath.empty()) {
        ImGui::TextDisabled("Select a clip to play it in the game.");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%s", std::filesystem::path(selectedClipPath).filename().string().c_str());
        ImGui::TextDisabled("%s", selectedClipPath.c_str());
    }

    if (replayActive) {
        const float duration = replayFrames.size() > 1
            ? static_cast<float>(replayFrames.size() - 1) * replayFrameIntervalSeconds : 0.0f;
        ImGui::Text("%zu car%s, %zu frames, %.1f s", replayCarCount, replayCarCount == 1 ? "" : "s", replayFrames.size(), duration);
        ImGui::Spacing();
        if (ImGui::Button(isPlayingClip ? "Pause [P]" : "Play [P]")) isPlayingClip = !isPlayingClip;
        ImGui::SameLine();
        if (ImGui::Button("Restart [Z]")) {
            replayTime = 0.0f;
            replayNeedsReseed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop [Esc]")) StopReplayClip();
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &replayLoop);

        float position = replayTime;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##scrub", &position, 0.0f, std::max(duration, 0.01f), "%.2f s")) {
            replayTime = position;
            replayNeedsReseed = true;
        }
        if (ImGui::Button("<< 10")) { replayTime = std::max(0.0f, replayTime - replayFrameIntervalSeconds * 10.0f); replayNeedsReseed = true; }
        ImGui::SameLine();
        if (ImGui::Button("<")) { replayTime = std::max(0.0f, replayTime - replayFrameIntervalSeconds); replayNeedsReseed = true; }
        ImGui::SameLine();
        if (ImGui::Button(">")) { replayTime += replayFrameIntervalSeconds; replayNeedsReseed = true; }
        ImGui::SameLine();
        if (ImGui::Button("10 >>")) { replayTime += replayFrameIntervalSeconds * 10.0f; replayNeedsReseed = true; }
        ImGui::SameLine();
        ImGui::TextDisabled("frame %zu / %zu", replayFrameIndex + 1, replayFrames.size());

        for (const float preset : {0.1f, 0.25f, 0.5f, 1.0f, 2.0f}) {
            if (ImGui::Button((std::to_string(preset).substr(0, 4) + "x").c_str())) replayRate = preset;
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("##speed", &replayRate, 0.1f, 50.0f, "%.2fx", 3.0f);

        if (ImGui::CollapsingHeader("Playback mode")) {
            ImGui::Checkbox("Teleport car and ball every tick (fun mode)", &funReplayMode);
            ImGui::Checkbox("Re-simulate from recorded inputs", &replayInputDriven);
        }
    }

    if (ImGui::CollapsingHeader("File actions")) {
        ImGui::InputTextWithHint("##newfolder", "new folder", newClipFolderBuf, sizeof(newClipFolderBuf));
        ImGui::SameLine();
        if (ImGui::Button("Create") && newClipFolderBuf[0] != '\0') {
            client_->RequestCreateClipFolder(newClipFolderBuf);
            newClipFolderBuf[0] = '\0';
        }
        ImGui::InputTextWithHint("##rename", "rename selected to", renameClipBuf, sizeof(renameClipBuf));
        ImGui::SameLine();
        if (ImGui::Button("Rename") && !selectedClipPath.empty() && renameClipBuf[0] != '\0') {
            client_->RequestRenameClipPath(selectedClipPath, renameClipBuf);
            renameClipBuf[0] = '\0';
        }
        ImGui::InputTextWithHint("##move", "move selected to folder", moveClipBuf, sizeof(moveClipBuf));
        ImGui::SameLine();
        if (ImGui::Button("Move") && !selectedClipPath.empty() && moveClipBuf[0] != '\0') {
            client_->RequestMoveClip(selectedClipPath, moveClipBuf);
            moveClipBuf[0] = '\0';
        }
    }
    ImGui::EndGroup();
    clipGroupsInitialized = true;
}

void RewardTesterPlugin::DrawWatchTab() {
    if (!watch.inReplay) {
        ImGui::TextWrapped("Open a saved replay in Rocket League and pick a player to follow with the replay camera. "
                           "This tab follows the same car: rewards are evaluated for the player you watch, and clips are "
                           "saved from what you see.");
        ImGui::Spacing();
        ImGui::TextDisabled("Not in a replay.");
    } else {
        ImGui::TextColored(kGreen, "Watching a replay");
        ImGui::Text("Following: %s", watch.followedName.c_str());
        ImGui::Text("Replay frame %d at %d fps, %d cars", watch.frame, watch.fps, watch.cars);
        ImGui::Text("Samples sent: %llu", (unsigned long long) watch.samples);
        if (watch.followedIndex < 0) ImGui::TextColored(kAmber, "No player is followed; choose one with the replay camera.");
    }
    ImGui::Separator();
    ImGui::Checkbox("Evaluate rewards while watching", &watchEvaluate);
    ImGui::TextDisabled("Replays carry no controller inputs, so input-based terms read zero here.");
    ImGui::Spacing();
    const ImVec2 wide(ImGui::GetContentRegionAvail().x * 0.5f - 4.0f, 34.0f);
    if (ColoredButton("Save clip: whole game state", ImVec4(0.2f, 0.4f, 0.7f, 1.0f), wide)) {
        if (client_) client_->RequestSaveClip(std::string(), {false, watch.inReplay ? "replay" : "live"});
    }
    ImGui::SameLine();
    if (ColoredButton("Save clip: followed car only", ImVec4(0.3f, 0.5f, 0.35f, 1.0f), wide)) {
        if (client_) client_->RequestSaveClip(std::string(), {true, watch.inReplay ? "replay" : "live"});
    }
    ImGui::TextDisabled("Length: the last %d s. Clips with only the followed car keep the ball and that one car.",
                        (int) clipDurationSeconds);
    if (client_) {
        const auto cs = client_->LatestClipState();
        if (cs.pending) ImGui::TextColored(kAmber, "saving...");
        else if (!cs.lastSavedPath.empty()) ImGui::TextColored(kGreen, "saved: %s", cs.lastSavedPath.c_str());
    }
}

void RewardTesterPlugin::DrawSettingsTab() {
    if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Host", serverHostBuf, sizeof(serverHostBuf));
        ImGui::InputInt("Port", &serverPort);
        ImGui::InputTextWithHint("Config", "empty = the server's own config", configFilePathBuf, sizeof(configFilePathBuf));
        if (ImGui::Button("Connect / reconnect")) Reconnect();
        ImGui::SameLine();
        if (ImGui::Button("Reload config")) ReloadConfig();
    }

    if (ImGui::CollapsingHeader("Clips and input", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("Clip length (s)", &clipDurationSeconds, 1.0f, 300.0f, "%.0f")) {
            clipDurationSeconds = std::clamp(clipDurationSeconds, 1.0f, 300.0f);
            SavePreferences();
            if (client_) client_->RequestSetClipWindow(clipDurationSeconds);
        }
        bool changed = false;
        changed |= ImGui::SliderFloat("Steering sensitivity", &inputSteerSensitivity, 1.0f, 10.0f, "%.2f");
        changed |= ImGui::SliderFloat("Aerial sensitivity", &inputAerialSensitivity, 1.0f, 10.0f, "%.2f");
        changed |= ImGui::SliderFloat("Controller deadzone", &inputDeadzone, 0.0f, 0.5f, "%.2f");
        if (changed) SavePreferences();
        ImGui::TextDisabled("Match these to your Rocket League controller settings.");
        ImGui::Checkbox("Save only my car in clips", &clipOnlyFocus);
    }

    if (ImGui::CollapsingHeader("Game speed")) {
        ImGui::SliderFloat("Speed", &gameSpeed, 0.1f, 2.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::Button("Apply")) {
            gameWrapper->Execute([this](GameWrapper *) {
                cvarManager->executeCommand("sv_soccar_gamespeed " + std::to_string(gameSpeed));
            });
        }
        ImGui::TextDisabled("Freeplay and training only. Set 1.00 to go back to normal.");
    }

    if (ImGui::CollapsingHeader("Alerts")) {
        const RewardResult res = client_ ? client_->LatestResult() : RewardResult{};
        if (!res.names.empty()) {
            alertTermIndex = std::clamp(alertTermIndex, 0, static_cast<int>(res.names.size()) - 1);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##alertterm", res.names[alertTermIndex].c_str())) {
                for (int i = 0; i < static_cast<int>(res.names.size()); ++i)
                    if (ImGui::Selectable(res.names[i].c_str(), i == alertTermIndex)) alertTermIndex = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::Combo("##alertop", &alertOp, "above\0below\0");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::InputFloat("##alertvalue", &alertValue);
            ImGui::SameLine();
            if (ImGui::Button("Add")) alerts.push_back({res.names[alertTermIndex], alertOp == 0, alertValue, 0.0});
        }
        for (size_t i = 0; i < alerts.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%s %s %.3f", alerts[i].term.c_str(), alerts[i].above ? "above" : "below", alerts[i].value);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                alerts.erase(alerts.begin() + static_cast<long>(i));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::TextDisabled("A toast shows when the raw value crosses the limit. Alerts are not saved between sessions.");
    }

    if (ImGui::CollapsingHeader("Keybinds", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Apply all keybinds")) ApplyKeybinds();
        ImGui::SameLine();
        ImGui::TextDisabled("Rebinds the keys below if you lost them.");
        struct Row { const char *key; const char *command; const char *what; };
        static const Row rows[] = {
            {"NumPad 0", "rewardtester_toggle_menu", "show or hide this window"},
            {"X", "rewardtester_save_task_clip", "save the shot as the next attempt"},
            {"NumPad *", "rewardtester_mark_scored", "mark the latest attempt scored"},
            {"NumPad /", "rewardtester_mark_miss", "mark the latest attempt missed"},
            {"NumPad .", "rewardtester_undo_attempt", "remove the latest attempt"},
            {"NumPad +  /  -", "rewardtester_next_task / prev_task", "change shot"},
            {"C", "rewardtester_save_clip", "save a generic clip"},
        };
        ImGui::Columns(3, "keys", false);
        ImGui::SetColumnWidth(0, 110.0f);
        ImGui::SetColumnWidth(1, 240.0f);
        for (const auto &r : rows) {
            ImGui::TextUnformatted(r.key);
            ImGui::NextColumn();
            ImGui::TextDisabled("%s", r.command);
            ImGui::NextColumn();
            ImGui::TextUnformatted(r.what);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }
}

void RewardTesterPlugin::DrawReplayOverlay() {
    if (!replayActive || replayFrames.empty()) return;
    const float duration = replayFrames.size() > 1
        ? static_cast<float>(replayFrames.size() - 1) * replayFrameIntervalSeconds : 0.0f;
    const float progress = duration > 0.0f ? std::clamp(replayTime / duration, 0.0f, 1.0f) : 0.0f;
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, 24.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(420.0f, 58.0f), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("RewardReplayOverlay", nullptr, flags)) {
        ImGui::TextUnformatted(isPlayingClip ? "REPLAY PLAYING" : "REPLAY PAUSED");
        ImGui::SameLine();
        ImGui::Text("%.2f / %.2f s", replayTime, duration);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 14.0f));
    }
    ImGui::End();
}
