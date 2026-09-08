#include "winchisel/platform/update.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <regex>
#include <span>
#include <sstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")

namespace winchisel::platform {
namespace {

winchisel::core::Result<ReleaseManifest> fail(std::string detail) {
    return std::unexpected(winchisel::core::Error{.detail = std::move(detail)});
}

winchisel::core::Result<std::string> get_https(std::string_view url, std::uint64_t maximum_size = 4 * 1024 * 1024) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
    std::wstring wide(count, L'\0'); MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), wide.data(), count);
    URL_COMPONENTS parts{.dwStructSize = sizeof(parts)}; std::array<wchar_t, 256> host{}; std::array<wchar_t, 4096> path{};
    parts.lpszHostName = host.data(); parts.dwHostNameLength = static_cast<DWORD>(host.size()); parts.lpszUrlPath = path.data(); parts.dwUrlPathLength = static_cast<DWORD>(path.size());
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return std::unexpected(winchisel::core::Error{.detail="Invalid HTTPS URL"});
    HINTERNET session=WinHttpOpen(L"Winchisel/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0); if(!session) return std::unexpected(winchisel::core::Error{.detail="WinHttpOpen failed"});
    WinHttpSetTimeouts(session, 10'000, 10'000, 30'000, 30'000);
    std::wstring target(path.data(), parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET connection=WinHttpConnect(session,host.data(),parts.nPort,0); HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",target.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    static constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\n";
    const bool sent=request&&WinHttpSendRequest(request,headers,static_cast<DWORD>(std::size(headers)-1),WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr); DWORD status{}; DWORD size=sizeof(status); if(sent) WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
    std::string output; bool too_large{}; bool transport{}; const auto deadline=GetTickCount64()+120'000;
    if(sent&&status==200){for(;;){if(GetTickCount64()>deadline){transport=true;break;}DWORD available{};if(!WinHttpQueryDataAvailable(request,&available)){transport=true;break;}if(!available)break;if(output.size()>maximum_size||available>maximum_size-output.size()){too_large=true;break;}const auto at=output.size();output.resize(at+available);DWORD read{};if(!WinHttpReadData(request,output.data()+at,available,&read)){transport=true;output.resize(at);break;}output.resize(at+read);}}
    if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);WinHttpCloseHandle(session);
    if(too_large)return std::unexpected(winchisel::core::Error{.detail="Response exceeded declared size limit"});
    if(transport)return std::unexpected(winchisel::core::Error{.detail="HTTPS transfer failed"});
    if(!sent||status!=200) return std::unexpected(winchisel::core::Error{.detail="GitHub returned HTTP "+std::to_string(status)}); return output;
}

