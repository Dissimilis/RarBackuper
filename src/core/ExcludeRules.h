#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace core
{

enum class RuleType
{
    Folder,
    File,
    Pattern,
};

struct ExcludeRule
{
    RuleType type;
    std::wstring value;
};

// Built-in default exclude rules (regenerable/junk data).
std::vector<ExcludeRule> DefaultExcludeRules();

// Translate a rule to the mask passed to Rar.exe as -x<mask>:
//   bare folder name  -> *\name\   (recursive, dirs only)
//   bare file name    -> *\name    (recursive)
//   full paths        -> verbatim
//   patterns          -> verbatim
std::wstring RuleToMask(const ExcludeRule& rule);

// True if the value looks like a full path rather than a bare name.
bool IsFullPathValue(std::wstring_view value);

// Case-insensitive wildcard match; '*' matches any run (including '\'),
// '?' matches a single character.
bool WildcardMatch(std::wstring_view pattern, std::wstring_view text);

// Local re-implementation of the RAR exclusion semantics for the supported
// rule forms, used by the pre-scan file counter so its total matches what
// RAR will actually add.
class ExcludeMatcher
{
public:
    explicit ExcludeMatcher(std::vector<ExcludeRule> rules);

    bool IsExcludedFile(const std::wstring& fullPath) const;

    // For pruning directory descent during the pre-scan.
    bool IsExcludedDir(const std::wstring& fullDirPath) const;

private:
    std::vector<ExcludeRule> rules_;
};

}
