#include "engine.hpp"
#include <cmath>
#define lerp cubiomes_lerp
#include <cubiomes/rng.h>
#undef lerp
#include <cubiomes/generator.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <random>
#include <stdexcept>


uint64_t verifyVoronoiVolumes() {
    std::mt19937 random(0x981ac3);
    uint64_t checked = 0;
    for (int trial = 0; trial < 128; ++trial) {
        int origin = trial % 3 == 0 ? -29999800 : trial % 3 == 1 ? -257 : 29999800;
        Range r{1,origin+int(random()%16),-origin+int(random()%16),
                1+int(random()%37),1+int(random()%29),-80+int(random()%40),1+int(random()%19)};
        Range s = getVoronoiSrcRange(r);
        std::vector<int> src(size_t(s.sx)*s.sy*s.sz), out(size_t(r.sx)*r.sy*r.sz,-999);
        for (auto &id : src)
            id = trial%4 == 0 ? 7 : int(random()%(trial%4 == 1 ? 2 : 255));
        uint64_t sha = getVoronoiSHA((uint64_t(random())<<32)|random());
        mapVoronoiVolume(sha,out.data(),src.data(),r.x,r.y,r.z,r.sx,r.sy,r.sz,
                         s.x,s.y,s.z,s.sx,s.sy,s.sz);
        for (int y = 0; y < r.sy; ++y)
            for (int z = 0; z < r.sz; ++z)
                for (int x = 0; x < r.sx; ++x) {
                    int qx,qy,qz;
                    voronoiAccess3D(sha,r.x+x,r.y+y,r.z+z,&qx,&qy,&qz);
                    int expected = src[(size_t(qy-s.y)*s.sz+qz-s.z)*s.sx+qx-s.x];
                    if (out[(size_t(y)*r.sz+z)*r.sx+x] != expected)
                        throw std::runtime_error("Voronoi volume differs from independent point lookup");
                    ++checked;
                }
    }
    return checked;
}


uint64_t verifyCavePruning() {
    using namespace riverfinder_native;
    std::mt19937 random(0x263018);
    uint64_t checked = 0, pruned = 0;
    const int edges[] = {-1,0,1,1999,2000,2001,8999,9000,9001,9999,10000,10001,10999,11000,11001};
    for (const auto &version : versions()) {
        int mc = versionInfo(version).cubiomesMc;
        for (int trial = 0; trial < 10000; ++trial) {
            uint64_t np[6];
            for (auto &value : np)
                value = int64_t(int(random()%30001)-15000);
            np[NP_DEPTH] = trial < 5000 ? int64_t(edges[trial%15]) : int64_t(int(random()%25001)-5000);
            if (trial%3 == 0) {
                np[NP_CONTINENTALNESS] = 8000;
                np[NP_HUMIDITY] = 7000;
                np[NP_EROSION] = -3750;
            }
            uint64_t cold = 0;
            int reference = climateToBiome(mc,np,&cold);
            int fast = climateCaveBiome(mc,np);
            if (climateToBiome(mc,np,nullptr) != reference || (fast != none && fast != reference))
                throw std::runtime_error("Cave pruning differs from cold full-tree query");
            pruned += fast != none;
            ++checked;
        }
    }
    std::cout << pruned << "/" << checked << " climate queries proved by cave bounds\n";
    return checked;
}


uint64_t verifyCaveIntervals() {
    using namespace riverfinder_native;
    std::mt19937 random(0x18263);
    uint64_t count = 0;
    for (const auto &version : versions())
        for (int trial = 0; trial < 64; ++trial) {
            uint64_t np[6];
            for (auto &value : np)
                value = int64_t(int(random()%24001)-12000);
            int states[256]{}, previous = none;
            for (int depth = -1000; depth <= 15000; depth += 250) {
                np[NP_DEPTH] = int64_t(depth);
                uint64_t cold = 0;
                int id = climateToBiome(versionInfo(version).cubiomesMc,np,&cold);
                auto cave = [](int v) {
                    return v == dripstone_caves || v == lush_caves || v == deep_dark || v == sulfur_caves;
                };
                if (id != previous && cave(previous))
                    states[previous] = 2;
                if (cave(id)) {
                    if (states[id] == 2)
                        throw std::runtime_error("Cave winning depths are not one interval");
                    states[id] = 1;
                }
                previous = id;
                ++count;
            }
        }
    return count;
}

int main(int argc,char **argv) {
    using namespace riverfinder_native;
    uint64_t checked = verifyVoronoiVolumes()+verifyCavePruning()+verifyCaveIntervals();
    for (const auto &version : versions()) {
        auto g = std::make_unique<Generator>();
        setupGenerator(g.get(),versionInfo(version).cubiomesMc,0);
        for (uint64_t seed : {uint64_t(0),uint64_t(8050192242802360875ULL)}) {
            applySeed(g.get(),DIM_OVERWORLD,seed);
            for (int base : {-29999800,-257,17,29999800}) {
                Range r{1,base,-base,5,5,-63,13};
                std::vector<int> out(getMinCacheSize(g.get(),1,5,13,5));
                if (genBiomes(g.get(),out.data(),r))
                    return 2;
                for (int y = 0; y < 13; ++y)
                    for (int z = 0; z < 5; ++z)
                        for (int x = 0; x < 5; ++x) {
                            int id = getBiomeAt(g.get(),1,base+x,-63+y,-base+z);
                            if (id != out[(y*5+z)*5+x]) {
                                std::cerr << version << ' ' << base << ' ' << x << ',' << y << ',' << z << '\n';
                                return 1;
                            }
                            ++checked;
                        }
            }
        }
    }
    if (argc > 1) {
        std::ifstream input(argv[1]);
        std::string version,seed,name,previous;
        int x,y,z,id;
        std::unique_ptr<Generator> g;
        uint64_t applied = 0;
        while (input >> version >> seed >> x >> y >> z >> id >> name) {
            if (version != previous) {
                g = std::make_unique<Generator>();
                setupGenerator(g.get(),versionInfo(version).cubiomesMc,0);
                previous = version;
                applied = parseSeed(seed);
                applySeed(g.get(),DIM_OVERWORLD,applied);
            } else if (applied != parseSeed(seed)) {
                applied = parseSeed(seed);
                applySeed(g.get(),DIM_OVERWORLD,applied);
            }
            if (getBiomeAt(g.get(),1,x,y,z) != id) {
                std::cerr << "original mismatch " << version << ' ' << x << ' ' << y << ' ' << z << '\n';
                return 1;
            }
            ++checked;
        }
        if (previous.empty())
            return 3;
    }
    std::cout << checked << " block biomes match independent point queries\n";
}
