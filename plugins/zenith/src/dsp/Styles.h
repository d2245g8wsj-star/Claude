#pragma once

#include <array>

namespace zenith
{

/** The eight limiting styles of the Modern engine.

    A style is not a preset of the visible controls: it sets how the work is
    divided between the leveller and the peak stage, and how the peak stage
    behaves inside the attack and release the controls ask for. The controls
    keep their meaning across every style.
*/
struct Style
{
    const char* name;
    const char* description;

    // Leveller.
    float levelRatio;        ///< 1 is off
    float levelHeadroomDb;
    float levelAttackMs;
    float levelReleaseMs;

    // Peak stage.
    float releaseScale;      ///< multiplies the Release control
    float attackShare;       ///< multiplies the Attack control's share of the lookahead
    float adaptive;          ///< 0...1
    float bassAware;         ///< 0...1
};

inline constexpr std::array<Style, 8> styles {{
    { "Clean",
      "Transparent. The leveller takes the body, the peak stage a long smooth ramp.",
      2.0f, 3.0f, 10.0f, 300.0f,   1.0f, 1.0f, 0.8f, 1.0f },

    { "Punch",
      "The leveller is slow to start, so drum fronts reach a short peak ramp intact.",
      1.6f, 5.0f, 30.0f, 150.0f,   0.5f, 0.45f, 0.5f, 0.6f },

    { "Open",
      "No leveller and a short ramp: the most dynamic, for material with headroom.",
      1.0f, 0.0f, 10.0f, 100.0f,   0.6f, 0.35f, 0.3f, 0.7f },

    { "Loud",
      "Fast leveller and quick release. Density first, at a price in grit.",
      3.0f, 1.5f, 3.0f, 80.0f,     0.35f, 0.6f, 0.2f, 0.3f },

    { "Balanced",
      "The all rounder. A medium leveller and a release that follows the music.",
      2.5f, 3.0f, 8.0f, 200.0f,    0.8f, 0.8f, 0.6f, 0.8f },

    { "Glue",
      "A slow, firm leveller doing most of the work, like a bus compressor.",
      4.0f, 1.0f, 15.0f, 400.0f,   1.2f, 1.0f, 0.9f, 1.0f },

    { "Safe",
      "No leveller; the longest ramp and slowest release. Hardest to make distort.",
      1.0f, 0.0f, 10.0f, 100.0f,   1.5f, 1.0f, 1.0f, 1.2f },

    { "Smooth",
      "A gentle leveller and a release that rides on the programme density.",
      2.0f, 2.0f, 5.0f, 250.0f,    0.7f, 0.9f, 1.0f, 1.0f },
}};

} // namespace zenith
