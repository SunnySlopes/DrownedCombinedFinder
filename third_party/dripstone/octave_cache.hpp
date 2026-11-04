#pragma once
// 兼容层：dripstone::OctaveCache 现在是 core/gates/octave_cache.hpp 的别名。
// 这样 river_gate 测试与既有调用点（cloud/world 名称不变）都不必改写。
#include "../../core/gates/octave_cache.hpp"

namespace dripstone {
using OctaveCache = wcore::OctaveCache;
}
