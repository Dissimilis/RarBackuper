#pragma once
#include <atomic>
#include <string>

#include "core/SettingsModel.h"
#include "engine/EventSink.h"

namespace engine
{

// Orchestrates the time-capsule content in <destination>\_meta (never
// %TEMP%; all I/O stays on the destination volume). The folder is added to
// the archive and removed again afterwards in every outcome.
class MetaCollector
{
public:
    MetaCollector(const core::AppConfig& config, std::wstring destination, EventSink& sink,
                  std::atomic<bool>& cancel);

    std::wstring MetaDir() const { return metaDir_; }

    // Creates a fresh _meta staging folder (cleaning any leftover from a
    // crashed run) and runs every enabled collector. Returns false when
    // cancelled part-way.
    bool Collect();

    // Writes the archive comment stamp next to (not inside) _meta and
    // returns its path; empty on failure. Deleted again by Cleanup().
    std::wstring WriteCommentFile(const core::AppConfig& config);

    // Removes the _meta staging folder and the comment file. Safe to call
    // multiple times and when nothing was created.
    void Cleanup();

private:
    bool Cancelled() const { return cancel_.load(); }

    core::AppConfig config_;
    std::wstring destination_;
    std::wstring metaDir_;
    std::wstring commentFile_;
    EventSink& sink_;
    std::atomic<bool>& cancel_;
    bool created_ = false;
};

}
