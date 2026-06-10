#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace core
{

struct ParsedEvent
{
    enum class Kind
    {
        Line,   // any non-empty output line (text holds the cleaned line)
        Adding, // an "Adding <file>" line; path holds the extracted file path
    };
    Kind kind = Kind::Line;
    std::wstring text;
    std::wstring path;
};

// Incremental parser for redirected Rar.exe output. Pipe reads arrive in
// arbitrary chunks, so the parser buffers bytes until a full line (CRLF or
// LF) is available. Lines are expected as UTF-8 (the runner passes -scR).
class RarOutputParser
{
public:
    std::vector<ParsedEvent> Feed(std::string_view chunk);

    // Flush any trailing line that lacked a newline (call after EOF).
    std::vector<ParsedEvent> Finish();

private:
    void EmitLine(std::string rawLine, std::vector<ParsedEvent>& out);

    std::string buffer_;
};

}
