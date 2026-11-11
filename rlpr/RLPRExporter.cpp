#include "RLPRExporter.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

namespace RLGC {
namespace {
constexpr uint8_t RLPR_MAGIC[4] = {'R', 'L', 'P', 'R'};
constexpr uint32_t RLPR_VERSION = 4;
constexpr size_t RLPR_MAX_CARS = 6;
}
#ifndef RLPR_IMPORT_ONLY
namespace {

    inline rlpr::Vec3 toVec3(const Vec& v) { return {v.x, v.y, v.z}; }

    inline rlpr::Mat3 toMat3(const RotMat& r) {
        rlpr::Mat3 m;
        m.rows[0] = {r.forward.x, r.right.x, r.up.x};
        m.rows[1] = {r.forward.y, r.right.y, r.up.y};
        m.rows[2] = {r.forward.z, r.right.z, r.up.z};
        return m;
    }

    rlpr::PhysRecord ballPhys(const BallState& b, uint32_t frame) {
        rlpr::PhysRecord p = {};
        p.physics_frame = frame;
        p.pos = toVec3(b.pos);
        p.rot = toMat3(b.rotMat);
        p.lin_vel = toVec3(b.vel);
        p.ang_vel = toVec3(b.angVel);
        p.has_world_contact = false;
        p.num_impulse_records = 0;
        return p;
    }

    rlpr::CarRecord carRecord(const Player& pl, uint32_t frame) {
        rlpr::CarRecord c = {};
        c.phys.physics_frame = frame;
        c.phys.pos = toVec3(pl.pos);
        c.phys.rot = toMat3(pl.rotMat);
        c.phys.lin_vel = toVec3(pl.vel);
        c.phys.ang_vel = toVec3(pl.angVel);
        c.phys.has_world_contact = pl.worldContact.hasContact;
        c.phys.world_contact_normal = toVec3(pl.worldContact.contactNormal);
        c.phys.num_impulse_records = 0;

        c.is_on_ground = pl.isOnGround;
        c.is_jumping = pl.isJumping;
        c.is_flipping = pl.isFlipping;
        c.jump_time = pl.jumpTime;
        c.flip_time = pl.flipTime;
        c.has_jumped = pl.hasJumped;
        c.double_jumped_or_flipped = pl.hasDoubleJumped || pl.hasFlipped;
        c.has_flip = pl.HasFlipOrJump();
        c.flip_rel_torque = toVec3(pl.flipRelTorque);
        c.boost_amount = pl.boost;
        c.is_touching_ball = pl.ballTouchedTick;

        c.prev_controls.throttle = pl.prevAction.throttle;
        c.prev_controls.steer = pl.prevAction.steer;
        c.prev_controls.pitch = pl.prevAction.pitch;
        c.prev_controls.yaw = pl.prevAction.yaw;
        c.prev_controls.roll = pl.prevAction.roll;
        c.prev_controls.jump = pl.prevAction.jump > 0.5f;
        c.prev_controls.boost = pl.prevAction.boost > 0.5f;
        c.prev_controls.handbrake = pl.prevAction.handbrake > 0.5f;

        for (int i = 0; i < 4; ++i) {
            c.wheels[i].has_contact = pl.wheelsWithContact[i];
            if (pl.wheelsWithContact[i]) {
                c.wheels[i].contact_normal = {0.0f, 0.0f, 1.0f};
            }
        }
        return c;
    }