std::optional<std::string> asset_url(std::string const& api_json, std::string_view name) {
    const std::regex marker("\\\"name\\\"\\s*:\\s*\\\"" + std::string(name) + "\\\""); std::smatch name_match; if(!std::regex_search(api_json,name_match,marker))return std::nullopt; const auto position=static_cast<std::size_t>(name_match.position());
    const auto remainder=api_json.substr(position); static const std::regex url(R"json("browser_download_url"\s*:\s*"([^"]+)")json"); std::smatch match; return std::regex_search(remainder,match,url)?std::optional<std::string>{match[1].str()}:std::nullopt;
}

std::vector<std::byte> decode_base64(std::string_view value) {
    DWORD size{};
    if (!CryptStringToBinaryA(value.data(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr)) return {};
    std::vector<std::byte> output(size);
    if (!CryptStringToBinaryA(value.data(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(output.data()), &size, nullptr, nullptr)) return {};
    return output;
}

bool verify_signature(std::string_view json, std::string_view signature_text) {
    // BCRYPT_ECCPUBLIC_BLOB: header followed by P-256 X and Y coordinates.
    static constexpr std::array<std::byte, 72> public_key{
        std::byte{0x45}, std::byte{0x43}, std::byte{0x53}, std::byte{0x31}, std::byte{0x20}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x45}, std::byte{0xAF}, std::byte{0x42}, std::byte{0x47},
        std::byte{0x0F}, std::byte{0x10}, std::byte{0x9F}, std::byte{0x23}, std::byte{0xF3}, std::byte{0x97}, std::byte{0x11}, std::byte{0xD5}, std::byte{0x88}, std::byte{0xD2}, std::byte{0x88}, std::byte{0x5C},
        std::byte{0x4E}, std::byte{0x9A}, std::byte{0x29}, std::byte{0x72}, std::byte{0xCE}, std::byte{0x9D}, std::byte{0x22}, std::byte{0x9B}, std::byte{0xCD}, std::byte{0xF5}, std::byte{0x0B}, std::byte{0xF9},
        std::byte{0x40}, std::byte{0x85}, std::byte{0x03}, std::byte{0x68}, std::byte{0x99}, std::byte{0x35}, std::byte{0xF0}, std::byte{0x40}, std::byte{0x41}, std::byte{0x82}, std::byte{0xE2}, std::byte{0xF1},
        std::byte{0x69}, std::byte{0xAA}, std::byte{0x81}, std::byte{0x23}, std::byte{0x28}, std::byte{0x87}, std::byte{0x81}, std::byte{0x27}, std::byte{0xDD}, std::byte{0x0B}, std::byte{0xE8}, std::byte{0x0C},
        std::byte{0xFA}, std::byte{0x38}, std::byte{0x05}, std::byte{0xB1}, std::byte{0x61}, std::byte{0x7F}, std::byte{0xB7}, std::byte{0xD4}, std::byte{0x21}, std::byte{0xF5}, std::byte{0x5C}, std::byte{0xEB},
    };
    const auto signature = decode_base64(signature_text); if (signature.empty()) return false;
    BCRYPT_ALG_HANDLE algorithm{}, hash_algorithm{}; BCRYPT_KEY_HANDLE key{}; BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;
    if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
            reinterpret_cast<BYTE*>(const_cast<std::byte*>(public_key.data())),
            static_cast<ULONG>(public_key.size()), 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    DWORD object_size{}, result{};
    if (BCryptOpenAlgorithmProvider(&hash_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(hash_algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&object_size),
            sizeof(object_size), &result, 0) < 0 || object_size == 0) {
        BCryptDestroyKey(key);
        if (hash_algorithm) BCryptCloseAlgorithmProvider(hash_algorithm, 0);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    std::vector<std::byte> object(object_size), digest(32);
    const bool ok = BCryptCreateHash(hash_algorithm, &hash, reinterpret_cast<BYTE*>(object.data()), object_size, nullptr, 0, 0) >= 0 && BCryptHashData(hash, reinterpret_cast<BYTE*>(const_cast<char*>(json.data())), static_cast<ULONG>(json.size()), 0) >= 0 && BCryptFinishHash(hash, reinterpret_cast<BYTE*>(digest.data()), static_cast<ULONG>(digest.size()), 0) >= 0 && BCryptVerifySignature(key, nullptr, reinterpret_cast<BYTE*>(digest.data()), static_cast<ULONG>(digest.size()), reinterpret_cast<BYTE*>(const_cast<std::byte*>(signature.data())), static_cast<ULONG>(signature.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); if (hash_algorithm) BCryptCloseAlgorithmProvider(hash_algorithm, 0); if (key) BCryptDestroyKey(key); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); return ok;
}

std::string to_hex(std::span<std::byte const> digest) {
    static constexpr char hex[]="0123456789abcdef"; std::string result_hex; result_hex.reserve(digest.size()*2);
    for (auto byte : digest) { const auto v=static_cast<unsigned>(byte); result_hex.push_back(hex[v>>4]); result_hex.push_back(hex[v&15]); }
    return result_hex;
}

struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::vector<std::byte> object;
    bool ok{};
    Sha256() {
        DWORD object_size{}, result{};
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&object_size), sizeof(object_size), &result, 0) < 0 || !object_size) return;
        object.resize(object_size);
        ok = BCryptCreateHash(algorithm, &hash, reinterpret_cast<BYTE*>(object.data()), object_size, nullptr, 0, 0) >= 0;
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    bool update(void const* data, ULONG size) {
        return ok && BCryptHashData(hash, static_cast<BYTE*>(const_cast<void*>(data)), size, 0) >= 0;
    }
    std::string finish() {
        std::array<std::byte, 32> digest{};
        if (!ok || BCryptFinishHash(hash, reinterpret_cast<BYTE*>(digest.data()), static_cast<ULONG>(digest.size()), 0) < 0) return {};
        ok = false;
        return to_hex(digest);
    }
};

std::string sha256_hex(std::span<std::byte const> value) {
    Sha256 hasher;
    if (!hasher.update(value.data(), static_cast<ULONG>(value.size()))) return {};
    return hasher.finish();
}

winchisel::core::Result<void> download_https_file(std::string_view url, std::filesystem::path const& destination, std::uint64_t expected_size, std::string_view expected_sha256) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
    std::wstring wide(count, L'\0'); MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), wide.data(), count);
    URL_COMPONENTS parts{.dwStructSize = sizeof(parts)}; std::array<wchar_t, 256> host{}; std::array<wchar_t, 4096> path{};
    parts.lpszHostName = host.data(); parts.dwHostNameLength = static_cast<DWORD>(host.size()); parts.lpszUrlPath = path.data(); parts.dwUrlPathLength = static_cast<DWORD>(path.size());
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return std::unexpected(winchisel::core::Error{.detail="Invalid HTTPS URL"});
    HINTERNET session=WinHttpOpen(L"Winchisel/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0); if(!session) return std::unexpected(winchisel::core::Error{.detail="WinHttpOpen failed"});
    WinHttpSetTimeouts(session, 10'000, 10'000, 30'000, 30'000);
    std::wstring target(path.data(), parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET connection=WinHttpConnect(session,host.data(),parts.nPort,0); HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",target.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    static constexpr wchar_t headers[] = L"Accept: application/octet-stream\r\n";
    const bool sent=request&&WinHttpSendRequest(request,headers,static_cast<DWORD>(std::size(headers)-1),WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr); DWORD status{}; DWORD size=sizeof(status); if(sent) WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
    auto close=[&]{ if(request)WinHttpCloseHandle(request); if(connection)WinHttpCloseHandle(connection); WinHttpCloseHandle(session); };
    if(!sent||status!=200){ close(); return std::unexpected(winchisel::core::Error{.detail="GitHub returned HTTP "+std::to_string(status)}); }
    std::ofstream output(destination, std::ios::binary|std::ios::trunc);
    if(!output){ close(); return std::unexpected(winchisel::core::Error{.detail="Write failed"}); }
    Sha256 hasher;
    std::array<char, 65536> buffer{};
    std::uint64_t total{};
    bool failed{};
    bool transport{};
    const auto deadline=GetTickCount64()+120'000;
    for(;;){
        if(GetTickCount64()>deadline){transport=true;break;}
        DWORD available{};
        if(!WinHttpQueryDataAvailable(request,&available)){transport=true;break;}
        if(!available)break;
        while(available){
            const DWORD chunk=static_cast<DWORD>(std::min<std::uint64_t>(available, buffer.size()));
            if(total+chunk>expected_size){ failed=true; break; }
            DWORD read{};
            if(!WinHttpReadData(request,buffer.data(),chunk,&read)){transport=true;break;}
            if(!read){transport=true;break;}
            if(!hasher.update(buffer.data(), read) || !output.write(buffer.data(), static_cast<std::streamsize>(read))){ failed=true; break; }
            total+=read; available-=read;
        }
        if(failed||transport) break;
    }
    output.close();
    close();
    auto remove_partial=[&]{ std::error_code error; std::filesystem::remove(destination, error); };
    if(transport){ remove_partial(); return std::unexpected(winchisel::core::Error{.detail="HTTPS transfer failed"}); }
    if(failed||!output||total!=expected_size){ remove_partial(); return std::unexpected(winchisel::core::Error{.detail=failed&&total+1>expected_size?"Response exceeded declared size limit":"Unexpected download size"}); }
    const auto digest=hasher.finish();
    if(digest!=expected_sha256){ remove_partial(); return std::unexpected(winchisel::core::Error{.detail="SHA-256 mismatch"}); }
    return {};
}

std::optional<std::wstring> portable_host() {
    const auto size = GetEnvironmentVariableW(L"WINCHISEL_PORTABLE_HOST", nullptr, 0);
    if (size > 1) {
        std::wstring host(size, L'\0');
        if (GetEnvironmentVariableW(L"WINCHISEL_PORTABLE_HOST", host.data(), size)) {
            if (host.back() == L'\0') host.pop_back();
            return host;
        }
    }
    int count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return std::nullopt;
    std::optional<std::wstring> host;
    for (int index = 1; index + 1 < count; ++index) {
        if (std::wstring_view(arguments[index]) == L"--portable-host") {
            host = arguments[index + 1];
            break;
        }
    }
    LocalFree(arguments);
    return host;
}

} // namespace

