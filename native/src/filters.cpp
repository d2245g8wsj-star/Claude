#include "vocalai/filters.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

namespace vocalai {
namespace {

using cd = std::complex<double>;

struct Zpk {
    std::vector<cd> zeros;
    std::vector<cd> poles;
    double gain = 1.0;
};

// Analog Butterworth prototype: poles evenly spaced on the left unit semicircle.
Zpk butter_prototype(int order) {
    Zpk zpk;
    zpk.gain = 1.0;
    for (int i = -order + 1; i < order; i += 2) {
        const double angle = M_PI * static_cast<double>(i) / (2.0 * order);
        zpk.poles.push_back(-std::exp(cd(0.0, angle)));
    }
    return zpk;
}

int relative_degree(const Zpk& zpk) {
    return static_cast<int>(zpk.poles.size()) - static_cast<int>(zpk.zeros.size());
}

Zpk lp2lp(const Zpk& in, double wo) {
    Zpk out;
    const int degree = relative_degree(in);
    for (const auto& z : in.zeros) out.zeros.push_back(z * wo);
    for (const auto& p : in.poles) out.poles.push_back(p * wo);
    out.gain = in.gain * std::pow(wo, degree);
    return out;
}

Zpk lp2hp(const Zpk& in, double wo) {
    Zpk out;
    const int degree = relative_degree(in);
    cd num(1.0, 0.0), den(1.0, 0.0);
    for (const auto& z : in.zeros) {
        out.zeros.push_back(wo / z);
        num *= -z;
    }
    for (const auto& p : in.poles) {
        out.poles.push_back(wo / p);
        den *= -p;
    }
    for (int i = 0; i < degree; ++i) out.zeros.push_back(cd(0.0, 0.0));
    out.gain = in.gain * (num / den).real();
    return out;
}

Zpk lp2bp(const Zpk& in, double wo, double bw) {
    Zpk out;
    const int degree = relative_degree(in);
    const cd wo2(wo * wo, 0.0);
    for (const auto& z : in.zeros) {
        const cd lp = z * (bw / 2.0);
        const cd root = std::sqrt(lp * lp - wo2);
        out.zeros.push_back(lp + root);
        out.zeros.push_back(lp - root);
    }
    for (const auto& p : in.poles) {
        const cd lp = p * (bw / 2.0);
        const cd root = std::sqrt(lp * lp - wo2);
        out.poles.push_back(lp + root);
        out.poles.push_back(lp - root);
    }
    for (int i = 0; i < degree; ++i) out.zeros.push_back(cd(0.0, 0.0));
    out.gain = in.gain * std::pow(bw, degree);
    return out;
}

Zpk bilinear(const Zpk& in, double fs) {
    Zpk out;
    const int degree = relative_degree(in);
    const double fs2 = 2.0 * fs;
    cd num(1.0, 0.0), den(1.0, 0.0);
    for (const auto& z : in.zeros) {
        out.zeros.push_back((fs2 + z) / (fs2 - z));
        num *= (fs2 - z);
    }
    for (const auto& p : in.poles) {
        out.poles.push_back((fs2 + p) / (fs2 - p));
        den *= (fs2 - p);
    }
    for (int i = 0; i < degree; ++i) out.zeros.push_back(cd(-1.0, 0.0));
    out.gain = in.gain * (num / den).real();
    return out;
}

// Split a conjugate-symmetric root set into real roots and one of each pair.
void split_roots(std::vector<cd> roots, std::vector<double>& reals,
                 std::vector<cd>& pairs) {
    const double tol = 1e-9;
    std::vector<bool> used(roots.size(), false);
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (used[i]) continue;
        if (std::abs(roots[i].imag()) < tol) {
            used[i] = true;
            reals.push_back(roots[i].real());
            continue;
        }
        // Find this root's conjugate partner.
        std::size_t best = roots.size();
        double best_err = 1e30;
        for (std::size_t j = i + 1; j < roots.size(); ++j) {
            if (used[j]) continue;
            const double err = std::abs(roots[j] - std::conj(roots[i]));
            if (err < best_err) {
                best_err = err;
                best = j;
            }
        }
        used[i] = true;
        if (best < roots.size()) used[best] = true;
        pairs.push_back(cd(roots[i].real(), std::abs(roots[i].imag())));
    }
}

// Build second-order sections from zeros/poles.  Conjugate pairs form one
// biquad each; leftover real roots are combined in twos.
Sos zpk_to_sos(const Zpk& zpk) {
    std::vector<double> zr, pr;
    std::vector<cd> zc, pc;
    split_roots(zpk.zeros, zr, zc);
    split_roots(zpk.poles, pr, pc);

    struct Quad {
        double c0 = 1.0, c1 = 0.0, c2 = 0.0;
    };
    auto from_pair = [](const cd& r) {
        Quad q;
        q.c0 = 1.0;
        q.c1 = -2.0 * r.real();
        q.c2 = std::norm(r);
        return q;
    };
    auto from_reals = [](double a, double b) {
        Quad q;
        q.c0 = 1.0;
        q.c1 = -(a + b);
        q.c2 = a * b;
        return q;
    };

    auto build = [&](std::vector<double>& reals, std::vector<cd>& pairs) {
        std::vector<Quad> quads;
        for (const auto& p : pairs) quads.push_back(from_pair(p));
        std::sort(reals.begin(), reals.end());
        std::size_t i = 0;
        while (i + 1 < reals.size()) {
            quads.push_back(from_reals(reals[i], reals[i + 1]));
            i += 2;
        }
        if (i < reals.size()) {
            Quad q;
            q.c0 = 1.0;
            q.c1 = -reals[i];
            q.c2 = 0.0;
            quads.push_back(q);
        }
        return quads;
    };

    std::vector<Quad> zq = build(zr, zc);
    std::vector<Quad> pq = build(pr, pc);
    const std::size_t n = std::max(zq.size(), pq.size());
    while (zq.size() < n) zq.push_back(Quad{});          // trivial numerator
    while (pq.size() < n) pq.push_back(Quad{});          // trivial denominator

    Sos sos;
    sos.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        Biquad bq;
        bq.b0 = zq[i].c0;
        bq.b1 = zq[i].c1;
        bq.b2 = zq[i].c2;
        bq.a0 = pq[i].c0;
        bq.a1 = pq[i].c1;
        bq.a2 = pq[i].c2;
        sos.push_back(bq);
    }
    if (sos.empty()) sos.push_back(Biquad{});
    // The overall gain rides on the first section.
    sos[0].b0 *= zpk.gain;
    sos[0].b1 *= zpk.gain;
    sos[0].b2 *= zpk.gain;
    return sos;
}

}  // namespace