    template <typename T>
    void writePrefixed(std::ofstream& out, const T& s) {
        const uint32_t sz = static_cast<uint32_t>(sizeof(T));
        out.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
        out.write(reinterpret_cast<const char*>(&s), sizeof(T));
    }
}

bool ExportClipToRLPR(const std::string& path,
                      const std::vector<const GameState*>& ticks,
                      const rlpr::Vec3& hitbox_min_bt,
                      const rlpr::Vec3& hitbox_max_bt,
                      const RLPRExportOptions& options) {
    if (ticks.empty() || ticks.front() == nullptr) {
        std::cerr << "[RLPRExporter] Empty clip\n";
        return false;
    }

    size_t num_cars = 0;
    const GameState* widest = nullptr;
    for (const GameState* s : ticks) {
        if (s && s->players.size() > num_cars) { num_cars = s->players.size(); widest = s; }
    }
    if (num_cars == 0 || num_cars > RLPR_MAX_CARS) {
        std::cerr << "[RLPRExporter] Unsupported car count " << num_cars
                  << " (max " << RLPR_MAX_CARS << ")\n";
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "[RLPRExporter] Failed to open " << path << "\n";
        return false;
    }

    out.write(reinterpret_cast<const char*>(RLPR_MAGIC), 4);
    const uint8_t little_endian = 0;
    out.write(reinterpret_cast<const char*>(&little_endian), 1);
    out.write(reinterpret_cast<const char*>(&RLPR_VERSION), sizeof(RLPR_VERSION));

    rlpr::Vec3 hbmin = hitbox_min_bt;
    rlpr::Vec3 hbmax = hitbox_max_bt;
    const bool hitbox_unset = hbmin.x == 0 && hbmin.y == 0 && hbmin.z == 0 &&
                              hbmax.x == 0 && hbmax.y == 0 && hbmax.z == 0;
    if (hitbox_unset) {
        hbmin = {-0.9025f, -0.842f, 0.0534f};
        hbmax = {1.4577f, 0.842f, 0.7766f};
    }

    rlpr::RecordingInfoInjected info = {};
    info.num_cars = static_cast<uint32_t>(num_cars);
    for (size_t c = 0; c < 6; ++c) {
        info.cars[c].hitbox_rel_min_bt = hbmin;
        info.cars[c].hitbox_rel_max_bt = hbmax;
        info.cars[c].team = (c < num_cars)
                                ? static_cast<uint32_t>(widest->players[c].team == Team::ORANGE ? 1 : 0)
                                : 0;
        info.cars[c].is_local = 0;
    }
    writePrefixed(out, info);

    rlpr::RecordingMetadata metadata = {};
    metadata.source_tick_rate_hz = std::max(1u, options.sourceTickRateHz);
    metadata.frame_interval_ticks = std::max(1u, options.frameIntervalTicks);
    metadata.frame_interval_seconds = static_cast<float>(metadata.frame_interval_ticks) /
                                      static_cast<float>(std::max(1u, metadata.source_tick_rate_hz));
    writePrefixed(out, metadata);

    const uint32_t num_ticks = static_cast<uint32_t>(ticks.size());
    out.write(reinterpret_cast<const char*>(&num_ticks), sizeof(num_ticks));

    for (uint32_t t = 0; t < num_ticks; ++t) {
        const GameState* state = ticks[t];
        if (!state) {
            std::cerr << "[RLPRExporter] Tick " << t << " is null; aborting\n";
            return false;
        }
        for (size_t c = 0; c < state->players.size() && c < num_cars; ++c) {
            writePrefixed(out, carRecord(state->players[c], t));
        }
        writePrefixed(out, ballPhys(state->ball, t));
    }

    return static_cast<bool>(out);
}
#endif

namespace {

    inline Vec fromVec3(const rlpr::Vec3& v) { return {v.x, v.y, v.z}; }

    inline RotMat fromMat3(const rlpr::Mat3& m) {
        RotMat r;
        r.forward = {m.rows[0].x, m.rows[1].x, m.rows[2].x};
        r.right   = {m.rows[0].y, m.rows[1].y, m.rows[2].y};
        r.up      = {m.rows[0].z, m.rows[1].z, m.rows[2].z};
        return r;
    }

    struct Cursor {
        const std::vector<char>& d;
        size_t pos = 0;
        template <typename T>
        bool read(T& out) {
            if (pos + sizeof(T) > d.size()) return false;
            std::memcpy(&out, d.data() + pos, sizeof(T));
            pos += sizeof(T);
            return true;
        }
        bool peekSize(uint32_t& out) const {
            if (pos + sizeof(uint32_t) > d.size()) return false;
            std::memcpy(&out, d.data() + pos, sizeof(uint32_t));
            return true;
        }
        template <typename T>
        bool readPrefixed(T& out) {
            uint32_t sz = 0;
            if (!read(sz) || sz != sizeof(T)) return false;
            return read(out);
        }
    };

    void fillPlayer(Player& p, const rlpr::CarRecord& c, int index, Team team) {
        p.carId = index + 1;
        p.index = index;
        p.team = team;
        p.pos = fromVec3(c.phys.pos);
        p.rotMat = fromMat3(c.phys.rot);
        p.vel = fromVec3(c.phys.lin_vel);
        p.angVel = fromVec3(c.phys.ang_vel);
        p.worldContact.hasContact = c.phys.has_world_contact;
        p.worldContact.contactNormal = fromVec3(c.phys.world_contact_normal);
        p.isOnGround = c.is_on_ground;
        p.isJumping = c.is_jumping;
        p.isFlipping = c.is_flipping;
        p.jumpTime = c.jump_time;
        p.flipTime = c.flip_time;
        p.hasJumped = c.has_jumped;
        p.hasFlipped = c.double_jumped_or_flipped;
        p.hasDoubleJumped = false;
        p.airTimeSinceJump = (!c.is_on_ground && !c.has_flip && !c.double_jumped_or_flipped) ? 2.0f : 0.0f;
        p.flipRelTorque = fromVec3(c.flip_rel_torque);
        p.boost = c.boost_amount;
        p.ballTouchedStep = c.is_touching_ball;
        p.ballTouchedTick = c.is_touching_ball;
        p.prevAction.throttle = c.prev_controls.throttle;
        p.prevAction.steer = c.prev_controls.steer;
        p.prevAction.pitch = c.prev_controls.pitch;
        p.prevAction.yaw = c.prev_controls.yaw;
        p.prevAction.roll = c.prev_controls.roll;
        p.prevAction.jump = c.prev_controls.jump ? 1.0f : 0.0f;
        p.prevAction.boost = c.prev_controls.boost ? 1.0f : 0.0f;
        p.prevAction.handbrake = c.prev_controls.handbrake ? 1.0f : 0.0f;
        for (int i = 0; i < 4; ++i) p.wheelsWithContact[i] = c.wheels[i].has_contact;
    }
}

bool ImportClipFromRLPR(const std::string& path,
                        std::vector<GameState>& outTicks,
                        RLPRMetadata* metadata) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "[RLPRExporter] Failed to open " << path << "\n";
        return false;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Cursor cur{data};

