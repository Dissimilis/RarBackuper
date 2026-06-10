#pragma once
#include <string>
#include <vector>

namespace core
{

enum class DetectorKind
{
    FileGlob,       // copy files/dirs matching path patterns (";"-separated,
                    // env vars allowed, "*" allowed per path component;
                    // a matched directory is copied recursively)
    RegistryExport, // export a registry key to a .reg file
    CommandOutput,  // run a command, capture stdout ({OUTDIR} placeholder
                    // expands to the detector's output folder); silently
                    // skipped when the tool is not installed
};

struct Detector
{
    std::wstring name;
    std::wstring description;
    std::wstring group;   // one of the 13 catalog groups
    DetectorKind kind;
    std::wstring target;  // patterns / registry key / command line
    std::wstring output;  // output file name for registry/command kinds
    std::wstring restore; // step-by-step restore instructions template
    unsigned long long sizeCapBytes = 10ull * 1024 * 1024; // per-file cap
};

// The full Important Stuff catalog (declarative data; no filesystem access).
const std::vector<Detector>& DetectorCatalog();

// The 13 group names, in catalog order.
std::vector<std::wstring> DetectorGroups();

}
