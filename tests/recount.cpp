#include "storage.hpp"
#include "coverage.hpp"
#include <cmath>
#define lerp cubiomes_lerp
#include <cubiomes/rng.h>
#undef lerp
#include <cubiomes/generator.h>
#include <fstream>
#include <iostream>
#include <memory>

using namespace riverfinder_native;

int main(int argc,char **argv) {
    if (argc != 2)
        return 2;
    std::ifstream input(argv[1],std::ios::binary);
    Json result;
    input >> result;
    Config cfg = configFromJson(result.at("config"));
    auto g = std::make_unique<Generator>();
    setupGenerator(g.get(),versionInfo(cfg.version).cubiomesMc,0);
    applySeed(g.get(),DIM_OVERWORLD,parseSeed(cfg.seed));
    int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;
    for (const auto &row : result.at("candidates")) {
        Hit expected;
        int px = row.at("x"),pz = row.at("z"),py = row.at("y");
        for (int y = -63; y <= (cfg.includeCaves ? -51 : -63); ++y) {
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    int x = px+dx,z = pz+dz;
                    if (x < kWorldMin || x > kWorldMax || z < kWorldMin || z > kWorldMax)
                        continue;
                    int64_t d = int64_t(dx)*dx+int64_t(dz)*dz;
                    if (cfg.coverage == "circle" && d > int64_t(radius)*radius)
                        continue;
                    if (cfg.coverage == "game") {
                        d += int64_t(y-py)*(y-py);
                        if (d <= 576 || d > 16384 ||
                            !naturalChunk(floorDiv(x,16),floorDiv(z,16),px,pz,cfg.simulationDistanceChunks))
                            continue;
                    }
                    int id = getBiomeAt(g.get(),1,x,y,z);
                    if (id == river) {
                        ++expected.riverVolume;
                        if (y == -63)
                            ++expected.riverArea;
                    } else if (cfg.includeCaves && id == dripstone_caves) {
                        ++expected.caveVolume;
                    }
                }
            }
        }
        if (expected.riverArea != row.at("riverArea") || expected.riverVolume != row.at("riverVolume") ||
            expected.caveVolume != row.at("caveVolume")) {
            std::cerr << "independent recount mismatch\n";
            return 1;
        }
    }
    std::cout << "independent block recount passed\n";
}
