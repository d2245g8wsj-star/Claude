#pragma once

#include <array>
#include <cmath>

/*
    The equaliser as the panel describes it.

    Everything in here is analogue: transfer functions in s, frequencies in
    rad/s. Turning those into filters at a sample rate is MatchedDesign's job;
    this file only says what each switch position means.

    Three bands and a high pass per channel, all with Q = 0.5:

      LOW   28 … 255 Hz,    shelf or peak
      MID   400 Hz … 4.8 kHz, peak
      HIGH  4 … 25 kHz,     shelf or peak
      HPF   20 … 45 Hz, or 100 … 225 Hz with x5, 12 dB/octave

    Every gain control, the output level included, is the same stepped
    switch: 0, ±0.5, ±1, ±2, ±3, ±4 dB.

    Each band is written so that a cut is the exact reciprocal of the boost of
    the same size: the same poles and zeros, swapped. That is what makes a
    Baxandall style band sound like one, and it means the plugin can pass a
    flat panel through untouched.
*/

namespace tide::bands
{

/** b0 + b1 s + b2 s^2 over a0 + a1 s + a2 s^2. */
struct Analog
{
    std::array<double, 3> b { 1.0, 0.0, 0.0 };
    std::array<double, 3> a { 1.0, 0.0, 0.0 };
};

inline constexpr double pi = 3.14159265358979323846;
inline constexpr double q  = 0.5;     ///< every band and the high pass

inline constexpr int numFrequencies = 6;
inline constexpr int numGainSteps   = 11;
inline constexpr int centreStep     = 5;

inline constexpr std::array<double, numFrequencies> lowHz  {   28.0,   48.0,   72.0,  132.0,  195.0,   255.0 };
inline constexpr std::array<double, numFrequencies> midHz  {  400.0,  630.0,  870.0, 1300.0, 2400.0,  4800.0 };
inline constexpr std::array<double, numFrequencies> highHz { 4000.0, 7200.0, 12000.0, 16000.0, 20000.0, 25000.0 };
inline constexpr std::array<double, numFrequencies> hpfHz  {   20.0,   25.0,   30.0,   35.0,   40.0,    45.0 };
inline constexpr double hpfMultiplier = 5.0;

inline constexpr std::array<const char*, numFrequencies> lowNames  { "28 Hz", "48 Hz", "72 Hz", "132 Hz", "195 Hz", "255 Hz" };
inline constexpr std::array<const char*, numFrequencies> midNames  { "400 Hz", "630 Hz", "870 Hz", "1.3 kHz", "2.4 kHz", "4.8 kHz" };
inline constexpr std::array<const char*, numFrequencies> highNames { "4 kHz", "7.2 kHz", "12 kHz", "16 kHz", "20 kHz", "25 kHz" };
inline constexpr std::array<const char*, numFrequencies> hpfNames  { "20 / 100 Hz", "25 / 125 Hz", "30 / 150 Hz",
                                                                     "35 / 175 Hz", "40 / 200 Hz", "45 / 225 Hz" };

/** The stepped gain switch, left to right. Not equal steps: half a dB either
    side of the centre for fine moves, then whole dB. */
inline constexpr std::array<double, numGainSteps> gainDb {
    -4.0, -3.0, -2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0, 3.0, 4.0
};

inline constexpr std::array<const char*, numGainSteps> gainNames {
    "-4 dB", "-3 dB", "-2 dB", "-1 dB", "-0.5 dB", "0 dB", "+0.5 dB", "+1 dB", "+2 dB", "+3 dB", "+4 dB"
};

inline double omega (double hz) noexcept { return 2.0 * pi * hz; }

/** Gain at the centre is exactly gainDb: A^2 over the band, with A = 10^(dB/40). */
inline double amplitude (double db) noexcept { return std::pow (10.0, db / 40.0); }

/** Constant Q peak. At the centre frequency the gain is A^2; at A < 1 it is
    the boost of the same size turned upside down. */
inline Analog peak (double w0, double db) noexcept
{
    const auto a = amplitude (db);

    Analog h;
    h.b = { w0 * w0, w0 * a / q, 1.0 };
    h.a = { w0 * w0, w0 / (a * q), 1.0 };
    return h;
}

/** Second order shelf. The legend is the middle of the slope: half the
    shelf's dB at the legend frequency, the full amount well below it. */
inline Analog lowShelf (double w0, double db) noexcept
{
    const auto a  = amplitude (db);
    const auto sa = std::sqrt (a);

    Analog h;
    h.b = { a * a * w0 * w0, a * sa * w0 / q, a };
    h.a = { w0 * w0,         sa * w0 / q,     a };
    return h;
}

inline Analog highShelf (double w0, double db) noexcept
{
    const auto a  = amplitude (db);
    const auto sa = std::sqrt (a);

    Analog h;
    h.b = { a * w0 * w0, a * sa * w0 / q, a * a };
    h.a = { a * w0 * w0, sa * w0 / q,     1.0 };
    return h;
}

/** 12 dB/octave at Q = 0.5: two coincident real poles, so no bump, and the
    legend is where it is 6 dB down. */
inline Analog highPass (double w0) noexcept
{
    Analog h;
    h.b = { 0.0, 0.0, 1.0 };
    h.a = { w0 * w0, w0 / q, 1.0 };
    return h;
}

inline double hpfOmega (int position, bool times5) noexcept
{
    return omega (hpfHz[(size_t) position] * (times5 ? hpfMultiplier : 1.0));
}

} // namespace tide::bands
