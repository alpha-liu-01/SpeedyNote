#include "OcrEngine.h"

#ifdef SPEEDYNOTE_HAS_WINDOWS_INK
#include "engines/WindowsInkOcrEngine.h"
#endif

std::unique_ptr<OcrEngine> OcrEngine::createBest()
{
#ifdef SPEEDYNOTE_HAS_WINDOWS_INK
    auto engine = std::make_unique<WindowsInkOcrEngine>();
    if (engine->isAvailable())
        return engine;
#endif
    return nullptr;
}
