#include <minecraft_finders/river.hpp>
#include <iostream>

int main() {
    riverfinder_native::Config config;
    config.seed = "12345";
    config.centerX = -87;
    config.centerZ = -236;
    config.radius = 64;
    config.coverage = "square";
    config.innerRadius = 16;
    config.includeCaves = true;
    config.minArea = 0;
    config.threads = 1;
    riverfinder_native::Control control;
    auto result = riverfinder_native::search(config, control);
    std::cout << result.hits.size() << '\n';
    return result.status == "completed" ? 0 : 1;
}
