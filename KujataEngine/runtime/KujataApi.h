#pragma once

#if defined(_WIN32)
#if defined(KUJATA_ENGINE_EXPORTS)
#define KUJATA_API __declspec(dllexport)
#else
#define KUJATA_API __declspec(dllimport)
#endif
#else
#define KUJATA_API
#endif
