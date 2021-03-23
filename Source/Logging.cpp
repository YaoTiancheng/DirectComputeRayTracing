#include "stdafx.h"
#include "Logging.h"

void LogString( const char* str )
{
    OutputDebugStringA( str );
}

void LogStringFormat( const char* format, ... )
{
    const uint32_t s_MaxBufferLength = 512;
    char buffer[ s_MaxBufferLength ];

    va_list argptr;
    va_start( argptr, format );
    vsprintf_s( buffer, s_MaxBufferLength, format, argptr );
    va_end( argptr );

    LogString( buffer );
}
