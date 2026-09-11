#include "AppControl.h"

namespace KujataEngine {
namespace {
bool gQuitRequested = false;
} // namespace

void RequestQuitApplication() { gQuitRequested = true; }

bool IsQuitRequested() { return gQuitRequested; }

} // namespace KujataEngine
