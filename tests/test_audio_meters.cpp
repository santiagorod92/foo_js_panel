#include "test.h"
#include "core/audio_meters.h"

#include <cmath>

using namespace jsp;

namespace {

std::vector<float> sine(int rate, double seconds, double hz, double left, double right) {
    std::vector<float> out;
    const int n = (int)(rate * seconds);
    for (int i = 0; i < n; ++i) {
        const double s = std::sin(2 * 3.14159265358979323846 * hz * i / rate);
        out.push_back((float)(s * left));
        out.push_back((float)(s * right));
    }
    return out;
}

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

}

TEST("audio meters: a full-scale 1 kHz sine reads 0 LUFS in stereo and -3 LUFS on one channel") {
    const AudioMeters both = compute_meters(sine(48000, 3, 1000, 1, 1), 2, 48000, 0.05);
    CHECK(near(both.shortTermLufs, 0, 0.1));
    CHECK(near(both.momentaryLufs, 0, 0.1));
    const AudioMeters left = compute_meters(sine(48000, 3, 1000, 1, 0), 2, 48000, 0.05);
    CHECK(near(left.shortTermLufs, -3.01, 0.1));
    CHECK_EQ(left.rightRms, -100.0);
    CHECK_EQ(left.correlation, 0.0);
}

TEST("audio meters: RMS, peaks and correlation over the last chunk") {
    const AudioMeters m = compute_meters(sine(44100, 1, 441, 0.5, -0.5), 2, 44100, 0.1);
    CHECK(near(m.leftPeak, 20 * std::log10(0.5), 0.05));
    CHECK(near(m.leftRms, 20 * std::log10(0.5 / std::sqrt(2.0)), 0.05));
    CHECK(near(m.rightRms, m.leftRms, 0.001));
    CHECK(near(m.correlation, -1, 0.001));
    CHECK(near(m.rms, m.leftRms, 0.001));
}

TEST("audio meters: silence and empty input stay at the floor") {
    const AudioMeters quiet = compute_meters(std::vector<float>(9600, 0.0f), 2, 48000, 0.05);
    CHECK_EQ(quiet.momentaryLufs, -100.0);
    CHECK_EQ(quiet.leftPeak, -100.0);
    const AudioMeters none = compute_meters({}, 2, 48000, 0.05);
    CHECK_EQ(none.rms, -100.0);
    const AudioMeters mono = compute_meters(sine(48000, 1, 1000, 1, 1), 1, 48000, 0.05);
    CHECK(near(mono.leftRms, mono.rightRms, 0.001));
}
