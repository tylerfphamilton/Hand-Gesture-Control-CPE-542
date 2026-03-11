// gesture_control.h
#pragma once
#include <vector>
#include <chrono>
#include "hailo_objects.hpp"

enum class Gesture {
    NONE,
    ARMS_UP,        // both wrists above shoulders -> volume up
    ARMS_DOWN,      // both wrists below shoulders      -> volume down
    TREBLE_UP,      // shoulder=elbow level, wrist 90° above elbow
    BASS_UP,        // shoulder=elbow level, wrist 90° below elbow
    REVERB_ON       // T-pose: wrist/elbow/shoulder all level
};

// Keypoint indices (YOLOv8 pose, COCO order)
// 5=L_shoulder, 6=R_shoulder, 7=L_elbow, 8=R_elbow, 9=L_wrist, 10=R_wrist
struct ArmKeypoints {
    float lsX, lsY;   // left shoulder
    float rsX, rsY;   // right shoulder
    float leX, leY;   // left elbow
    float reX, reY;   // right elbow
    float lwX, lwY;   // left wrist
    float rwX, rwY;   // right wrist
    bool  valid = false;
};

inline ArmKeypoints extractArms(const std::vector<HailoPoint>& pts, int fw, int fh) {
    ArmKeypoints k;
    // need all 6 arm points with good confidence
    for (int i : {5,6,7,8,9,10}) {
        if (i >= (int)pts.size() || pts[i].confidence() < 0.5f) return k;
    }
    k.lsX = pts[5].x()*fw;  k.lsY = pts[5].y()*fh;
    k.rsX = pts[6].x()*fw;  k.rsY = pts[6].y()*fh;
    k.leX = pts[7].x()*fw;  k.leY = pts[7].y()*fh;
    k.reX = pts[8].x()*fw;  k.reY = pts[8].y()*fh;
    k.lwX = pts[9].x()*fw;  k.lwY = pts[9].y()*fh;
    k.rwX = pts[10].x()*fw; k.rwY = pts[10].y()*fh;
    k.valid = true;
    return k;
}

// threshold in pixels - tune these for your camera resolution
constexpr float LEVEL_THRESH  = 30.0f;  // "same level" tolerance
constexpr float PERP_THRESH   = 40.0f;  // "90 degrees" tolerance

inline Gesture classifyGesture(const ArmKeypoints& k, int /*fw*/, int /*fh*/) {
    if (!k.valid) return Gesture::NONE;

    float shoulderY = (k.lsY + k.rsY) / 2.0f;
    float elbowY    = (k.leY + k.reY) / 2.0f;
    float wristY    = (k.lwY + k.rwY) / 2.0f;

    // ── T-pose: all three joints at same height ──────────────────────────
    bool shoulderElbowLevel = std::abs(shoulderY - elbowY) < LEVEL_THRESH;
    bool elbowWristLevel    = std::abs(elbowY    - wristY) < LEVEL_THRESH;
    if (shoulderElbowLevel && elbowWristLevel)
        return Gesture::REVERB_ON;

    // ── Treble: shoulder=elbow level, wrists 90° above elbow ─────────────
    // "wrist above elbow" = wristY < elbowY in image coords (Y flips)
    bool seLevel   = std::abs(shoulderY - elbowY) < LEVEL_THRESH;
    bool wristUp   = (elbowY - wristY) > PERP_THRESH;  // wrist clearly above
    bool wristDown = (wristY - elbowY) > PERP_THRESH;  // wrist clearly below

    if (seLevel && wristUp)   return Gesture::TREBLE_UP;
    if (seLevel && wristDown) return Gesture::BASS_UP;

    // ── Arms up/down (use shoulder as reference) ──────────────────────────
    bool bothWristsAboveShoulder = (k.lwY < k.lsY - LEVEL_THRESH) &&
                                   (k.rwY < k.rsY - LEVEL_THRESH);
    bool bothWristsBelowShoulder = (k.lwY > k.lsY + LEVEL_THRESH) &&
                                   (k.rwY > k.rsY + LEVEL_THRESH);

    if (bothWristsAboveShoulder) return Gesture::ARMS_UP;
    if (bothWristsBelowShoulder) return Gesture::ARMS_DOWN;

    return Gesture::NONE;
}