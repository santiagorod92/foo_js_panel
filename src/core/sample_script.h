#pragma once
#include <vector>

namespace jsp {

const char* sample_script();

struct Sample {
    const char* name;
    const char* file;
    const char* source;
};
const std::vector<Sample>& samples();

}
