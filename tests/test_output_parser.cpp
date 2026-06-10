#include <doctest/doctest.h>
#include "core/RarOutputParser.h"

using namespace core;

static std::vector<ParsedEvent> feedAll(RarOutputParser& p, std::initializer_list<const char*> chunks)
{
    std::vector<ParsedEvent> out;
    for (const char* c : chunks)
    {
        auto ev = p.Feed(c);
        out.insert(out.end(), ev.begin(), ev.end());
    }
    auto last = p.Finish();
    out.insert(out.end(), last.begin(), last.end());
    return out;
}

TEST_CASE("complete CRLF lines are split into events")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"line one\r\nline two\r\n"});
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].kind == ParsedEvent::Kind::Line);
    CHECK(ev[0].text == L"line one");
    CHECK(ev[1].text == L"line two");
}

TEST_CASE("LF-only lines work too")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"a\nb\n"});
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].text == L"a");
    CHECK(ev[1].text == L"b");
}

TEST_CASE("lines split across arbitrary chunk boundaries are reassembled")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"Add", "ing    C:\\src\\fi", "le.txt  OK\r", "\nDone\r\n"});
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].kind == ParsedEvent::Kind::Adding);
    CHECK(ev[0].path == L"C:\\src\\file.txt");
    CHECK(ev[1].kind == ParsedEvent::Kind::Line);
    CHECK(ev[1].text == L"Done");
}

TEST_CASE("Adding line with leading whitespace and trailing OK extracts the path")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"Adding    C:\\data\\report final.docx                 OK\r\n"});
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == ParsedEvent::Kind::Adding);
    CHECK(ev[0].path == L"C:\\data\\report final.docx");
}

TEST_CASE("Adding line with percentage artifact extracts the path")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"Adding    C:\\data\\x.bin                 42%\r\n"});
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == ParsedEvent::Kind::Adding);
    CHECK(ev[0].path == L"C:\\data\\x.bin");
}

TEST_CASE("Adding line with backspace progress artifacts is tolerated")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"Adding    C:\\data\\y.bin   12%\b\b\b\b 99%  OK\r\n"});
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == ParsedEvent::Kind::Adding);
    CHECK(ev[0].path == L"C:\\data\\y.bin");
}

TEST_CASE("recovery-record and comment notices are NOT file Adding events")
{
    // Real Rar 7.12 output: these start with "Adding" but are not files.
    // File lines always carry a trailing OK / percentage status.
    RarOutputParser p;
    auto ev = feedAll(p, {"Adding the data recovery record     \r\n"
                          "Adding a comment to probe\\out2.rar\r\n"});
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].kind == ParsedEvent::Kind::Line);
    CHECK(ev[1].kind == ParsedEvent::Kind::Line);
}

TEST_CASE("Adding line for a directory is an Adding event (RAR prints them too)")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"Adding    C:\\data\\sub                                       OK \r\n"});
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == ParsedEvent::Kind::Adding);
    CHECK(ev[0].path == L"C:\\data\\sub");
}

TEST_CASE("non-Adding lines pass through as plain log lines")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"RAR 7.00   Copyright (c) 1993-2024\r\nCreating archive D:\\b\\a.rar\r\n"});
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].kind == ParsedEvent::Kind::Line);
    CHECK(ev[1].kind == ParsedEvent::Kind::Line);
}

TEST_CASE("trailing line without newline is delivered by Finish")
{
    RarOutputParser p;
    auto first = p.Feed("partial");
    CHECK(first.empty());
    auto last = p.Finish();
    REQUIRE(last.size() == 1);
    CHECK(last[0].text == L"partial");
}

TEST_CASE("empty lines are skipped")
{
    RarOutputParser p;
    auto ev = feedAll(p, {"\r\n\r\na\r\n\r\n"});
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].text == L"a");
}

TEST_CASE("UTF-8 content in lines survives, even split mid-codepoint")
{
    RarOutputParser p;
    // "Adding    C:\dāta\ž.txt" in UTF-8, split in the middle of the 2-byte ā
    std::string full = "Adding    C:\\d\xC4\x81ta\\\xC5\xBE.txt  OK\r\n";
    auto part1 = full.substr(0, 16); // cuts inside \xC4\x81
    auto part2 = full.substr(16);
    auto e1 = p.Feed(part1);
    auto e2 = p.Feed(part2);
    e1.insert(e1.end(), e2.begin(), e2.end());
    REQUIRE(e1.size() == 1);
    CHECK(e1[0].kind == ParsedEvent::Kind::Adding);
    CHECK(e1[0].path == L"C:\\d\x0101ta\\\x017E.txt");
}
