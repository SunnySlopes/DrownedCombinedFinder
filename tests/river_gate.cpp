#include "../third_party/riverfinder/climate_gate.hpp"
#include <cstdio>
#include "../third_party/dripstone/octave_cache.hpp"


bool verifyOctaveCache(const BiomeNoise *bn) {
    dripstone::OctaveCache cache;
    for (int parameter : {NP_CONTINENTALNESS,NP_WEIRDNESS}) {
        int index = parameter == NP_CONTINENTALNESS ? 8 : 2;
        auto &field = parameter == NP_CONTINENTALNESS ? cache.continental[index] : cache.weirdness[index];
        field.step = parameter == NP_CONTINENTALNESS ? 32 : 8;
        field.phaseX = field.phaseZ = field.step/2;
        field.side = int(std::llround(256.0/bn->climate[parameter].octA.octaves[index].lacunarity))/field.step;
        field.values.resize(size_t(field.side)*field.side);
        for (int z = 0; z < field.side; ++z)
            for (int x = 0; x < field.side; ++x)
                field.values[size_t(z)*field.side+x] = dripstone::OctaveCache::live(
                    bn,parameter,index,false,x*field.step+field.step/2,z*field.step+field.step/2);
        for (int trial = -64; trial < 64; ++trial) {
            int x = trial*field.step*7919+field.step/2, z = -trial*field.step*7823+field.step/2;
            float expected = sampleClimateNoise(bn,parameter,x,0,z);
            float actual = cache.noise(bn,parameter,x,z);
            if (std::abs(expected-actual) > 0.00001F)
                return false;
        }
        // 最大周期也需覆盖正负远坐标，避免只验证最短周期和小坐标。
        const auto &noise = bn->climate[parameter];
        for (int octave = 0; octave < noise.octA.octcnt; ++octave) {
            int period = int(std::llround(256.0/noise.octA.octaves[octave].lacunarity));
            for (int trial = -16; trial < 16; ++trial) {
                int x = trial*31213+field.step/2, z = -trial*51317+field.step/2;
                int wrappedX = (x%period+period)%period, wrappedZ = (z%period+period)%period;
                double expected = dripstone::OctaveCache::live(bn,parameter,octave,false,x,z);
                double actual = dripstone::OctaveCache::live(bn,parameter,octave,false,wrappedX,wrappedZ);
                if (std::abs(expected-actual) > 0.00001)
                    return false;
            }
        }
    }
    return true;
}

int main()
{
    Generator generator;
    setupGenerator(&generator, MC_26_3, 0);
    uint64_t random = 20261029;
    const uint64_t seeds[] = {0, UINT64_MAX, 12345, UINT64_C(8050192242802360875)};
    int rivers = 0;
    for (uint64_t seed : seeds)
    {
        applySeed(&generator, DIM_OVERWORLD, seed);
        if (!verifyOctaveCache(&generator.bn))
            return 3;
        for (int i = 0; i < 36000; i++)
        {
            random = random * UINT64_C(6364136223846793005) + 1;
            int x = int(random % 15000000) - 7500000;
            random = random * UINT64_C(6364136223846793005) + 1;
            int z = int(random % 15000000) - 7500000;
            // 固定复现旧预筛误杀的qw=-500点；其余覆盖地表与地下。
            int y = i % 3 == 0 ? i % 96 - 16 : 16;
            if (seed == 0 && i == 0)
            {
                x = 7499999;
                z = 858428;
                y = 16;
            }
            int full = sampleBiomeNoise(&generator.bn, nullptr, x, y, z, nullptr, SAMPLE_NO_SHIFT);
            int fast = riverfinder::sampleRiverBiomeNoise(&generator.bn, x, y, z, SAMPLE_NO_SHIFT);
            float knownW = sampleClimateNoise(&generator.bn,NP_WEIRDNESS,x,0,z);
            int reused = riverfinder::riverBiomeIdAt(&generator.bn,x,y,z,&knownW);
            if (fast != reused)
                return 5;
            if (full == river) rivers++;
            if ((full == river) != (fast == river))
            {
                std::fprintf(stderr, "River gate mismatch at seed %llu (%d,%d,%d)\n",
                             static_cast<unsigned long long>(seed), x, y, z);
                return 1;
            }
        }
    }
    setupGenerator(&generator,MC_1_18_2,0);
    applySeed(&generator,DIM_OVERWORLD,12345);
    if (!verifyOctaveCache(&generator.bn))
        return 4;
    std::printf("144000 river gate samples matched; %d river points\n", rivers);
    return 0;
}
