// Butterworth design and second-order-section filtering.
//
// The Python reference leans on scipy for `butter` and `sosfiltfilt`; this is
// the equivalent, including the steady-state initial conditions and odd
// padding that make forward-backward filtering behave at the signal edges.
#pragma once

#include <cstddef>
#include <vector>

namespace vocalai {

// One biquad: b0 b1 b2 a0 a1 a2, with a0 normalised to 1.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a0 = 1.0, a1 = 0.0, a2 = 0.0;
};

using Sos = std::vector<Biquad>;

enum class BandType { LowPass, HighPass, BandPass };

// `wn` is normalised frequency: 1.0 is Nyquist. BandPass takes {low, high}.
Sos butter(int order, const std::vector<double>& wn, BandType type);
Sos butter_lowpass(int order, double wn);
Sos butter_highpass(int order, double wn);
Sos butter_bandpass(int order, double low, double high);

// Single-pass filtering. `zi` may be null; when given it is updated in place.
std::vector<double> sosfilt(const Sos& sos, const std::vector<double>& x,
                            std::vector<double>* zi = nullptr);

// Steady-state initial conditions, matching scipy's `sosfilt_zi`.
std::vector<double> sosfilt_zi(const Sos& sos);

// Zero-phase forward-backward filtering, matching scipy's `sosfiltfilt`.
std::vector<double> sosfiltfilt(const Sos& sos, const std::vector<double>& x);

}  // namespace vocalai
