#include "replay_cli.h"
#include "RocketSim.h"
#include "rlpr/RLPRExporter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace rs {

using json = nlohmann::json;

namespace {

std::string ShortName(const std::string& name) { return name.substr(name.rfind(':') + 1); }

void PrintError(bool asJson, const char* message) {
    if (asJson) printf("{\"ok\":false,\"error\":\"%s\"}\n", message);
    else printf("[replay] %s\n", message);
}

}

int CheckReconstructionTiming(RewardEval& eval) {
    if (!eval.MirrorEnabled()) { printf("timing check requires collision meshes\n"); return 2; }
    auto arena = std::unique_ptr<RocketSim::Arena>(RocketSim::Arena::Create(RocketSim::GameMode::SOCCAR));
    auto car = arena->AddCar(RocketSim::Team::BLUE);
    auto carState = car->GetState();
    carState.pos = Vec(0, 1000, 17);
    carState.vel = Vec(1500, 0, 0);
    car->SetState(carState);
    auto ballState = arena->ball->GetState();
    ballState.pos = Vec(300, 1000, 92);
    arena->ball->SetState(ballState);
    RLGC::GameState previous(arena.get());
    int failures = 0, contacts = 0;
    for (int t = 0; t < 40; ++t) {
        arena->Step(1);
        RLGC::GameState observed(arena.get());
        const auto hit = car->GetState().ballHitInfo;
        const bool expected = hit.isValid && hit.tickCountWhenHit == arena->tickCount - 1;
        contacts += expected;
        auto reconstructed = observed;
        eval.ReconstructState(reconstructed, &previous, 1);
        if (reconstructed.players[0].ballTouchedStep != expected) {
            printf("timing mismatch at source tick %d: inferred=%d native=%d\n", t + 1,
                   int(reconstructed.players[0].ballTouchedStep), int(expected));
            ++failures;
        }
        if ((reconstructed.ball.pos - observed.ball.pos).LengthSq() > 0.0f ||
            (reconstructed.players[0].pos - observed.players[0].pos).LengthSq() > 0.0f) ++failures;
        previous = observed;
    }
    if (!contacts) { printf("timing fixture never contacted ball\n"); ++failures; }
    auto initial = previous;
    initial.players[0].ballTouchedStep = initial.players[0].ballTouchedTick = true;
    eval.ReconstructState(initial, nullptr, 1);
    if (initial.players[0].ballTouchedStep || initial.players[0].ballHitInfo.isValid) ++failures;
    auto teleported = previous;
    teleported.ball.pos.x += 3000;
    teleported.players[0].ballTouchedStep = true;
    eval.ReconstructState(teleported, &previous, 1);
    if (teleported.players[0].ballTouchedStep) ++failures;
    if (eval.Ready()) {
        RewardTerms terms;
        initial.players[0].ballTouchedStep = true;
        if (!eval.EvaluateState(initial, 4, 0, false, terms, false) || !initial.players[0].ballTouchedStep) ++failures;
    } else ++failures;
    printf("reconstruction timing failures=%d; native contact ticks=%d\n", failures, contacts);
    return failures ? 1 : 0;
}

