#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace {
std::string read(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool validate(std::filesystem::path const& root, std::filesystem::path const& relative, std::string const& symbol, std::ostream* golden) {
    const auto text = read(root / relative);
    const std::regex declaration("array<[^,]+,\\s*(\\d+)>\\s+" + symbol);
    std::smatch declared;
    if (!std::regex_search(text, declared, declaration)) {
        std::cerr << symbol << ": missing declaration\n";
        return false;
    }
    const auto begin = text.find(symbol + "{{");
    const auto end = begin == std::string::npos ? begin : text.find("}};", begin);
    if (begin == std::string::npos || end == std::string::npos) {
        std::cerr << symbol << ": missing body\n";
        return false;
    }
    const auto body = text.substr(begin, end - begin);
    const std::regex entry(R"re((?:^|\n)\s*\{"([^"]+)")re");
    std::set<std::string> ids;
    std::size_t count{};
    for (std::sregex_iterator it(body.begin(), body.end(), entry), last; it != last; ++it) {
        ++count;
        if (!ids.insert((*it)[1].str()).second) {
            std::cerr << symbol << ": duplicate ID " << (*it)[1].str() << '\n';
            return false;
        }
    }
    const auto expected = static_cast<std::size_t>(std::stoull(declared[1].str()));
    if (count != expected) {
        std::cerr << symbol << ": declared " << expected << ", found " << count << '\n';
        return false;
    }
    std::cout << symbol << ": " << count << " unique entries\n";
    if (golden) {
        *golden << symbol << ' ' << ids.size() << '\n';
        for (auto const& id : ids) *golden << id << '\n';
        *golden << '\n';
    }
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    const std::filesystem::path root = argv[1];
    std::ofstream golden_file;
    std::ostream* golden = nullptr;
    if (argc == 3) {
        golden_file.open(argv[2], std::ios::binary | std::ios::trunc);
        if (!golden_file) return 1;
        golden = &golden_file;
    }
    bool ok = true;
    ok = validate(root, "src/core/src/debloater_catalog.generated.hpp", "generated_debloat_catalog", golden) && ok;
    ok = validate(root, "src/core/src/download_catalog.generated.hpp", "generated_download_catalog", golden) && ok;
    ok = validate(root, "src/core/src/performance_catalog.generated.hpp", "generated_performance_catalog", golden) && ok;
    ok = validate(root, "src/core/src/privacy_catalog.generated.hpp", "generated_privacy_catalog", golden) && ok;
    const auto downloads = read(root / "src/core/src/download_catalog.generated.hpp");
    if (downloads.find("\"http://") != std::string::npos) {
        std::cerr << "download catalog contains an insecure URL\n";
        ok = false;
    }
    const auto latency = read(root / "src/platform/src/latency_database.generated.hpp");
    if (latency.find("inline constexpr std::array") == std::string::npos) {
        std::cerr << "latency database is missing\n";
        ok = false;
    }
    return ok ? 0 : 1;
}
