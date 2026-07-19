#pragma once

#include "Cubit/Core.h"
#include "Cubit/Logger.h"

#include <string>

//Breaks into the debugger. Only meaningful in builds that enable assertions.
#ifdef CB_DEBUG
	#ifdef _MSC_VER
		#define CB_DEBUGBREAK() __debugbreak()
	#else
		#define CB_DEBUGBREAK()
	#endif
	#define CB_ENABLE_ASSERTS
#else
	#define CB_DEBUGBREAK()
#endif

//Reports a broken internal expectation and stops in the debugger.
//
//Assertions describe bugs, not bad input. Use them for conditions that are
//impossible unless the program is already wrong, and validate untrusted input
//with ordinary error handling instead. They compile away outside debug builds,
//so the condition must never be relied on for behaviour.
#ifdef CB_ENABLE_ASSERTS
	#define CB_ASSERT(condition, message)                                     \
		do                                                                    \
		{                                                                     \
			if (!(condition))                                                 \
			{                                                                 \
				Logger::Critical(                                             \
					std::string("Assertion failed: ") + (message) +           \
					" [" #condition "] at " __FILE__ ":" +                    \
					std::to_string(__LINE__));                                \
				CB_DEBUGBREAK();                                              \
			}                                                                 \
		} while (false)
#else
	#define CB_ASSERT(condition, message) do { } while (false)
#endif
