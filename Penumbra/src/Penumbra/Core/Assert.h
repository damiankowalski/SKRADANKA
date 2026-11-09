#pragma once

namespace Penumbra::Detail {

	[[noreturn]] void AssertionFailed(const char* expression, const char* message, const char* file, int line);

}

// Asserts guard programmer errors (broken invariants), never recoverable runtime
// conditions such as missing files - those must be reported through return values.
#ifndef PN_DIST
	#define PN_CORE_ASSERT(condition, message)                                                                   \
		do                                                                                                       \
		{                                                                                                        \
			if (!(condition))                                                                                    \
				::Penumbra::Detail::AssertionFailed(#condition, message, __FILE__, __LINE__);                    \
		} while (false)
#else
	#define PN_CORE_ASSERT(condition, message) ((void)0)
#endif

#define PN_CORE_VERIFY(condition, message)                                                                       \
	do                                                                                                           \
	{                                                                                                            \
		if (!(condition))                                                                                        \
			::Penumbra::Detail::AssertionFailed(#condition, message, __FILE__, __LINE__);                        \
	} while (false)
