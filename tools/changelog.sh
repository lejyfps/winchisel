#!/usr/bin/env bash
# Builds Markdown release notes from commit subjects since the previous release.
#
# Usage: tools/changelog.sh <new-tag> [<commit>]
#   <new-tag>  tag being released (v1.2.3, v1.2.3.4 or v1.2.3-nightly.YYYYMMDD.N)
#   <commit>   commit the release points at (default: HEAD)
#
# Previous release:
#   stable  -> newest older STABLE tag (nightlies in between are folded in)
#   nightly -> newest older tag of any kind
# Commits are grouped by their leading verb; repo plumbing (CI, .gitignore,
# funding, workflows) goes into a collapsed "Maintenance" section.
set -euo pipefail

tag="${1:?usage: changelog.sh <new-tag> [<commit>]}"
head="${2:-HEAD}"
repo="${GITHUB_REPOSITORY:-lejyfps/winchisel}"

if [[ "$tag" == *-* ]]; then
  candidates=$(git tag -l 'v*' --sort=-v:refname)
else
  candidates=$(git tag -l 'v*' --sort=-v:refname | grep -v -- '-' || true)
fi
prev=""
while IFS= read -r t; do
  [ -z "$t" ] || [ "$t" = "$tag" ] && continue
  # Only tags that are ancestors of the release commit count as "previous".
  if git merge-base --is-ancestor "$t" "$head" 2>/dev/null; then prev="$t"; break; fi
done <<<"$candidates"

range="$head"
[ -n "$prev" ] && range="$prev..$head"

features=(); fixes=(); security=(); improvements=(); maintenance=()
while IFS=$'\t' read -r sha subject; do
  [ -z "$sha" ] && continue
  line="- $subject ([\`$sha\`](https://github.com/$repo/commit/$sha))"
  lower="${subject,,}"
  # bash ERE has no \b, so "ci" is matched as a whole word explicitly.
  if [[ "$lower" =~ (\.gitignore|workflow|(^|[^a-z])ci([^a-z]|$)|funding|dependabot|winget token|release tooling|helper script) ]]; then
    maintenance+=("$line")
  elif [[ "$lower" =~ ^(add|introduce|support|expose|show|enable) ]]; then
    features+=("$line")
  elif [[ "$lower" =~ ^(fix|correct|repair|prevent|handle|resolve) ]]; then
    fixes+=("$line")
  elif [[ "$lower" =~ ^(harden|restrict|secure|validate|sanitize) ]]; then
    security+=("$line")
  else
    improvements+=("$line")
  fi
done < <(git log --no-merges --reverse --pretty=$'%h\t%s' "$range")

section() {
  local title="$1"; shift
  [ "$#" -eq 0 ] && return
  printf '### %s\n\n' "$title"
  printf '%s\n' "$@"
  printf '\n'
}

{
  printf '## What'"'"'s new in %s\n\n' "$tag"
  section "✨ Features" "${features[@]+"${features[@]}"}"
  section "🐛 Fixes" "${fixes[@]+"${fixes[@]}"}"
  section "🔒 Security & hardening" "${security[@]+"${security[@]}"}"
  section "🔧 Improvements" "${improvements[@]+"${improvements[@]}"}"
  if [ "${#maintenance[@]}" -gt 0 ]; then
    printf '<details><summary>Maintenance (%d)</summary>\n\n' "${#maintenance[@]}"
    printf '%s\n' "${maintenance[@]}"
    printf '\n</details>\n\n'
  fi
  printf -- '---\n\n'
  printf '**Install:** `winget install lejy.Winchisel` · or download `Winchisel-Setup-x64.exe` (installer) / `Winchisel-Portable-x64.exe` (no install) below.\n\n'
  if [ -n "$prev" ]; then
    printf '**Full changelog:** https://github.com/%s/compare/%s...%s\n' "$repo" "$prev" "$tag"
  fi
}
