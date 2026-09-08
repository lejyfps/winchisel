#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::wcerr << L"usage: generate_catalogs <repo-root> <golden-out>\n";
        return 2;
    }
    const std::filesystem::path root = argv[1];
    const std::filesystem::path out = argv[2];
    const std::pair<std::filesystem::path, std::string> catalogs[] = {
        {"src/core/src/debloater_catalog.generated.hpp", "generated_debloat_catalog"},
        {"src/core/src/download_catalog.generated.hpp", "generated_download_catalog"},
        {"src/core/src/performance_catalog.generated.hpp", "generated_performance_catalog"},
        {"src/core/src/privacy_catalog.generated.hpp", "generated_privacy_catalog"},
    };
    std::ofstream golden(out, std::ios::binary | std::ios::trunc);
    if (!golden) return 1;
    for (auto const& [relative, symbol] : catalogs) {
        std::ifstream input(root / relative, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const auto begin = text.find(symbol + "{{");
        const auto end = begin == std::string::npos ? begin : text.find("}};", begin);
        if (begin == std::string::npos || end == std::string::npos) {
            std::cerr << symbol << " missing\n";
            return 1;
        }
        const auto body = text.substr(begin, end - begin);
        const std::regex entry(R"re((?:^|\n)\s*\{"([^"]+)")re");
        std::set<std::string> ids;
        for (std::sregex_iterator it(body.begin(), body.end(), entry), last; it != last; ++it) {
            if (!ids.insert((*it)[1].str()).second) {
                std::cerr << "duplicate " << (*it)[1].str() << '\n';
                return 1;
            }
        }
        golden << symbol << ' ' << ids.size() << '\n';
        for (auto const& id : ids) golden << id << '\n';
        golden << '\n';
    }
    return 0;
}
