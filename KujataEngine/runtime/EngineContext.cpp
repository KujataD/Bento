#include "EngineContext.h"

namespace KujataEngine {

EngineContext& GetEngineContext() {
	static EngineContext context;
	return context;
}

} // namespace KujataEngine
