#include "core/RarOutputParser.h"
#include "core/Text.h"

namespace core
{

namespace
{

// Apply backspace characters the way a terminal would: each \b removes the
// previous character. RAR's percentage indicator repaints itself this way.
std::string ApplyBackspaces(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        if (c == '\b')
        {
            if (!out.empty())
                out.pop_back();
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

bool IsDigits(std::wstring_view s)
{
    if (s.empty())
        return false;
    for (wchar_t c : s)
        if (c < L'0' || c > L'9')
            return false;
    return true;
}

void TrimRight(std::wstring& s)
{
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r'))
        s.pop_back();
}

// Strip trailing "OK" / "NN%" status artifacts from an Adding line remainder.
void StripTrailingArtifacts(std::wstring& s)
{
    for (;;)
    {
        TrimRight(s);
        size_t ws = s.find_last_of(L" \t");
        if (ws == std::wstring::npos)
            return; // nothing before the token -> the whole string is the path
        std::wstring_view token = std::wstring_view(s).substr(ws + 1);
        bool isOk = token == L"OK";
        bool isPercent = token.size() >= 2 && token.back() == L'%' &&
                         IsDigits(token.substr(0, token.size() - 1));
        if (!isOk && !isPercent)
            return;
        s.erase(ws);
    }
}

}

std::vector<ParsedEvent> RarOutputParser::Feed(std::string_view chunk)
{
    buffer_.append(chunk.data(), chunk.size());
    std::vector<ParsedEvent> out;
    size_t pos;
    while ((pos = buffer_.find('\n')) != std::string::npos)
    {
        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 1);
        EmitLine(std::move(line), out);
    }
    return out;
}

std::vector<ParsedEvent> RarOutputParser::Finish()
{
    std::vector<ParsedEvent> out;
    if (!buffer_.empty())
    {
        std::string line;
        line.swap(buffer_);
        EmitLine(std::move(line), out);
    }
    return out;
}

void RarOutputParser::EmitLine(std::string rawLine, std::vector<ParsedEvent>& out)
{
    std::wstring line = Utf8ToWide(ApplyBackspaces(rawLine));
    TrimRight(line);
    if (line.empty())
        return;

    ParsedEvent ev;
    size_t start = line.find_first_not_of(L" \t");
    if (start != std::wstring::npos &&
        line.compare(start, 6, L"Adding") == 0 &&
        start + 6 < line.size() && (line[start + 6] == L' ' || line[start + 6] == L'\t'))
    {
        std::wstring rest = line.substr(start + 6);
        size_t pathStart = rest.find_first_not_of(L" \t");
        if (pathStart != std::wstring::npos)
        {
            std::wstring path = rest.substr(pathStart);
            StripTrailingArtifacts(path);
            if (!path.empty())
            {
                ev.kind = ParsedEvent::Kind::Adding;
                ev.path = std::move(path);
            }
        }
    }
    ev.text = std::move(line);
    out.push_back(std::move(ev));
}

}