int ReplayClip(RewardEval& eval, const std::string& path, const ReplayOptions& options) {
    const int focus = options.focus;
    const bool asJson = options.json;
    if ((!options.trustTouch && !eval.MirrorEnabled()) || focus < 0 || options.tickSkip < 1) {
        PrintError(true, "reconstruction requires meshes; focus must be nonnegative and tick skip positive");
        return 1;
    }
    std::vector<RLGC::GameState> ticks;
    RLGC::RLPRMetadata metadata;
    if (!RLGC::ImportClipFromRLPR(path, ticks, &metadata) || ticks.empty()) {
        const std::string message = "failed to read " + path;
        PrintError(asJson, message.c_str());
        return 1;
    }
    if (!asJson) {
        printf("\n[replay] %s: %d ticks, %d cars, %.1fs\n", path.c_str(), (int)ticks.size(),
               (int)ticks.front().players.size(), ticks.size() * metadata.frameIntervalSeconds);
    }

    eval.Reset();
    eval.SetStep(500000000000ULL);    const unsigned sourceInterval = std::max(1u, metadata.frameIntervalTicks);
    const size_t sampleStride = std::max<size_t>(1, (static_cast<unsigned>(options.tickSkip) + sourceInterval - 1u) / sourceInterval);
    const unsigned evaluatedInterval = static_cast<unsigned>(sampleStride) * sourceInterval;
    if (options.frameOffset < 0 || static_cast<size_t>(options.frameOffset) >= sampleStride) {
        PrintError(true, "replay frame offset must be within one evaluation stride");
        return 1;
    }

    std::vector<float> sums;
    json jsonTicks = json::array();
    size_t evaluatedTicks = 0;

    for (size_t t = static_cast<size_t>(options.frameOffset); t < ticks.size(); t += sampleStride) {
        RLGC::GameState gs = ticks[t];
        std::vector<bool> touched(gs.players.size(), false);
        std::vector<RocketSim::BallHitInfo> hits(gs.players.size());
        std::vector<decltype(gs.players[0].eventState)> events(gs.players.size());
        int lastTouch = gs.lastTouchCarID;
        bool goalScored = gs.goalScored;
        const size_t first = t < sampleStride ? 0 : t - sampleStride + 1;
        for (size_t s = first; s <= t; ++s) {
            auto observed = ticks[s];
            observed.lastTickCount = s * sourceInterval;
            if (!options.trustTouch)
                eval.ReconstructState(observed, s ? &ticks[s - 1] : nullptr, sourceInterval);
            for (size_t p = 0; p < gs.players.size(); ++p) {
                const auto& e = observed.players[p].eventState;
                auto& sum = events[p];
                sum.goal |= e.goal; sum.assist |= e.assist;
                sum.shot |= e.shot; sum.shotPass |= e.shotPass; sum.save |= e.save;
                sum.bump |= e.bump; sum.bumped |= e.bumped;
                sum.demo |= e.demo; sum.demoed |= e.demoed;
                if (observed.players[p].ballTouchedStep) {
                    touched[p] = true;
                    hits[p] = observed.players[p].ballHitInfo;
                    lastTouch = observed.players[p].carId;
                }
            }
            goalScored = goalScored || observed.goalScored;
            if (s == t) gs = std::move(observed);
        }
        gs.goalScored = goalScored;
        gs.lastTouchCarID = lastTouch;
        for (size_t p = 0; p < gs.players.size(); ++p) {
            gs.players[p].ballTouchedStep = touched[p];
            gs.players[p].ballHitInfo = hits[p];
            gs.players[p].eventState = events[p];
        }

        RewardTerms terms;
        if (!eval.EvaluateState(gs, static_cast<int>(evaluatedInterval), focus, true, terms, false)) continue;
        ++evaluatedTicks;
        if (sums.empty()) sums.assign(terms.names.size(), 0.0f);

        const RLGC::Player& p = gs.players[std::min<size_t>(focus, gs.players.size() - 1)];
        const double seconds = t * metadata.frameIntervalSeconds;

        json tickObj;
        if (asJson) {
            tickObj["t"] = seconds;
            tickObj["car_z"] = p.pos.z;
            tickObj["ball_z"] = gs.ball.pos.z;
            tickObj["car_speed"] = p.vel.Length();
            tickObj["ball_speed"] = gs.ball.vel.Length();
            tickObj["dist_to_ball"] = (p.pos - gs.ball.pos).Length();
            tickObj["touch"] = (bool)p.ballTouchedStep;
            tickObj["is_flipping"] = (bool)p.isFlipping;
            tickObj["is_on_ground"] = (bool)p.isOnGround;
            tickObj["has_flipped"] = (bool)p.hasFlipped;
            tickObj["has_jumped"] = (bool)p.hasJumped;
            tickObj["boost"] = p.boost;
            tickObj["ctrl_jump"] = p.prevAction.jump;
            tickObj["ctrl_pitch"] = p.prevAction.pitch;
            tickObj["ctrl_yaw"] = p.prevAction.yaw;
            tickObj["ctrl_roll"] = p.prevAction.roll;
            tickObj["terms"] = json::object();
            if (options.extra && eval.Extension()) tickObj.update(eval.Extension()->TickReport(focus));
        }

        std::string line;
        for (size_t i = 0; i < terms.names.size(); ++i) {
            const float w = i < terms.weighted.size() ? terms.weighted[i] : terms.values[i];
            sums[i] += w;
            const std::string& name = terms.names[i];
            if (asJson && std::fabs(w) >= 1e-5f) tickObj["terms"][ShortName(name)] = w;
            if (std::fabs(w) < 1e-4f) continue;
            if (!options.termFilter.empty() &&
                std::none_of(options.termFilter.begin(), options.termFilter.end(),
                             [&](const std::string& f) { return name.find(f) != std::string::npos; })) continue;
            char buf[128];
            snprintf(buf, sizeof(buf), "  %s=%.3f", ShortName(name).c_str(), w);
            line += buf;
        }

        if (asJson) jsonTicks.push_back(tickObj);
        else if (!line.empty())
            printf("[replay] t=%5.2fs carZ=%6.0f ballZ=%6.0f touch=%d flip=%d%s\n", seconds, p.pos.z, gs.ball.pos.z,
                   (int)p.ballTouchedStep, (int)p.isFlipping, line.c_str());
    }

    if (!asJson) {
        printf("[replay] totals:");
        for (size_t i = 0; i < sums.size(); ++i)
            if (std::fabs(sums[i]) >= 1e-4f) printf("  %s=%.2f", ShortName(eval.Names()[i]).c_str(), sums[i]);
        printf("\n");
        return 0;
    }

    json out;
    out["ok"] = true;
    out["path"] = path;
    out["ticks_count"] = ticks.size();
    out["evaluated_ticks_count"] = evaluatedTicks;
    out["source_tick_rate_hz"] = metadata.sourceTickRateHz;
    out["frame_interval_ticks"] = metadata.frameIntervalTicks;
    out["evaluated_interval_ticks"] = evaluatedInterval;
    out["evaluation_frame_offset"] = options.frameOffset;
    out["contact_source"] = options.trustTouch ? "recorded_flags_unverified" : "preceding_interval_reconstruction";
    out["contact_is_native_rl_truth"] = false;
    if (options.extra && eval.Extension()) out.update(eval.Extension()->ReplayReport());
    out["duration_seconds"] = ticks.size() * metadata.frameIntervalSeconds;
    out["cars_count"] = ticks.front().players.size();
    out["totals"] = json::object();
    float grandTotal = 0.0f;
    for (size_t i = 0; i < sums.size(); ++i) {
        if (std::fabs(sums[i]) < 1e-4f) continue;
        out["totals"][ShortName(eval.Names()[i])] = sums[i];
        grandTotal += sums[i];
    }
    out["grand_total"] = grandTotal;
    out["ticks"] = jsonTicks;
    printf("%s\n", out.dump().c_str());
    return 0;
}

int ReplayPaths(RewardEval& eval, const std::vector<std::string>& paths, const ReplayOptions& options) {
    int rc = 0;
    for (const auto& path : paths) {
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) {
            rc |= ReplayClip(eval, path, options);
            continue;
        }
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec))
            if (entry.path().extension() == ".rlpr") files.push_back(entry.path().string());
        std::sort(files.begin(), files.end());
        for (const auto& file : files) rc |= ReplayClip(eval, file, options);
    }
    return rc;
}

}
