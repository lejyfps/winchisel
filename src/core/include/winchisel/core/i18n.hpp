#pragma once

#include "settings.hpp"

#include <string>
#include <string_view>

namespace winchisel::core {

void set_ui_language(Language language);
Language ui_language();

// English source string is the lookup key. German returns the translation.
std::wstring loc(std::wstring_view english);
std::string loc_utf8(std::string_view english);

}  // namespace winchisel::core