    uint8_t magic[4];
    for (int i = 0; i < 4; ++i)
        if (!cur.read(magic[i]) || magic[i] != RLPR_MAGIC[i]) return false;
    uint8_t bigEndian = 0;
    uint32_t version = 0;
    if (!cur.read(bigEndian) || !cur.read(version) || version < 2 || version > RLPR_VERSION) return false;
    RLPRMetadata parsedMetadata;
    parsedMetadata.version = version;

    uint32_t numCars = 0;
    Team teams[RLPR_MAX_CARS];
    for (size_t i = 0; i < RLPR_MAX_CARS; ++i) teams[i] = (i == 0) ? Team::BLUE : Team::ORANGE;
    {
        uint32_t peeked = 0;
        if (!cur.peekSize(peeked)) return false;
        if (peeked == sizeof(rlpr::RecordingInfoInjected)) {
            rlpr::RecordingInfoInjected inj{};
            if (!cur.readPrefixed(inj)) return false;
            numCars = inj.num_cars;
            for (size_t i = 0; i < RLPR_MAX_CARS; ++i) teams[i] = inj.cars[i].team ? Team::ORANGE : Team::BLUE;
        } else {
            rlpr::RecordingInfo info{};
            if (!cur.readPrefixed(info)) return false;
            numCars = info.num_cars;
        }
    }
    if (numCars == 0 || numCars > RLPR_MAX_CARS) return false;

    if (version >= 4) {
        rlpr::RecordingMetadata serializedMetadata{};
        if (!cur.readPrefixed(serializedMetadata)) return false;
        parsedMetadata.sourceTickRateHz = serializedMetadata.source_tick_rate_hz;
        parsedMetadata.frameIntervalTicks = serializedMetadata.frame_interval_ticks;
        parsedMetadata.frameIntervalSeconds = serializedMetadata.frame_interval_seconds;
        if (parsedMetadata.sourceTickRateHz == 0 || parsedMetadata.frameIntervalTicks == 0)
            return false;
        if (!(parsedMetadata.frameIntervalSeconds > 0.0f)) {
            parsedMetadata.frameIntervalSeconds =
                static_cast<float>(parsedMetadata.frameIntervalTicks) /
                static_cast<float>(parsedMetadata.sourceTickRateHz);
        }
    }
    if (metadata) *metadata = parsedMetadata;

    uint32_t numTicks = 0;
    if (!cur.read(numTicks)) return false;
    outTicks.clear();
    outTicks.reserve(numTicks);

    for (uint32_t t = 0; t < numTicks; ++t) {
        GameState gs;
        gs.deltaTime = parsedMetadata.frameIntervalSeconds;
        gs.lastTickCount = static_cast<uint64_t>(t) * parsedMetadata.frameIntervalTicks;
        for (;;) {
            uint32_t sz = 0;
            if (!cur.peekSize(sz)) return false;
            if (sz == sizeof(rlpr::CarRecord)) {
                rlpr::CarRecord c{};
                if (!cur.readPrefixed(c) || gs.players.size() >= numCars) return false;
                const int idx = static_cast<int>(gs.players.size());
                gs.players.emplace_back();
                fillPlayer(gs.players.back(), c, idx, teams[idx]);
            } else if (sz == sizeof(rlpr::PhysRecord)) {
                rlpr::PhysRecord b{};
                if (!cur.readPrefixed(b)) return false;
                gs.ball.pos = fromVec3(b.pos);
                gs.ball.rotMat = fromMat3(b.rot);
                gs.ball.vel = fromVec3(b.lin_vel);
                gs.ball.angVel = fromVec3(b.ang_vel);
                break;
            } else {
                return false;
            }
        }
        for (const auto& p : gs.players)
            if (p.ballTouchedTick) { gs.lastTouchCarID = p.carId; break; }
        outTicks.push_back(std::move(gs));
    }
    return true;
}

}
