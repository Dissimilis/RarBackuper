#include "core/ExcludeRules.h"

#include <cwctype>

namespace core
{

namespace
{

wchar_t Lower(wchar_t c)
{
    return static_cast<wchar_t>(std::towlower(c));
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (Lower(a[i]) != Lower(b[i]))
            return false;
    return true;
}

bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && EqualsNoCase(text.substr(0, prefix.size()), prefix);
}

std::wstring_view TrimTrailingSlashes(std::wstring_view p)
{
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/'))
        p.remove_suffix(1);
    return p;
}

std::vector<std::wstring_view> SplitPath(std::wstring_view path)
{
    std::vector<std::wstring_view> parts;
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/')
        {
            if (i > start)
                parts.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::wstring_view FileNameOf(std::wstring_view path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring_view::npos ? path : path.substr(pos + 1);
}

// "<rule path>" excludes "<rule path>" itself and everything below it.
bool UnderFullPath(std::wstring_view path, std::wstring_view rulePath)
{
    rulePath = TrimTrailingSlashes(rulePath);
    if (EqualsNoCase(path, rulePath))
        return true;
    return path.size() > rulePath.size() &&
           StartsWithNoCase(path, rulePath) &&
           (path[rulePath.size()] == L'\\' || path[rulePath.size()] == L'/');
}

}

bool IsFullPathValue(std::wstring_view value)
{
    return value.find_first_of(L"\\/") != std::wstring_view::npos ||
           value.find(L':') != std::wstring_view::npos;
}

std::vector<ExcludeRule> DefaultExcludeRules()
{
    return {
        // VCS internals
        {RuleType::Folder, L".git"},
        {RuleType::Folder, L".svn"},
        {RuleType::Folder, L".hg"},
        // Dependency dirs
        {RuleType::Folder, L"node_modules"},
        {RuleType::Folder, L".venv"},
        {RuleType::Folder, L"venv"},
        {RuleType::Folder, L"packages"},
        // Build outputs / caches
        {RuleType::Folder, L"bin"},
        {RuleType::Folder, L"obj"},
        {RuleType::Folder, L".vs"},
        {RuleType::Folder, L"__pycache__"},
        {RuleType::Folder, L"target"},
        {RuleType::Pattern, L"*.pyc"},
        // OS & temp junk
        {RuleType::File, L"Thumbs.db"},
        {RuleType::File, L"desktop.ini"},
        {RuleType::Pattern, L"*.tmp"},
        {RuleType::Pattern, L"~$*"},
        {RuleType::Folder, L"$RECYCLE.BIN"},
        {RuleType::Folder, L"System Volume Information"},
    };
}

std::wstring RuleToMask(const ExcludeRule& rule)
{
    switch (rule.type)
    {
    case RuleType::Folder:
        if (IsFullPathValue(rule.value))
            return rule.value;
        return L"*\\" + rule.value + L"\\";
    case RuleType::File:
        if (IsFullPathValue(rule.value))
            return rule.value;
        return L"*\\" + rule.value;
    case RuleType::Pattern:
    default:
        return rule.value;
    }
}

bool WildcardMatch(std::wstring_view pattern, std::wstring_view text)
{
    size_t p = 0, t = 0;
    size_t starP = std::wstring_view::npos, starT = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == L'?' || Lower(pattern[p]) == Lower(text[t])))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == L'*')
        {
            starP = p++;
            starT = t;
        }
        else if (starP != std::wstring_view::npos)
        {
            p = starP + 1;
            t = ++starT;
        }
        else
        {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == L'*')
        ++p;
    return p == pattern.size();
}

ExcludeMatcher::ExcludeMatcher(std::vector<ExcludeRule> rules)
    : rules_(std::move(rules))
{
}

bool ExcludeMatcher::IsExcludedFile(const std::wstring& fullPath) const
{
    std::wstring_view path = TrimTrailingSlashes(fullPath);
    std::wstring_view fileName = FileNameOf(path);
    std::vector<std::wstring_view> parts = SplitPath(path);

    for (const ExcludeRule& r : rules_)
    {
        switch (r.type)
        {
        case RuleType::Folder:
            if (IsFullPathValue(r.value))
            {
                // the rule path is a directory: it excludes files strictly below it
                std::wstring_view rulePath = TrimTrailingSlashes(r.value);
                if (path.size() > rulePath.size() && UnderFullPath(path, rulePath) &&
                    !EqualsNoCase(path, rulePath))
                    return true;
            }
            else
            {
                // any *directory* component equal to the bare name (not the file itself)
                for (size_t i = 0; i + 1 < parts.size(); ++i)
                    if (EqualsNoCase(parts[i], r.value))
                        return true;
            }
            break;
        case RuleType::File:
            if (IsFullPathValue(r.value))
            {
                if (EqualsNoCase(path, TrimTrailingSlashes(r.value)))
                    return true;
            }
            else if (EqualsNoCase(fileName, r.value))
            {
                return true;
            }
            break;
        case RuleType::Pattern:
        {
            std::wstring_view target =
                r.value.find_first_of(L"\\/") != std::wstring::npos ? path : fileName;
            if (WildcardMatch(r.value, target))
                return true;
            break;
        }
        }
    }
    return false;
}

bool ExcludeMatcher::IsExcludedDir(const std::wstring& fullDirPath) const
{
    std::wstring_view path = TrimTrailingSlashes(fullDirPath);
    std::vector<std::wstring_view> parts = SplitPath(path);

    for (const ExcludeRule& r : rules_)
    {
        if (r.type != RuleType::Folder)
            continue;
        if (IsFullPathValue(r.value))
        {
            if (UnderFullPath(path, r.value))
                return true;
        }
        else
        {
            for (const auto& part : parts)
                if (EqualsNoCase(part, r.value))
                    return true;
        }
    }
    return false;
}

}
