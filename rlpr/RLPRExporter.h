#pragma once
#include "RLGymCPP/Gamestates/GameState.h"
#include <cstdint>
#include <string>
#include <vector>

namespace RLGC {

    /**
     * Reader and writer for `.rlpr` recordings. The binary structs below are the on-disk layout and
     * must not change; the static_asserts guard against padding drift.
     *
     * Source states must be full precision. Up to 6 cars; readers accept a tick carrying fewer
     * CarRecords than the header's `num_cars` (a demoed car has no record that tick).
     */
    namespace rlpr {

        struct Vec3 { float x, y, z; };
        static_assert(sizeof(Vec3) == 12);

        struct Mat3 { Vec3 rows[3]; };
        static_assert(sizeof(Mat3) == 36);

        struct WheelRecord {
            float susp_length;
            float susp_rel_vel;
            bool  has_contact;
            char  _pad0[3];
            Vec3  contact_normal;
            float steer_amount;
            float engine_force;
            float brake;
            float lat_friction;
            float long_friction;
            float extra_pushback;
        };
        static_assert(sizeof(WheelRecord) == 48);

        struct ControlsRecord {
            float throttle, steer, pitch, yaw, roll;
            bool  jump, boost, handbrake;
            char  _pad0[1];
        };
        static_assert(sizeof(ControlsRecord) == 24);

        enum class ImpulseType : uint8_t {
            WheelsSuspension = 0, WheelsFriction, StickyForce, Jump, DoubleJump,
            DodgeImpulse, DodgeTorque, AirControl, Boost
        };

        struct ImpulseRecord {
            Vec3 lin_impulse;
            Vec3 ang_impulse;
            ImpulseType impulse_type;
            bool is_accum;
            char _pad0[2];
        };
        static_assert(sizeof(ImpulseRecord) == 28);

        struct PhysRecord {
            uint32_t physics_frame;
            Vec3     pos;
            Mat3     rot;
            Vec3     lin_vel;
            Vec3     ang_vel;
            bool     has_world_contact;
            char     _pad0[3];
            Vec3     world_contact_point;
            Vec3     world_contact_normal;
            ImpulseRecord impulse_records_data[8];
            uint32_t num_impulse_records;
        };
        static_assert(sizeof(PhysRecord) == 332);

        struct CarRecord {
            PhysRecord phys;
            bool  is_on_ground;
            bool  is_jumping;
            bool  is_flipping;
            char  _pad0[1];
            float jump_time;
            float flip_time;
            bool  has_jumped;
            bool  double_jumped_or_flipped;
            bool  has_flip;
            char  _pad1[1];
            Vec3  flip_rel_torque;
            float boost_amount;
            bool  is_touching_ball;
            char  _pad2[3];
            ControlsRecord prev_controls;
            WheelRecord wheels[4];
        };
        static_assert(sizeof(CarRecord) == 584);

        struct RecordingInfo {
            uint32_t num_cars;
            Vec3     hitbox_rel_min_bt;
            Vec3     hitbox_rel_max_bt;
        };
        static_assert(sizeof(RecordingInfo) == 28);

        struct InjectedCarInfo {
            Vec3     hitbox_rel_min_bt;
            Vec3     hitbox_rel_max_bt;
            uint32_t team;
            uint32_t is_local;
        };
        static_assert(sizeof(InjectedCarInfo) == 32);

        /// The 196-byte header dialect: six fixed car slots carrying per-car hitbox extents and
        /// team. Readers tell it apart from the 28-byte header by its serialized size, so both
        /// dialects coexist under the same magic. Required for anything past 1v1, because the
        /// 28-byte header cannot express per-car team.
        struct RecordingInfoInjected {
            uint32_t        num_cars;
            InjectedCarInfo cars[6];
        };
        static_assert(sizeof(RecordingInfoInjected) == 196);

        struct RecordingMetadata {
            uint32_t source_tick_rate_hz;
            uint32_t frame_interval_ticks;
            float    frame_interval_seconds;
            uint32_t reserved;
        };
        static_assert(sizeof(RecordingMetadata) == 16);

    }

    struct RLPRExportOptions {
        uint32_t sourceTickRateHz = 120;
        uint32_t frameIntervalTicks = 1;
    };

    struct RLPRMetadata {
        uint32_t version = 0;
        uint32_t sourceTickRateHz = 120;
        uint32_t frameIntervalTicks = 8;
        float frameIntervalSeconds = 8.0f / 120.0f;
    };

    /// Writes a full-precision clip (one entry per recorded tick) to `path` in `.rlpr` v3 format.
    /// `hitbox_min_bt`/`hitbox_max_bt` are the car hitbox extents in Bullet units (Octane defaults
    /// are used if left at zero). Returns false on I/O error or if any state has >6 cars.
    /// Replay-derived states lack per-wheel and impulse internals; those are written as neutral
    /// defaults (RocketSim recomputes them on step) — the exported kinematics (pos/rot/vel/angVel +
    /// car flags + controls) are the calibration ground truth.
    bool ExportClipToRLPR(const std::string& path,
                          const std::vector<const GameState*>& ticks,
                          const rlpr::Vec3& hitbox_min_bt = {0, 0, 0},
                          const rlpr::Vec3& hitbox_max_bt = {0, 0, 0},
                          const RLPRExportOptions& options = {});

    /// Reads a `.rlpr` v2/v3/v4 recording back into one GameState per tick (inverse of
    /// ExportClipToRLPR; accepts both header dialects and per-tick car-count variance).
    /// Only kinematics, car flags, controls and contact are restored; prev/userInfo are null.
    bool ImportClipFromRLPR(const std::string& path,
                            std::vector<GameState>& outTicks,
                            RLPRMetadata* metadata = nullptr);

}
