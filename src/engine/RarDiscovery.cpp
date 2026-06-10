#include "engine/RarDiscovery.h"

#include <windows.h>

#include <cwctype>
#include <deque>
#include <filesystem>

#include "engine/Settings.h"

namespace fs = std::filesystem;

namespace engine
{

namespace
{

bool NameEqualsNoCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(b[i]))
            return false;
    return true;
}

}

std::wstring FindExecutable(const std::wstring& fileName,
                            const std::vector<std::wstring>& roots,
                            int maxDepth,
                            std::vector<std::wstring>* searchedRoots)
{
    for (const std::wstring& root : roots)
    {
        if (searchedRoots)
            searchedRoots->push_back(root);

        // Breadth-first so the shallowest match wins.
        std::deque<std::pair<fs::path, int>> queue;
        queue.emplace_back(fs::path(root), 0);
        while (!queue.empty())
        {
            auto [dir, depth] = queue.front();
            queue.pop_front();
            std::error_code ec;
            fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
            if (ec)
                continue;
            for (const fs::directory_entry& entry : it)
            {
                std::error_code ec2;
                if (entry.is_regular_file(ec2) &&
                    NameEqualsNoCase(entry.path().filename().wstring(), fileName))
                    return entry.path().wstring();
                if (depth + 1 <= maxDepth && entry.is_directory(ec2) && !entry.is_symlink(ec2))
                    queue.emplace_back(entry.path(), depth + 1);
            }
        }
    }
    return L"";
}

std::wstring DiscoverTool(const std::wstring& fileName, std::vector<std::wstring>* searchedRoots)
{
    std::vector<std::wstring> roots;
    roots.push_back(ExeDirectory());

    wchar_t cwd[MAX_PATH + 1]{};
    DWORD n = GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring cwdStr(cwd, n);
    if (!cwdStr.empty() && !NameEqualsNoCase(cwdStr, roots[0]))
        roots.push_back(cwdStr);

    return FindExecutable(fileName, roots, 3, searchedRoots);
}

}