winchisel::core::Result<ReleaseManifest> verify_release_manifest(std::string_view json, std::string_view signature) {
    if (!verify_signature(json, signature)) return fail("Signature verification failed");
    static const std::regex version(R"json("version"\s*:\s*"([0-9]+\.[0-9]+\.[0-9]+)")json");
    static const std::regex artifact(R"json(\{\s*"id"\s*:\s*"([a-z0-9-]+)"\s*,\s*"file"\s*:\s*"([A-Za-z0-9._-]+)"\s*,\s*"sha256"\s*:\s*"([a-fA-F0-9]{64})"\s*,\s*"size"\s*:\s*([0-9]+)\s*\})json");
    std::match_results<std::string_view::const_iterator> version_match; if (!std::regex_search(json.begin(), json.end(), version_match, version)) return fail("Missing semantic version");
    ReleaseManifest result{.version = version_match[1].str()};
    using Iterator = std::string_view::const_iterator;
    for (std::regex_iterator<Iterator> it(json.begin(), json.end(), artifact), end; it != end; ++it) result.artifacts.push_back({.id=(*it)[1].str(), .file_name=(*it)[2].str(), .sha256=(*it)[3].str(), .size=std::stoull((*it)[4].str())});
    return result.artifacts.empty() ? fail("No artifacts") : winchisel::core::Result<ReleaseManifest>{std::move(result)};
}

