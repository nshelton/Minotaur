#include "plotters/MotionPlanner.h"

#include <algorithm>
#include <cmath>

namespace {

static inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

static inline int roundToInt(float v) {
    return static_cast<int>(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

// Kinematics helpers
static inline float vFinal_Vi_A_Dx(float vi, float a, float dx) {
    float term = vi * vi + 2.0f * a * dx;
    return term > 0.0f ? std::sqrt(term) : 0.0f;
}

static inline float vInitial_VF_A_Dx_safe(float vf, float accel, float dx) {
    // Using v_i^2 = v_f^2 - 2 a dx (with accel >= 0 for magnitudes)
    float term = vf * vf - 2.0f * accel * dx;
    return term > 0.0f ? std::sqrt(term) : 0.0f;
}

} // namespace

std::vector<MoveSlice> planPath(const PlannerSettings &s,
                                const std::vector<Vec2> &pointsPageMm,
                                bool penUp,
                                const Vec2 &startMm)
{
    std::vector<MoveSlice> moves;
    if (pointsPageMm.size() < 2) return moves;

    // Build trimmed vertices and unit direction vectors
    const float minDist = s.minSegmentMm;
    std::vector<Vec2> verts;
    verts.reserve(pointsPageMm.size());
    verts.push_back(pointsPageMm.front());
    for (size_t i = 1; i < pointsPageMm.size(); ++i) {
        const Vec2 &last = verts.back();
        const Vec2 &v = pointsPageMm[i];
        float dx = v.x - last.x;
        float dy = v.y - last.y;
        float d = std::hypot(dx, dy);
        if (d >= minDist) verts.push_back(v);
    }
    if (verts.size() < 2) return moves;

    const float speedLimit = penUp ? s.speedPenUpMmPerS : s.speedPenDownMmPerS;
    const float accel = penUp ? s.accelPenUpMmPerS2 : s.accelPenDownMmPerS2;
    // Junction deviation model: treat s.cornering as jd in mm
    const float jd = std::max(0.0f, s.cornering);
    const float vJunctionFloor = (static_cast<float>(s.junctionSpeedFloorPercent) / 100.0f) * speedLimit;
    const float timeSlice = static_cast<float>(s.timeSliceMs) / 1000.0f;

    // Segment lengths and unit vectors
    const size_t segCount = verts.size() - 1;
    std::vector<float> segLen(segCount);
    std::vector<Vec2> segDir(segCount);
    for (size_t i = 0; i < segCount; ++i) {
        float dx = verts[i + 1].x - verts[i].x;
        float dy = verts[i + 1].y - verts[i].y;
        float d = std::hypot(dx, dy);
        segLen[i] = d;
        if (d > 0.0f) segDir[i] = Vec2(dx / d, dy / d); else segDir[i] = Vec2(1.0f, 0.0f);
    }

    // Precompute junction caps based on junction deviation
    const size_t N = verts.size();
    std::vector<float> vJunctionMax(N, speedLimit);
    for (size_t i = 1; i + 1 < N; ++i) {
        size_t idxIn = i - 1;
        size_t idxOut = i;
        float dot = segDir[idxIn].x * segDir[idxOut].x + segDir[idxIn].y * segDir[idxOut].y;
        dot = clampf(dot, -1.0f, 1.0f);
        float sinHalf = std::sqrt(std::max(0.0f, 0.5f * (1.0f - dot)));
        float vMax = speedLimit;
        if (sinHalf > 1e-6f && jd > 0.0f) {
            float R = jd * (1.0f + sinHalf) / (1.0f - sinHalf);
            vMax = std::sqrt(std::max(0.0f, accel * R));
        }
        // Apply optional floor
        vMax = std::max(vMax, vJunctionFloor);
        vJunctionMax[i] = clampf(vMax, 0.0f, speedLimit);
    }

    // Forward pass (pure kinematics + junction caps)
    std::vector<float> vAtVertex(N, 0.0f);
    vAtVertex[0] = 0.0f;
    for (size_t i = 1; i < N; ++i) {
        float vFromAccel = std::sqrt(std::max(0.0f, vAtVertex[i - 1] * vAtVertex[i - 1] + 2.0f * accel * segLen[i - 1]));
        float cap = (i < N - 1) ? std::min(speedLimit, vJunctionMax[i]) : 0.0f;
        vAtVertex[i] = std::min(vFromAccel, cap);
    }

    // Backward pass
    for (size_t i = N - 1; i > 0; --i) {
        float vFromDecel = std::sqrt(std::max(0.0f, vAtVertex[i] * vAtVertex[i] + 2.0f * accel * segLen[i - 1]));
        float cap = (i - 1 > 0) ? std::min(speedLimit, vJunctionMax[i - 1]) : 0.0f;
        vAtVertex[i - 1] = std::min(vAtVertex[i - 1], std::min(vFromDecel, cap));
    }

    // Emit slices per segment
    Vec2 current = startMm;
    int prevA = 0, prevB = 0;
    int cumA = 0, cumB = 0;
    int cumT = 0;

    for (size_t i = 0; i < segCount; ++i) {
        const Vec2 target = verts[i + 1];
        const float vi = clampf(vAtVertex[i], 0.0f, speedLimit);
        const float vf = clampf(vAtVertex[i + 1], 0.0f, speedLimit);

        // Segment vector
        float dx = target.x - current.x;
        float dy = target.y - current.y;
        float segLenIn = std::hypot(dx, dy);
        if (segLenIn <= 0.0f) continue;

        // Map to CoreXY native distances (mm)
        float a_mm = dx + dy; // A axis (dx+dy)
        float b_mm = dx - dy; // B axis (dx-dy)
        // Steps per axis
        int a_steps_total = roundToInt(s.stepsPerMm * a_mm);
        int b_steps_total = roundToInt(s.stepsPerMm * b_mm);
        if (std::abs(a_steps_total) < 1 && std::abs(b_steps_total) < 1) {
            current = target;
            continue;
        }

        // Rounded mm accomplished by those steps
        float a_mm_round = static_cast<float>(a_steps_total) / static_cast<float>(s.stepsPerMm);
        float b_mm_round = static_cast<float>(b_steps_total) / static_cast<float>(s.stepsPerMm);
        float dx_round = 0.5f * (a_mm_round + b_mm_round);
        float dy_round = 0.5f * (a_mm_round - b_mm_round);
        
        float segLenRound = std::hypot(dx_round, dy_round);
        if (segLenRound <= 0.0f) segLenRound = segLenIn;

        // Trapezoidal/triangular time slicing
        float tAccelMax = (speedLimit - vi) / std::max(1e-6f, accel);
        float tDecelMax = (speedLimit - vf) / std::max(1e-6f, accel);
        float accelDistMax = vi * tAccelMax + 0.5f * accel * tAccelMax * tAccelMax;
        float decelDistMax = vf * tDecelMax + 0.5f * accel * tDecelMax * tDecelMax;
        float timeElapsed = 0.0f;
        float pos = 0.0f;
        float vel = vi;

        std::vector<int> durationArray;
        std::vector<float> distArray;

        auto pushSlice = [&](float t, float p) {
            durationArray.push_back(std::max(1, roundToInt(t * 1000.0f)));
            distArray.push_back(p);
        };

        if ((segLenRound > (accelDistMax + decelDistMax + timeSlice * speedLimit))) {
            // Accel phase
            int intervalsUp = static_cast<int>(std::floor(tAccelMax / timeSlice));
            if (intervalsUp > 0) {
                float timePer = tAccelMax / static_cast<float>(intervalsUp);
                float velStep = (speedLimit - vi) / (static_cast<float>(intervalsUp) + 1.0f);
                for (int k = 0; k < intervalsUp; ++k) {
                    vel += velStep;
                    timeElapsed += timePer;
                    pos += vel * timePer;
                    pushSlice(timeElapsed, pos);
                }
            }
            // Cruise phase
            float coastDist = segLenRound - (accelDistMax + decelDistMax);
            if (coastDist > (timeSlice * speedLimit)) {
                vel = speedLimit;
                float ct = coastDist / vel;
                float cruiseInterval = 20.0f * timeSlice;
                while (ct > cruiseInterval) {
                    ct -= cruiseInterval;
                    timeElapsed += cruiseInterval;
                    pos += vel * cruiseInterval;
                    pushSlice(timeElapsed, pos);
                }
                timeElapsed += ct;
                pos += vel * ct;
                pushSlice(timeElapsed, pos);
            }
            // Decel phase
            int intervalsDown = static_cast<int>(std::floor(tDecelMax / timeSlice));
            if (intervalsDown > 0) {
                float timePer = tDecelMax / static_cast<float>(intervalsDown);
                float velStep = (speedLimit - vf) / (static_cast<float>(intervalsDown) + 1.0f);
                for (int k = 0; k < intervalsDown; ++k) {
                    vel -= velStep;
                    timeElapsed += timePer;
                    pos += vel * timePer;
                    pushSlice(timeElapsed, pos);
                }
            }
        } else {
            // Triangle/linear fallback
            float accelLocal = accel;
            // analytical minimal profile ensuring vf at end
            float ta = accelLocal > 0 ? (std::sqrt(std::max(0.0f, 2 * vi * vi + 2 * vf * vf + 4 * accelLocal * segLenRound)) - 2 * vi) / (2 * accelLocal) : 0.0f;
            float vmax = vi + accelLocal * ta;
            int intervalsUp = static_cast<int>(std::floor(ta / timeSlice));
            if (intervalsUp > 0) {
                float timePer = ta / static_cast<float>(intervalsUp);
                float velStep = (vmax - vi) / (static_cast<float>(intervalsUp) + 1.0f);
                for (int k = 0; k < intervalsUp; ++k) {
                    vel += velStep;
                    timeElapsed += timePer;
                    pos += vel * timePer;
                    pushSlice(timeElapsed, pos);
                }
            }
            float td = accelLocal > 0 ? ta - (vf - vi) / accelLocal : 0.0f;
            int intervalsDown = static_cast<int>(std::floor(td / timeSlice));
            if (intervalsDown > 0) {
                float timePer = td / static_cast<float>(intervalsDown);
                float velStep = (vmax - vf) / (static_cast<float>(intervalsDown) + 1.0f);
                for (int k = 0; k < intervalsDown; ++k) {
                    vel -= velStep;
                    timeElapsed += timePer;
                    pos += vel * timePer;
                    pushSlice(timeElapsed, pos);
                }
            }
            if (durationArray.empty()) {
                // constant fallback
                float v = std::max(vi, std::max(vf, speedLimit * 0.1f));
                float t = segLenRound / std::max(1e-6f, v);
                timeElapsed = t;
                pos = segLenRound;
                pushSlice(timeElapsed, pos);
            }
        }

        // Map to per-slice steps and dtMs
        int prev1 = 0, prev2 = 0;
        int prevT = 0;
        for (size_t si = 0; si < distArray.size(); ++si) {
            float frac = pos > 0.0f ? distArray[si] / pos : 1.0f;
            int dest1 = roundToInt(frac * static_cast<float>(a_steps_total));
            int dest2 = roundToInt(frac * static_cast<float>(b_steps_total));
            int sA = dest1 - prev1;
            int sB = dest2 - prev2;
            int dt = durationArray[si] - prevT;
            if (dt < 1) dt = 1;

            // Overspeed guard (maxStepRatePerAxis in steps/second)
            auto stepsAllowed = [&](int dtMs) {
                return static_cast<int>(std::floor(static_cast<float>(s.maxStepRatePerAxis) * (static_cast<float>(dtMs) / 1000.0f)));
            };
            int maxSteps = stepsAllowed(dt);
            while ((std::abs(sA) > maxSteps) || (std::abs(sB) > maxSteps)) {
                dt += 1;
                maxSteps = stepsAllowed(dt);
            }

            prev1 = dest1;
            prev2 = dest2;
            prevT = durationArray[si];

            if (sA != 0 || sB != 0) {
                // Map native B to controller axis: axis2 = -B
                int axis1 = sA;
                int axis2 = -sB;
                cumA += axis1;
                cumB += axis2;
                cumT += dt;
                moves.push_back(MoveSlice{axis1, axis2, dt});
            }
        }

        current = target;
    }

    (void)prevA; (void)prevB; (void)cumA; (void)cumB; (void)cumT;
    return moves;
}


