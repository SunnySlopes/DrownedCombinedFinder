#pragma once
#include "engine.hpp"
#include "json.hpp"

namespace riverfinder_native {
using Json = nlohmann::json;

Json configJson(const Config &cfg);
Config configFromJson(const Json &value);
Json statisticsJson(const Statistics &stats);
Json hitJson(const Hit &hit);
Json resultJson(const SearchResult &result);
Json candidateJson(const SearchResult &result, size_t index);
void atomicJson(const std::filesystem::path &path, const Json &value);

struct ExportText {
    std::string csv;
    std::string txt;
};
ExportText exportText(const SearchResult &result);
Json exportResults(const std::filesystem::path &prefix, SearchResult &result);

std::filesystem::path utf8Path(const std::string &value);
std::string pathUtf8(const std::filesystem::path &path);
}
