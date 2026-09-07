#pragma once

#include "winchisel/core/error.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winchisel::platform {

struct ReleaseArtifact {
    std::string id;
    std::string file_name;
    std::string sha256;
    std::uint64_t size{};
};

struct ReleaseManifest {
    std::string version;
    std::vector<ReleaseArtifact> artifacts;
};

// Validates the exact UTF-8 bytes of release.json against release.json.sig.
// Signatures use ECDSA P-256 with SHA-256 and are Base64 encoded.
winchisel::core::Result<ReleaseManifest> verify_release_manifest(
    std::string_view manifest_json, std::string_view signature_base64);

// Performs discovery through GitHub's public latest-release API, then verifies
// the separately downloaded manifest before returning any update metadata.
winchisel::core::Result<ReleaseManifest> check_github_latest_release();

inline constexpr std::string_view k_github_latest_release_api =
    "https://api.github.com/repos/lejyfps/winchisel/releases/latest";

}  // namespace winchisel::platform
