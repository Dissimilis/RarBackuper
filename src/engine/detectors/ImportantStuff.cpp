#include "engine/detectors/Detectors.h"

namespace engine::detectors
{

// Implemented in Task 12 (detector catalog execution + manifest).
void CollectImportantStuff(const std::wstring&, EventSink& sink, std::atomic<bool>&)
{
    sink.OnLog(LogSeverity::Warn, L"Important Stuff collection is not implemented yet");
}

}
