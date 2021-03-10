#pragma once

#include <locale>
#include <codecvt>

namespace StringConversion
{
    template < typename codecvt >
    std::wstring StringToWString( const std::string& src )
    {
        std::wstring_convert< codecvt > converter;
        return converter.from_bytes( src );
    }

    template < typename codecvt >
    std::string WStringToString( const std::wstring& src )
    {
        std::wstring_convert< codecvt > converter;
        return converter.to_bytes( src );
    }

    std::wstring UTF8StringToUTF16WString( const std::string& src )
    {
        return StringToWString< std::codecvt_utf8_utf16< wchar_t > >( src );
    }

    std::string UTF16WStringToUTF8String( const std::wstring& src )
    {
        return WStringToString< std::codecvt_utf8_utf16< wchar_t > >( src );
    }
}