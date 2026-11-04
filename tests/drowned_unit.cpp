#include "storage.hpp"
#include "coverage.hpp"
#include "coarse_mask.hpp"
#include "core/platform/player_y.hpp"
#include <cubiomes/biomes.h>
#include <iostream>
#include <stdexcept>

using namespace riverfinder_native;
void check(bool value,const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Hit brute(const Config &cfg,const std::vector<int> &ids,int wx,int wz,int w,int h,
          int layers,int px,int pz,int py) {
    Hit hit;
    hit.x = px;
    hit.z = pz;
    hit.y = py;
    for (int layer = 0; layer < layers; ++layer) {
        for (int z = 0; z < h; ++z) {
            for (int x = 0; x < w; ++x) {
                int worldX = wx+x, worldZ = wz+z;
                int64_t dx = worldX-px, dz = worldZ-pz, dy = -63+layer-py;
                int64_t d = dx*dx+dz*dz;
                if (cfg.coverage == "square") {
                    if (std::abs(dx) > cfg.innerRadius || std::abs(dz) > cfg.innerRadius)
                        continue;
                } else if (cfg.coverage == "circle") {
                    if (d > int64_t(cfg.innerRadius)*cfg.innerRadius)
                        continue;
                } else {
                    if (d+dy*dy <= 576 || d+dy*dy > 16384)
                        continue;
                    auto chunk = [](int n) { return int(std::floor(double(n)/16)); };
                    int cx = chunk(worldX), cz = chunk(worldZ);
                    if (std::abs(cx-chunk(px)) > cfg.simulationDistanceChunks ||
                        std::abs(cz-chunk(pz)) > cfg.simulationDistanceChunks)
                        continue;
                    double qx = cx*16+8-(px+0.5), qz = cz*16+8-(pz+0.5);
                    if (qx*qx+qz*qz >= 16384)
                        continue;
                }
                int id = ids[(size_t(layer)*h+z)*w+x];
                if (id == river) {
                    ++hit.riverVolume;
                    if (!layer)
                        ++hit.riverArea;
                } else if (cfg.includeCaves && id == dripstone_caves) {
                    ++hit.caveVolume;
                }
            }
        }
    }
    return hit;
}

void verifyCoarseMask() {
    int w = 19, h = 13;
    std::vector<uint8_t> potential(w*h), expected(w*h);
    for (int i = 0; i < w*h; ++i) potential[i] = (i*7+i/13)%5 == 0;
    CoarseMask mask(w,h,potential);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            for (int r : {0,1,4,32}) {
                uint32_t count = 0;
                for (int j = std::max(0,z-r); j <= std::min(h-1,z+r); ++j)
                    for (int i = std::max(0,x-r); i <= std::min(w-1,x+r); ++i)
                        count += potential[j*w+i] != 0;
                check(mask.count(x,z,r) == count,"粗筛方形上界积分图");
                std::vector<int> widths;
                for (int dz = -r; dz <= r; ++dz)
                    widths.push_back(int(std::sqrt(r*r-dz*dz)));
                uint32_t disk = 0;
                for (int j = 0; j < h; ++j)
                    for (int i = 0; i < w; ++i)
                        if ((i-x)*(i-x)+(j-z)*(j-z) <= r*r)
                            disk += potential[j*w+i] != 0;
                check(mask.countDisk(x,z,widths) == disk,"粗筛圆盘上界必须覆盖正确的格点");
                if ((x+z+r)%11 == 0) {
                    mask.include(x,z,r);
                    for (int j = std::max(0,z-r); j <= std::min(h-1,z+r); ++j)
                        for (int i = std::max(0,x-r); i <= std::min(w-1,x+r); ++i)
                            expected[j*w+i] = 1;
                }
            }
    mask.finish();
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            check(mask.needs(x,z) == bool(expected[z*w+x]),"保留窗口并集不能漏掉所需列");
}

void verifySparseRows() {
    Config cfg;
    cfg.includeCaves = true;
    for (int scale : {1,8,16,32}) {
        constexpr int w = 19, h = 23, layers = 13, x0 = -161, z0 = -177;
        std::vector<int> ids(w*h*layers);
        for (size_t i = 0; i < ids.size(); ++i)
            ids[i] = i%5 == 0 ? river : i%7 == 0 ? dripstone_caves : deep_dark;
        BiomeWindow window(x0,z0,w,h,layers,ids.data(),scale);
        for (const auto &mode : {"game","circle"}) {
            cfg.coverage = mode;
            cfg.innerRadius = 47;
            cfg.simulationDistanceChunks = 5;
            for (bool upper : {false,true})
                for (int py : {-63,-40,77,200})
                    for (int px : {-19,-16,-1,0,17})
                        for (int pz : {-33,-16,0,1,31}) {
                            Hit expected;
                            for (int y = 0; y < layers; ++y)
                                for (int z = 0; z < h; ++z)
                                    for (int x = 0; x < w; ++x) {
                                        int wx = x0+x*scale, wz = z0+z*scale;
                                        int64_t dx = wx-px, dz = wz-pz;
                                        bool game = cfg.coverage == "game";
                                        int radius = game ? 128 : cfg.innerRadius;
                                        int64_t dy = game && !upper ? -63+y-py : 0;
                                        int64_t d = dx*dx+dz*dz+dy*dy;
                                        if (d > int64_t(radius)*radius || (game && !upper && d <= 576))
                                            continue;
                                        if (game && !naturalChunk(floorDiv(wx,16),floorDiv(wz,16),
                                                                  px,pz,cfg.simulationDistanceChunks))
                                            continue;
                                        int id = ids[(size_t(y)*h+z)*w+x];
                                        if (id == river) {
                                            expected.riverVolume += scale*scale;
                                            if (!y)
                                                expected.riverArea += scale*scale;
                                        } else if (id == dripstone_caves) {
                                            expected.caveVolume += scale*scale;
                                        }
                                    }
                            auto actual = window.evaluate(cfg,px,pz,py,upper);
                            if (actual.riverVolume != expected.riverVolume ||
                                actual.caveVolume != expected.caveVolume ||
                                actual.riverArea != expected.riverArea) {
                                std::cerr << "mode=" << mode << " upper=" << upper
                                          << " py=" << py << " px=" << px << " pz=" << pz
                                          << " got " << actual.riverVolume << '/' << actual.caveVolume
                                          << '/' << actual.riverArea << " expected "
                                          << expected.riverVolume << '/' << expected.caveVolume
                                          << '/' << expected.riverArea << '\n';
                            }
                            check(actual.riverVolume == expected.riverVolume &&
                                  actual.caveVolume == expected.caveVolume &&
                                  actual.riverArea == expected.riverArea,"稀疏截面必须与逐格位几何计分一致");
                            if (!upper) {
                                // 无条件上界剪枝的前提：投影上界必须不低于全部层实算。
                                auto bound = window.evaluate(cfg,px,pz,py,true);
                                check(scoreKey(bound) >= scoreKey(actual),"投影上界必须覆盖全部层");
                            }
                        }
        }
    }
}

int main() {
    try {
        verifyCoarseMask();
        verifySparseRows();
        Config defaults;
        // 未 validate 时 playerY 是“自动”哨兵，validate() 才解析成 yMin + 23 = -40。
        check(defaults.playerY == kPlayerYAuto,"默认挂机Y为自动哨兵");
        check(!defaults.optimizePlayerY,"默认不计算最佳Y");
        validate(defaults);
        check(defaults.playerY == -40,"未计算最佳Y时解析为yMin+23=-40");
        defaults.playerY = -55;
        validate(defaults);
        check(defaults.playerY == -55,"--player-y可覆盖默认挂机Y");
        defaults.optimizePlayerY = true;
        validate(defaults);
        check(defaults.playerY == -63,"计算最佳Y时走平台解析解");

        Config offPlatform;
        offPlatform.includeCaves = true;
        offPlatform.yMin = -10;
        offPlatform.yMax = 20;
        offPlatform.playerY = -40;
        validate(offPlatform);
        bool warned = false;
        for (const auto &warning : offPlatform.warnings)
            warned = warned || warning.find("固定挂机Y") != std::string::npos;
        check(warned,"固定Y超出层范围±24时给出警告");

        Config cfg;
        cfg.playerY = -63;  // 参考上限按旧的固定 -63 记录
        check(versions().size() == 32,"版本列表");
        check(pureRiverUpperBound(cfg) == 49162,"游戏模式纯河流保守上限");
        cfg.includeCaves = true;
        check(pureRiverUpperBound(cfg) == 49211,"13层平均纯河流上限向上取整");
        cfg.optimizePlayerY = true;
        check(pureRiverUpperBound(cfg) >= 49211,"自动Y上限不可低于固定高度");
        cfg.coverage = "circle";
        cfg.innerRadius = 3;
        check(pureRiverUpperBound(cfg) == 29,"小圆纯河流上限");
        cfg.coverage = "square";
        check(pureRiverUpperBound(cfg) == 49,"小方纯河流上限");
        cfg = Config{};

        validate(cfg);
        check(cfg.candidateBudget == 8192,"默认候选预算");
        cfg.top = 200;
        cfg.candidateBudget = 0;
        validate(cfg);
        check(cfg.candidateBudget == 12800,"TopK候选预算");
        check(floorDiv(-1,16) == -1 && floorDiv(-16,16) == -1 && floorDiv(-17,16) == -2,"负坐标");
        for (bool caves : {false,true}) {
            cfg.includeCaves = caves;
            int layers = caves ? 13 : 1;
            int w = 261,h = 13;
            std::vector<int> ids(size_t(w)*h*layers);
            const int types[] = {river,dripstone_caves,deep_dark,frozen_river,ocean,plains};
            for (size_t i = 0; i < ids.size(); ++i)
                ids[i] = types[(i*13+i/19)%6];
            BiomeWindow window(-130,-6,w,h,layers,ids.data());
            std::vector<uint16_t> riverCounts(size_t(w)*h), caveCounts(size_t(w)*h);
            for (int y = 0; y < layers; ++y)
                for (int i = 0; i < w*h; ++i) {
                    riverCounts[i] += ids[size_t(y)*w*h+i] == river;
                    caveCounts[i] += ids[size_t(y)*w*h+i] == dripstone_caves;
                }
            BiomeWindow projected(-130,-6,w,h,riverCounts.data(),caves ? caveCounts.data() : nullptr,1);
            for (const char *mode : {"circle","square","game"}) {
                cfg.coverage = mode;
                cfg.innerRadius = 32;
                for (int x : {-19,0,23}) {
                    auto original = window.evaluate(cfg,x,0,-63,true);
                    auto collapsed = projected.evaluate(cfg,x,0,-63,true);
                    check(scoreKey(original) == scoreKey(collapsed),"合并13层投影不能改变粗筛分数");
                    check(!scoreKey(collapsed) || projected.hasTargets(x,0,128),"占用矩形不能跳过有分数的窗口");
                }
                for (int sim : {1,5,10}) {
                    cfg.simulationDistanceChunks = sim;
                    for (int py : {-63,-39,0,77}) {
                        for (int px : {-17,-9,-1,0,3,7,15,16}) {
                            auto a = window.evaluate(cfg,px,1,py);
                            auto b = brute(cfg,ids,-130,-6,w,h,layers,px,1,py);
                            if (a.riverVolume != b.riverVolume || a.caveVolume != b.caveVolume ||
                                a.riverArea != b.riverArea) {
                                std::cerr << mode << " caves=" << caves << " sim=" << sim
                                    << " py=" << py << " px=" << px << " got "
                                    << a.riverVolume << '/' << a.caveVolume << '/' << a.riverArea
                                    << " expected " << b.riverVolume << '/' << b.caveVolume << '/'
                                    << b.riverArea << '\n';
                                throw std::runtime_error("逐格几何对照");
                            }
                        }
                    }
                }
            }
            cfg.coverage = "game";
            cfg.simulationDistanceChunks = 10;
            cfg.playerY = -63;
            cfg.optimizePlayerY = true;
            // 平台解析解（计划 §1.2）：所有层在玩家 ±24 内时，每层覆盖面积相同，
            // 总分与挂机 Y 无关；bestHeight 不再逐 Y 穷举。
            const int platformY = cfg.includeCaves ? cfg.yMin : -63;
            Hit expected = brute(cfg,ids,-130,-6,w,h,layers,0,1,platformY);
            cfg.playerY = platformY;
            auto actual = window.bestHeight(cfg,0,1);
            check(scoreKey(actual) == scoreKey(expected) && actual.y == platformY,"平台挂机Y");
            if (caves) {
                // 平台公式与几何不变量（计划 §1.2）：所有层都在玩家 ±24 内。
                check(wcore::platform::playerY(-63,-51) == -63,"默认范围挂机Y保持-63");
                check(wcore::platform::playerY(-63,-39) == -63,"25层以内取yMin");
                check(wcore::platform::playerY(-63,-16) == -39,"48层取平台中心");
                check(wcore::platform::onPlatform(-63,-51,-63),"默认范围在平台上");
                check(wcore::platform::onPlatform(-63,-16,-39),"48层在平台上");
                check(!wcore::platform::onPlatform(-63,-14,-38),"超平台范围必须被识别");
            }
            auto upper = window.evaluate(cfg,0,1,platformY,true);
            check(scoreKey(upper) >= scoreKey(actual),"平台Y候选上界");
        }
        Config edge;
        edge.centerX = kWorldMin;
        edge.centerZ = kWorldMax;
        edge.radius = 1;
        validate(edge);
        check(inside(edge,kWorldMin,kWorldMax),"世界边界有效端点");
        check(!inside(edge,kWorldMin-1,kWorldMax),"世界边界越界");
        edge.fullWorld = true;
        validate(edge);
        check(edge.centerX == 0 && edge.radius == 30000000,"全图展开");
        Config c;
        c.minArea = 0;
        c.top = 10;
        c.minDistance = 256;
        Hit a,b,d;
        a.x = 1000;
        a.caveVolume = 10;
        b.x = 10;
        b.riverVolume = 14;
        d = a;
        d.x += 1;
        check(better(a,b,c),"分数精度不能舍入后排名");
        auto selected = selectHotspots({d,b,a},c);
        check(selected.size() == 2 && selected[0].x == 1000,"热点去重与同分距离");
        auto j = configJson(c);
        j["innerRadius"] = Json{{"value",1.5},{"unit","chunks"}};
        j["simulationDistanceChunks"] = Json{{"value",80},{"unit","blocks"}};
        auto parsed = configFromJson(j);
        check(parsed.innerRadius == 24 && parsed.simulationDistanceChunks == 5,"单位换算");
        bool rejected = false;
        try { configFromJson(Json{{"sampleY",-63}}); } catch (const std::exception &) { rejected = true; }
        check(rejected,"旧参数必须报错");
        SearchResult result;
        result.config = c;
        result.hits = selected;
        auto out = resultJson(result);
        auto csv = exportText(result);
        check(out["candidates"][0]["scoreNumerator"].is_string(),"精确分数输出");
        check(csv.csv.find("scoreNumerator") != std::string::npos,"导出综合分");
        std::cout << "geometry, height, scores, units and exports passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
