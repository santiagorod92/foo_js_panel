#pragma once
#include <vector>

namespace jsp {

struct AudioMeters {
    double leftRms = -100, rightRms = -100, rms = -100;
    double leftPeak = -100, rightPeak = -100;
    double momentaryLufs = -100, shortTermLufs = -100;
    double correlation = 0;
};

AudioMeters compute_meters(const std::vector<float>& interleaved, int channels, int sampleRate, double chunkSeconds);

}