winchisel::core::Result<ReleaseManifest> check_github_latest_release() {
    auto release=get_https(k_github_latest_release_api); if(!release)return std::unexpected(release.error()); auto manifest_url=asset_url(*release,"release.json"), signature_url=asset_url(*release,"release.json.sig"); if(!manifest_url||!signature_url)return fail("Release is missing manifest assets"); auto manifest=get_https(*manifest_url), signature=get_https(*signature_url); if(!manifest)return std::unexpected(manifest.error()); if(!signature)return std::unexpected(signature.error()); return verify_release_manifest(*manifest,*signature);
}

winchisel::core::Result<std::filesystem::path> stage_release_artifact(ReleaseManifest const& manifest, std::string_view artifact_id) {
    const auto artifact=std::ranges::find_if(manifest.artifacts,[artifact_id](auto const& value){return value.id==artifact_id;}); if(artifact==manifest.artifacts.end()) return std::unexpected(winchisel::core::Error{.detail=std::string(artifact_id)});
    const auto url=std::string("https://github.com/lejyfps/winchisel/releases/download/v")+manifest.version+"/"+artifact->file_name; if(!artifact->size||artifact->size>2ULL*1024*1024*1024)return std::unexpected(winchisel::core::Error{.detail="Invalid artifact size"});
    PWSTR raw{}; if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw)))return std::unexpected(winchisel::core::Error{.detail="LocalAppData unavailable"}); std::filesystem::path dir=raw;CoTaskMemFree(raw);dir/=L"Winchisel";dir/=L"updates";dir/=std::wstring(manifest.version.begin(),manifest.version.end());std::error_code error;std::filesystem::create_directories(dir,error);if(error)return std::unexpected(winchisel::core::Error{.detail=error.message()});
    const auto final=dir/std::filesystem::path(artifact->file_name);const auto partial=std::filesystem::path(final.wstring()+L".partial");
    std::filesystem::remove(partial, error);
    if (auto downloaded = download_https_file(url, partial, artifact->size, artifact->sha256); !downloaded) return std::unexpected(downloaded.error());
    if(!MoveFileExW(partial.c_str(),final.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){ std::filesystem::remove(partial, error); return std::unexpected(winchisel::core::Error{.detail=std::to_string(GetLastError())}); }
    return final;
}

