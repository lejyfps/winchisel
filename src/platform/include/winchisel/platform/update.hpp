#pragma once

#include "winchisel/core/error.hpp"

#include <cstdint>
#include <filesystem>
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

// Downloads exactly one signed-manifest artifact into a private staging folder.
// The returned file has passed size and SHA-256 validation.
winchisel::core::Result<std::filesystem::path> stage_release_artifact(
    ReleaseManifest const& manifest, std::string_view artifact_id);

std::string current_app_version();
bool is_newer_version(std::string_view candidate, std::string_view current);
bool is_portable_install();
// True when running with package identity (Microsoft Store / MSIX). Packaged
// builds must update through the Store and never self-install GitHub builds.
bool is_packaged_install();
// Opens the Store downloads/updates page. Downloads and runs no code, so it
// stays Store-policy compliant.
bool open_store_updates_page();
std::string_view update_artifact_id();
winchisel::core::Result<void> launch_staged_update(
    std::filesystem::path const& staged, ReleaseArtifact const& artifact);

inline constexpr std::string_view k_github_latest_release_api =
    "https://api.github.com/repos/lejyfps/winchisel/releases/latest";

}  // namespace winchisel::platform
