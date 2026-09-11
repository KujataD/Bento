#pragma once
#include "../runtime/KujataApi.h"
#include <Windows.h>
#include <cstdint>
#include <string>

namespace KujataEngine {

namespace StringUtil {

KUJATA_API std::wstring ToWString(const std::string& str);

KUJATA_API std::string ToString(const std::wstring& str);

} // namespace StringUtil

} // namespace KujataEngine