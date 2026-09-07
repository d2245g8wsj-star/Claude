// A very small test harness plus loaders for the Python-generated reference
// vectors.  The point of these tests is not that the C++ merely runs, but that
// it agrees numerically with the implementation that was validated first.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace test {

inline int failures = 0;
inline int checks = 0;

inline void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cout << "  FAIL: " << what << "\n";
    }
}

inline void check_close(double got, double want, double tol, const std::string& what) {
    const bool ok = std::abs(got - want) <= tol;
    ++checks;
    if (!ok) {
        ++failures;
        std::cout << "  FAIL: " << what << "  got " << got << " want " << want
                  << " (tol " << tol << ")\n";
    }
}

// Largest absolute difference over a range, ignoring `edge` samples at each end.
inline double max_diff(const std::vector<double>& a, const std::vector<double>& b,
                       std::size_t edge = 0) {
    const std::size_t n = std::min(a.size(), b.size());
    double worst = 0.0;
    for (std::size_t i = edge; i + edge < n; ++i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

inline std::string ref_dir() {
    const char* env = std::getenv("VOCALAI_REF_DIR");
    return env ? std::string(env) : std::string("reference");
}

inline std::vector<double> load(const std::string& name) {
    const std::string path = ref_dir() + "/" + name + ".f64";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cout << "  FAIL: missing reference file " << path << "\n";
        ++failures;
        return {};
    }
    in.seekg(0, std::ios::end);
    const std::streamsize bytes = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<double> out(static_cast<std::size_t>(bytes) / sizeof(double));
    in.read(reinterpret_cast<char*>(out.data()), bytes);
    return out;
}

inline void section(const std::string& name) { std::cout << name << "\n"; }

inline int report() {
    std::cout << "\n" << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

}  // namespace test
