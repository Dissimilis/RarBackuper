#include "core/Text.h"

namespace core
{

std::wstring Utf8ToWide(std::string_view s)
{
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size())
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80)
        {
            cp = c;
        }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size() && (s[i + 1] & 0xC0) == 0x80)
        {
            cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
            len = 2;
        }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size() && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80)
        {
            cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            len = 3;
        }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size() && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80)
        {
            cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            len = 4;
        }
        i += len;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            cp = 0xFFFD;
        if (cp >= 0x10000)
        {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
        else
        {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

std::string WideToUtf8(std::wstring_view s)
{
    std::string out;
    out.reserve(s.size() * 3);
    size_t i = 0;
    while (i < s.size())
    {
        char32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
        {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i += 2;
        }
        else
        {
            if (cp >= 0xD800 && cp <= 0xDFFF)
                cp = 0xFFFD;
            ++i;
        }
        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

}
