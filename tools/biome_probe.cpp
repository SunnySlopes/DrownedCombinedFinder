#include "engine.hpp"
#include <cmath>
#define lerp cubiomes_lerp
#include <cubiomes/rng.h>
#undef lerp
#include <cubiomes/generator.h>
#include <iostream>
#include <memory>
#include <string>

int main(int argc,char **argv) {
    if (argc < 2)
        return 2;
    auto g = std::make_unique<Generator>();
    setupGenerator(g.get(),riverfinder_native::versionInfo(argv[1]).cubiomesMc,0);
    uint64_t current = 1;
    bool initialized = false;
    std::string seed;
    int x,y,z;
    while (std::cin >> seed >> x >> y >> z) {
        uint64_t value = riverfinder_native::parseSeed(seed);
        if (!initialized || value != current) {
            applySeed(g.get(),DIM_OVERWORLD,value);
            current = value;
            initialized = true;
        }
        int id = getBiomeAt(g.get(),1,x,y,z);
        std::cout << id << '\n';
    }
}
