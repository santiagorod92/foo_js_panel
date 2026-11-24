#include "audio_meters.h"

#include <algorithm>
#include <cmath>

namespace jsp {
namespace {

constexpr double kFloor = -100;
constexpr double kPi = 3.14159265358979323846;

double to_db(double amplitude) { return amplitude > 0 ? std::max(kFloor, 20 * std::log10(amplitude)) : kFloor; }

struct Biquad {
    double b0, b1, b2, a1, a2;
    double z1 = 0, z2 = 0;
    double run(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

Biquad shelf(int rate) {
    const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
    const double k = std::tan(kPi * f0 / rate), vh = std::pow(10, g / 20), vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1 + k / q + k * k;
    return { (vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0, 2 * (k * k - 1) / a0,
             (1 - k / q + k * k) / a0 };
}

Biquad highpass(int rate) {
    const double f0 = 38.13547087602444, q = 0.5003270373238773;
    const double k = std::tan(kPi * f0 / rate), a0 = 1 + k / q + k * k;
    return { 1, -2, 1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0 };
}

double channel_weight(int channel, int channels) {
    if (channels >= 6 && channel == 3) return 0;
    if (channels >= 5 && channel >= (channels >= 6 ? 4 : 3)) return 1.41;
    return 1;
}

}

AudioMeters compute_meters(const std::vector<float>& interleaved, int channels, int sampleRate, double chunkSeconds) {
    AudioMeters m;
    if (channels <= 0 || sampleRate <= 0) return m;
    const size_t frames = interleaved.size() / (size_t)channels;
    if (frames == 0) return m;
    const int right = channels > 1 ? 1 : 0;
    const size_t chunk = std::min(frames, (size_t)std::max(1.0, chunkSeconds * sampleRate));
    double sl = 0, sr = 0, slr = 0, all = 0, pl = 0, pr = 0;
    for (size_t f = frames - chunk; f < frames; ++f) {
        const double l = interleaved[f * channels], r = interleaved[f * channels + right];
        sl += l * l;
        sr += r * r;
        slr += l * r;
        pl = std::max(pl, std::fabs(l));
        pr = std::max(pr, std::fabs(r));
        for (int c = 0; c < channels; ++c) all += (double)interleaved[f * channels + c] * interleaved[f * channels + c];
    }
    m.leftRms = to_db(std::sqrt(sl / chunk));
    m.rightRms = to_db(std::sqrt(sr / chunk));
    m.rms = to_db(std::sqrt(all / (chunk * channels)));
    m.leftPeak = to_db(pl);
    m.rightPeak = to_db(pr);
    m.correlation = sl > 0 && sr > 0 ? std::clamp(slr / std::sqrt(sl * sr), -1.0, 1.0) : 0;

    const size_t momentary = std::min(frames, (size_t)(0.4 * sampleRate));
    double shortSum = 0, momentarySum = 0;
    for (int c = 0; c < channels; ++c) {
        const double w = channel_weight(c, channels);
        if (w == 0) continue;
        Biquad a = shelf(sampleRate), b = highpass(sampleRate);
        double whole = 0, tail = 0;
        for (size_t f = 0; f < frames; ++f) {
            const double y = b.run(a.run(interleaved[f * channels + c]));
            whole += y * y;
            if (f >= frames - momentary) tail += y * y;
        }
        shortSum += w * whole / frames;
        momentarySum += w * tail / momentary;
    }
    const auto lufs = [](double sum) { return sum > 0 ? std::max(kFloor, -0.691 + 10 * std::log10(sum)) : kFloor; };
    m.shortTermLufs = lufs(shortSum);
    m.momentaryLufs = lufs(momentarySum);
    return m;
}

}