std::string current_app_version() {
    std::array<wchar_t, 32768> module{};
    const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length == module.size()) return "0.0.0";
    std::ifstream input(std::filesystem::path(std::wstring(module.data(), length)).parent_path() / L"version.txt");
    std::string version;
    std::getline(input, version);
    return version.empty() ? "0.0.0" : version;
}

bool is_newer_version(std::string_view candidate, std::string_view current) {
    auto parse = [](std::string_view value) -> std::optional<std::array<unsigned long, 3>> {
        std::array<unsigned long, 3> parts{};
        for (std::size_t index{}; index < parts.size(); ++index) {
            const auto separator = value.find('.');
            const auto token = value.substr(0, separator);
            if (token.empty() || !std::ranges::all_of(token, [](unsigned char c) { return std::isdigit(c) != 0; })) return std::nullopt;
            try { parts[index] = std::stoul(std::string(token)); } catch (...) { return std::nullopt; }
            if (index + 1 < parts.size()) {
                if (separator == std::string_view::npos) return std::nullopt;
                value.remove_prefix(separator + 1);
            } else if (separator != std::string_view::npos) return std::nullopt;
        }
        return parts;
    };
    const auto next = parse(candidate), installed = parse(current);
    return next && installed && *next > *installed;
}

bool is_portable_install() {
    return portable_host().has_value();
}

std::string_view update_artifact_id() { return is_portable_install() ? "portable-x64" : "setup-x64"; }

winchisel::core::Result<void> launch_staged_update(
    std::filesystem::path const& staged, ReleaseArtifact const& artifact) {
    if (!is_portable_install()) {
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", staged.c_str(), nullptr,
            staged.parent_path().c_str(), SW_SHOWNORMAL));
        if (result <= 32) return std::unexpected(winchisel::core::Error{.detail=std::to_string(result)});
        return {};
    }
    const auto host = portable_host();
    if (!host) return std::unexpected(winchisel::core::Error{.detail="Portable host unavailable"});
    std::array<wchar_t, 32768> module{}; const auto length=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
    const auto updater=std::filesystem::path(std::wstring(module.data(),length)).parent_path()/L"Winchisel.Updater.exe";
    std::wstring command=L"\""+updater.wstring()+L"\" --staged \""+staged.wstring()+L"\" --target \""+*host+
        L"\" --sha256 "+std::wstring(artifact.sha256.begin(),artifact.sha256.end())+L" --wait-pid "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{.cb=sizeof(startup)}; PROCESS_INFORMATION process{};
    if(!CreateProcessW(updater.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,updater.parent_path().c_str(),&startup,&process))
        return std::unexpected(winchisel::core::Error{.detail=std::to_string(GetLastError())});
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return {};
}

} // namespace winchisel::platform
