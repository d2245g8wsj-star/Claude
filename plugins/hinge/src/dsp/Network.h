#pragma once

#include <array>
#include <cmath>
#include <numeric>

/*
    The equaliser as a circuit.

    Everything in here is analogue: resistors in ohms, capacitors in farads,
    and transfer functions in s. Turning those into filters at a sample rate is
    MatchedDesign's job; this file only says what the hardware would do.

    The shelves are a Baxandall network. Each one is a stepped attenuator whose
    two halves either side of the wiper set the curve, and a switched capacitor
    that sets where it sits. Swapping the two halves mirrors the curve, which
    is why a cut is the exact inverse of the boost of the same size, and why
    the centre detent is flat to the last bit rather than nearly flat.
*/

namespace hinge::network
{

/** b0 + b1 s + b2 s^2 over a0 + a1 s + a2 s^2. */
struct Analog
{
    std::array<double, 3> b { 1.0, 0.0, 0.0 };
    std::array<double, 3> a { 1.0, 0.0, 0.0 };
};

// ---------------------------------------------------------------------------
// The level attenuators: twenty resistors between twenty one detents, wound
// so each detent is half a dB at the shelf's own frequency. Not equal steps:
// the middle of the travel needs more resistance per half dB than the ends.

inline constexpr std::array<double, 20> levelSegments {
    232.0, 232.0, 232.0, 249.0, 249.0, 270.0, 270.0, 280.0, 280.0, 287.0,
    287.0, 280.0, 280.0, 270.0, 270.0, 249.0, 249.0, 232.0, 232.0, 232.0
};

inline constexpr int    levelDetents = (int) levelSegments.size() + 1;
inline constexpr double levelCentre  = 10.0;
inline constexpr double levelDbPerDetent = 0.5;
inline constexpr double maxLevelDb   = levelCentre * levelDbPerDetent;

inline constexpr double levelTotal = [] {
    double sum = 0.0;
    for (auto r : levelSegments)
        sum += r;
    return sum;
}();

/** Resistance either side of the wiper. Positions between detents are a
    wiper resting between two taps; the hardware cannot do that, the dynamic
    shelves need it. */
struct Wiper
{
    double toStart;
    double toEnd;
};

inline Wiper wiper (double position) noexcept
{
    position = std::fmin (std::fmax (position, 0.0), (double) levelSegments.size());

    double below = 0.0;
    size_t k = 0;

    for (; k < levelSegments.size() && position >= (double) (k + 1); ++k)
        below += levelSegments[k];

    if (k < levelSegments.size())
        below += (position - (double) k) * levelSegments[k];

    return { below, levelTotal - below };
}

/** Panel dB to wiper position, and back. Nominal: half a dB per detent. */
inline double positionForDb (double db) noexcept   { return levelCentre + db / levelDbPerDetent; }
inline double dbForPosition (double pos) noexcept  { return (pos - levelCentre) * levelDbPerDetent; }

// ---------------------------------------------------------------------------
// Low shelf. One half of the network, with the attenuator split a | b:
//
//     N(s) = R1 (a + R2) + R1 C (R2 (a + b) + a b) s + R1 R2 a b C^2 s^2
//     D(s) = a b + 2 R2 (a + b) + R1 R2 + R2 C (R1 (a + b) + 4 a b) s + R1 R2 a b C^2 s^2
//
// The shelf is that half over the same half with a and b swapped. Both share
// their s^2 term, so the shelf is flat above the band, and at the centre the
// two halves are identical, so the shelf is exactly flat everywhere.

inline constexpr double lowR1 = 15000.0;
inline constexpr double lowR2 =  5000.0;

inline Analog lowShelfHalf (double a, double b, double c) noexcept
{
    const auto ab = a * b;
    const auto s2 = lowR1 * lowR2 * ab * c * c;

    Analog h;
    h.b = { lowR1 * (a + lowR2), lowR1 * c * (lowR2 * (a + b) + ab), s2 };
    h.a = { ab + 2.0 * lowR2 * (a + b) + lowR1 * lowR2, lowR2 * c * (lowR1 * (a + b) + 4.0 * ab), s2 };
    return h;
}

// ---------------------------------------------------------------------------
// High shelf. Each half is first order, from 1 at DC to tN / tD above the band:
//
//     tN = C (R1 (a + R3) + a b + R3 (a + b + R3)) / (a + b + 2 R3)
//     tD = C (a b + R3 (a + b + R3)) / (a + b + 2 R3)

inline constexpr double highR1 = 15000.0;
inline constexpr double highR3 =  3320.0;

struct HighHalf
{
    double tN, tD;
};

inline HighHalf highShelfHalf (double a, double b, double c) noexcept
{
    const auto k      = a + b + 2.0 * highR3;
    const auto shared = a * b + highR3 * (a + b + highR3);

    return { c * (highR1 * (a + highR3) + shared) / k, c * shared / k };
}

// ---------------------------------------------------------------------------
// The switch positions. Each table is in knob order, clockwise.

inline constexpr int numPositions = 8;

/** LOW CUT: second order Butterworth high pass, w0 = k / C. 0 is out. */
inline constexpr std::array<double, 7> lowCutCapacitors {
    1.302e-6, 902.0e-9, 682.0e-9, 532.85118e-9, 447.5625e-9, 388.36111e-9, 299.135e-9
};
inline constexpr double lowCutK = 9.4280904e-5;          // sqrt (2) / 15k
inline constexpr double lowCutQ = 0.70710678118654752;

/** LOW SHELF. The legend is the bottom shoulder of the slope. */
inline constexpr std::array<double, 8> lowShelfCapacitors {
    277.0e-9, 244.0e-9, 209.0e-9, 176.0e-9, 157.0e-9, 124.0e-9, 89.0e-9, 56.0e-9
};

/** HIGH SHELF. The legend is the middle of the slope; full boost arrives
    about an octave above it. */
inline constexpr std::array<double, 8> highShelfCapacitors {
    1.0e-9, 2.5e-9, 3.7e-9, 5.2e-9, 6.6e-9, 8.1e-9, 9.3e-9, 10.8e-9
};

/** HIGH CUT: second order low pass, slightly under Butterworth. 0 is out. */
inline constexpr std::array<double, 7> highCutCapacitors {
    0.429e-9, 0.989e-9, 1.429e-9, 1.989e-9, 2.229e-9, 2.789e-9, 3.229e-9
};
inline constexpr double highCutK = 1.4512959e-4;
inline constexpr double highCutQ = 0.51193837 / (4990.0 * highCutK);

inline constexpr std::array<const char*, numPositions> lowCutNames {
    "Out", "12 Hz", "18 Hz", "24 Hz", "30 Hz", "36 Hz", "43 Hz", "54 Hz"
};
inline constexpr std::array<const char*, numPositions> lowShelfNames {
    "74 Hz", "84 Hz", "98 Hz", "116 Hz", "131 Hz", "166 Hz", "230 Hz", "361 Hz"
};
inline constexpr std::array<const char*, numPositions> highShelfNames {
    "18 kHz", "7.1 kHz", "4.8 kHz", "3.4 kHz", "2.5 kHz", "2.1 kHz", "1.8 kHz", "1.6 kHz"
};
inline constexpr std::array<const char*, numPositions> highCutNames {
    "Out", "70 kHz", "28 kHz", "18 kHz", "12.6 kHz", "11.1 kHz", "9 kHz", "7.5 kHz"
};

/** Legend frequency times capacitance, near enough constant across each
    switch: what the side chain uses to find a shelf's band between detents. */
inline constexpr double lowShelfK  = 2.05e-5;
inline constexpr double highShelfK = 1.73e-5;

/** The frequencies the legends name, for the side chain and the tests. */
inline constexpr std::array<double, numPositions> lowShelfHz  { 74, 84, 98, 116, 131, 166, 230, 361 };
inline constexpr std::array<double, numPositions> highShelfHz { 18000, 7100, 4800, 3400, 2500, 2100, 1800, 1600 };

inline Analog highPass (double w0, double q) noexcept
{
    Analog h;
    h.b = { 0.0, 0.0, 1.0 };
    h.a = { w0 * w0, w0 / q, 1.0 };
    return h;
}

inline Analog lowPass (double w0, double q) noexcept
{
    Analog h;
    h.b = { w0 * w0, 0.0, 0.0 };
    h.a = { w0 * w0, w0 / q, 1.0 };
    return h;
}

/** Natural frequency of a cut, in rad/s, for a switch position other than out. */
inline double lowCutOmega  (int position) noexcept { return lowCutK  / lowCutCapacitors [(size_t) (position - 1)]; }
inline double highCutOmega (int position) noexcept { return highCutK / highCutCapacitors[(size_t) (position - 1)]; }

// ---------------------------------------------------------------------------
// OUTPUT: the same twenty resistors as a gain pot against 5k either side,
// so the trim steps in a little over half a dB and spans +-6.2 dB.

inline constexpr double outputR = 5000.0;
inline constexpr int    outputSteps = 10;    // either side of 0

inline double outputGain (int step) noexcept
{
    const auto w = wiper (levelCentre + (double) step);
    return (outputR + w.toStart) / (outputR + w.toEnd);
}

} // namespace hinge::network