Sos butter(int order, const std::vector<double>& wn, BandType type) {
    if (order < 1) throw std::invalid_argument("filter order must be >= 1");
    for (double w : wn) {
        if (!(w > 0.0 && w < 1.0))
            throw std::invalid_argument("normalised frequency must be in (0, 1)");
    }
    // scipy normalises to fs = 2, so Nyquist is 1.0.
    const double fs = 2.0;
    auto prewarp = [fs](double w) { return 2.0 * fs * std::tan(M_PI * w / fs); };

    Zpk zpk = butter_prototype(order);
    switch (type) {
        case BandType::LowPass:
            zpk = lp2lp(zpk, prewarp(wn.at(0)));
            break;
        case BandType::HighPass:
            zpk = lp2hp(zpk, prewarp(wn.at(0)));
            break;
        case BandType::BandPass: {
            if (wn.size() < 2) throw std::invalid_argument("bandpass needs two frequencies");
            const double lo = prewarp(wn[0]);
            const double hi = prewarp(wn[1]);
            if (!(hi > lo)) throw std::invalid_argument("bandpass edges must be ordered");
            zpk = lp2bp(zpk, std::sqrt(lo * hi), hi - lo);
            break;
        }
    }
    return zpk_to_sos(bilinear(zpk, fs));
}

Sos butter_lowpass(int order, double wn) { return butter(order, {wn}, BandType::LowPass); }
Sos butter_highpass(int order, double wn) { return butter(order, {wn}, BandType::HighPass); }
Sos butter_bandpass(int order, double low, double high) {
    return butter(order, {low, high}, BandType::BandPass);
}

