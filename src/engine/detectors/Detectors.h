#pragma once
#include <atomic>
#include <string>

#include "engine/EventSink.h"

namespace engine::detectors
{

// _meta\system-info.txt: machine passport + full drives/partition map.
// Degrades gracefully where data needs elevation; never elevates.
void WriteSystemInfo(const std::wstring& filePath, EventSink& sink);

// _meta\filelist-<drive>.txt per fixed drive: full recursive listing
// (path|size|modified). Inaccessible paths are skipped and counted.
void WriteDriveInventories(const std::wstring& metaDir, EventSink& sink,
                           std::atomic<bool>& cancel);

// _meta\bookmarks\: copies of browser bookmark stores (Chrome, Edge,
// Brave, Opera, Vivaldi, Firefox), named browser+profile.
void CollectBookmarks(const std::wstring& bookmarksDir, EventSink& sink,
                      std::atomic<bool>& cancel);

// _meta\important\: Important Stuff detector catalog execution + manifest.
void CollectImportantStuff(const std::wstring& importantDir, EventSink& sink,
                           std::atomic<bool>& cancel);

}
