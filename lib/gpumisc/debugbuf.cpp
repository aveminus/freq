#include "debugbuf.h"

#if defined(_MSC_VER)
	#include <Windows.h>  
	
	template<>
	void basic_debugbuf<char>::output_debug_string(const char *text)
	{
	    ::OutputDebugStringA(text);
	}
	
	template<>
	void basic_debugbuf<wchar_t>::output_debug_string(const wchar_t *text)
	{
	    ::OutputDebugStringW(text);
	}

#else  // not _MSC_VER, just write to stderr

	#include <stdio.h>

	template<>
	void basic_debugbuf<char>::output_debug_string(const char *text)
	{
            fprintf(stderr,"%s", text);
	}
	
	template<>
	void basic_debugbuf<wchar_t>::output_debug_string(const wchar_t *text)
	{
            fwprintf(stderr,L"%S", text);
	}

#endif
