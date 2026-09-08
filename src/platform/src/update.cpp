#include "winchisel/platform/update.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <shlobj.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <regex>
#include <span>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")

namespace winchisel::platform {
namespace {

winchisel::core::Result<ReleaseManifest> fail(std::string detail) {
    return std::unexpected(winchisel::core::Error{.code = winchisel::core::ErrorCode::parse, .message_key = "update_manifest_invalid", .detail = std::move(detail)});
}

winchisel::core::Result<std::string> get_https(std::string_view url) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
    std::wstring wide(count, L'\0'); MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), wide.data(), count);
    URL_COMPONENTS parts{.dwStructSize = sizeof(parts)}; std::array<wchar_t, 256> host{}; std::array<wchar_t, 4096> path{};
    parts.lpszHostName = host.data(); parts.dwHostNameLength = static_cast<DWORD>(host.size()); parts.lpszUrlPath = path.data(); parts.dwUrlPathLength = static_cast<DWORD>(path.size());
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_check_failed",.detail="Invalid HTTPS URL"});
    HINTERNET session=WinHttpOpen(L"Winchisel/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0); if(!session) return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_check_failed",.detail="WinHttpOpen failed"});
    std::wstring target(path.data(), parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET connection=WinHttpConnect(session,host.data(),parts.nPort,0); HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",target.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    static constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\n";
    const bool sent=request&&WinHttpSendRequest(request,headers,static_cast<DWORD>(std::size(headers)-1),WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr); DWORD status{}; DWORD size=sizeof(status); if(sent) WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
    std::string output; for(DWORD available{}; sent&&status==200&&WinHttpQueryDataAvailable(request,&available)&&available;){const auto at=output.size();output.resize(at+available);DWORD read{};if(!WinHttpReadData(request,output.data()+at,available,&read)){output.clear();break;}output.resize(at+read);} if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);WinHttpCloseHandle(session);
    if(!sent||status!=200) return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_check_failed",.detail="GitHub returned HTTP "+std::to_string(status)}); return output;
}

std::optional<std::string> asset_url(std::string const& api_json, std::string_view name) {
    const auto marker = std::string("\"name\":\"") + std::string(name) + "\""; const auto position=api_json.find(marker); if(position==std::string::npos)return std::nullopt;
    const auto begin=api_json.rfind('{',position), end=api_json.find('}',position); if(begin==std::string::npos||end==std::string::npos)return std::nullopt; const auto object=api_json.substr(begin,end-begin+1);
    static const std::regex url(R"json("browser_download_url":"([^"]+)")json"); std::smatch match; return std::regex_search(object,match,url)?std::optional<std::string>{match[1].str()}:std::nullopt;
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
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_KEY_HANDLE key{}; BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0 || BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, reinterpret_cast<BYTE*>(const_cast<std::byte*>(public_key.data())), static_cast<ULONG>(public_key.size()), 0) < 0) return false;
    DWORD object_size{}, result{}; BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&object_size), sizeof(object_size), &result, 0);
    std::vector<std::byte> object(object_size), digest(32);
    const bool ok = BCryptCreateHash(algorithm, &hash, reinterpret_cast<BYTE*>(object.data()), object_size, nullptr, 0, 0) >= 0 && BCryptHashData(hash, reinterpret_cast<BYTE*>(const_cast<char*>(json.data())), static_cast<ULONG>(json.size()), 0) >= 0 && BCryptFinishHash(hash, reinterpret_cast<BYTE*>(digest.data()), static_cast<ULONG>(digest.size()), 0) >= 0 && BCryptVerifySignature(key, nullptr, reinterpret_cast<BYTE*>(digest.data()), static_cast<ULONG>(digest.size()), reinterpret_cast<BYTE*>(const_cast<std::byte*>(signature.data())), static_cast<ULONG>(signature.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); if (key) BCryptDestroyKey(key); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); return ok;
}

std::string sha256_hex(std::span<std::byte const> value) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{}; if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD object_size{}, result{}; BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&object_size), sizeof(object_size), &result, 0); std::vector<std::byte> object(object_size), digest(32);
    const bool ok=BCryptCreateHash(algorithm,&hash,reinterpret_cast<BYTE*>(object.data()),object_size,nullptr,0,0)>=0&&BCryptHashData(hash,reinterpret_cast<BYTE*>(const_cast<std::byte*>(value.data())),static_cast<ULONG>(value.size()),0)>=0&&BCryptFinishHash(hash,reinterpret_cast<BYTE*>(digest.data()),static_cast<ULONG>(digest.size()),0)>=0; if(hash)BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0); if(!ok)return {};
    static constexpr char hex[]="0123456789abcdef"; std::string result_hex; result_hex.reserve(64); for(auto byte:digest){const auto v=static_cast<unsigned>(byte);result_hex.push_back(hex[v>>4]);result_hex.push_back(hex[v&15]);} return result_hex;
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
    const auto artifact=std::ranges::find_if(manifest.artifacts,[artifact_id](auto const& value){return value.id==artifact_id;}); if(artifact==manifest.artifacts.end()) return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::not_found,.message_key="update_artifact_missing",.detail=std::string(artifact_id)});
    const auto url=std::string("https://github.com/lejyfps/winchisel/releases/download/v")+manifest.version+"/"+artifact->file_name; auto downloaded=get_https(url); if(!downloaded)return std::unexpected(downloaded.error()); if(downloaded->size()!=artifact->size)return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_hash_mismatch",.detail="Unexpected download size"});
    const auto digest=sha256_hex(std::as_bytes(std::span{downloaded->data(),downloaded->size()})); if(digest!=artifact->sha256)return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_hash_mismatch",.detail="SHA-256 mismatch"});
    PWSTR raw{}; if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw)))return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_stage_failed",.detail="LocalAppData unavailable"}); std::filesystem::path dir=raw;CoTaskMemFree(raw);dir/=L"Winchisel";dir/=L"updates";dir/=std::wstring(manifest.version.begin(),manifest.version.end());std::error_code error;std::filesystem::create_directories(dir,error);if(error)return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_stage_failed",.detail=error.message()});
    const auto final=dir/std::filesystem::path(artifact->file_name);const auto partial=final.wstring()+L".partial";std::ofstream output(std::filesystem::path(partial),std::ios::binary|std::ios::trunc);output.write(downloaded->data(),static_cast<std::streamsize>(downloaded->size()));output.close();if(!output)return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_stage_failed",.detail="Write failed"}); if(!MoveFileExW(partial.c_str(),final.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return std::unexpected(winchisel::core::Error{.code=winchisel::core::ErrorCode::io,.message_key="update_stage_failed",.detail=std::to_string(GetLastError())});return final;
}

} // namespace winchisel::platform