std::vector<double> sosfilt(const Sos& sos, const std::vector<double>& x,
                            std::vector<double>* zi) {
    std::vector<double> state(sos.size() * 2, 0.0);
    if (zi) {
        if (zi->size() != state.size()) throw std::invalid_argument("zi has the wrong size");
        state = *zi;
    }
    std::vector<double> y = x;
    for (std::size_t s = 0; s < sos.size(); ++s) {
        const Biquad& bq = sos[s];
        double z1 = state[2 * s];
        double z2 = state[2 * s + 1];
        for (std::size_t i = 0; i < y.size(); ++i) {
            const double in = y[i];
            const double out = bq.b0 * in + z1;          // transposed direct form II
            z1 = bq.b1 * in - bq.a1 * out + z2;
            z2 = bq.b2 * in - bq.a2 * out;
            y[i] = out;
        }
        state[2 * s] = z1;
        state[2 * s + 1] = z2;
    }
    if (zi) *zi = state;
    return y;
}

std::vector<double> sosfilt_zi(const Sos& sos) {
    std::vector<double> zi(sos.size() * 2, 0.0);
    double scale = 1.0;
    for (std::size_t s = 0; s < sos.size(); ++s) {
        const Biquad& bq = sos[s];
        // Steady state of one biquad under a unit step: solve (I - A) z = B.
        const double m00 = 1.0 + bq.a1, m01 = -1.0;
        const double m10 = bq.a2, m11 = 1.0;
        const double r0 = bq.b1 - bq.a1 * bq.b0;
        const double r1 = bq.b2 - bq.a2 * bq.b0;
        const double det = m00 * m11 - m01 * m10;
        double z0 = 0.0, z1 = 0.0;
        if (std::abs(det) > 1e-300) {
            z0 = (r0 * m11 - m01 * r1) / det;
            z1 = (m00 * r1 - r0 * m10) / det;
        }
        zi[2 * s] = scale * z0;
        zi[2 * s + 1] = scale * z1;
        // Later sections see this section's DC gain.
        const double num = bq.b0 + bq.b1 + bq.b2;
        const double den = bq.a0 + bq.a1 + bq.a2;
        if (std::abs(den) > 1e-300) scale *= num / den;
    }
    return zi;
}

std::vector<double> sosfiltfilt(const Sos& sos, const std::vector<double>& x) {
    const std::size_t n = x.size();
    if (n == 0) return {};

    // scipy pads by 3 * (2 * n_sections + 1), minus trivial trailing taps.
    std::size_t ntaps = 2 * sos.size() + 1;
    std::size_t trivial_b = 0, trivial_a = 0;
    for (const auto& bq : sos) {
        if (bq.b2 == 0.0) ++trivial_b;
        if (bq.a2 == 0.0) ++trivial_a;
    }
    ntaps -= std::min(trivial_b, trivial_a);
    const std::size_t edge = ntaps * 3;
    if (n <= edge) {
        // Too short to pad the scipy way; fall back to plain forward-backward.
        std::vector<double> y = sosfilt(sos, x);
        std::reverse(y.begin(), y.end());
        y = sosfilt(sos, y);
        std::reverse(y.begin(), y.end());
        return y;
    }

    // Odd extension: 2*x[0] - x[edge..1], and the mirror at the end.
    std::vector<double> ext;
    ext.reserve(n + 2 * edge);
    for (std::size_t i = 0; i < edge; ++i) ext.push_back(2.0 * x[0] - x[edge - i]);
    ext.insert(ext.end(), x.begin(), x.end());
    for (std::size_t i = 0; i < edge; ++i)
        ext.push_back(2.0 * x[n - 1] - x[n - 2 - i]);

    const std::vector<double> zi_unit = sosfilt_zi(sos);

    std::vector<double> zi = zi_unit;
    for (double& v : zi) v *= ext.front();
    std::vector<double> forward = sosfilt(sos, ext, &zi);

    std::reverse(forward.begin(), forward.end());
    zi = zi_unit;
    for (double& v : zi) v *= forward.front();
    std::vector<double> backward = sosfilt(sos, forward, &zi);
    std::reverse(backward.begin(), backward.end());

    return std::vector<double>(backward.begin() + edge, backward.begin() + edge + n);
}

}  // namespace vocalai
