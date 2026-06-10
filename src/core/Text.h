#pragma once
#include <string>
#include <string_view>

namespace core
{
// UTF-8 <-> UTF-16 conversion without Win32 dependencies so core stays
// portable and the test exe stays hermetic. Invalid sequences become U+FFFD.
std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(std::wstring_view s);
}
