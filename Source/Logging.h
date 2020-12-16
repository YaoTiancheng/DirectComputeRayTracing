#pragma once

void LogString( const char* str );

void LogStringFormat( const char* format, ... );

#define LOG_STRING( str )                   LogString( str )

#define LOG_STRING_FORMAT( format, ... )    LogStringFormat( format, __VA_ARGS__ )