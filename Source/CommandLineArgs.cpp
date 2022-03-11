#include "stdafx.h"
#include "CommandLineArgs.h"

CommandLineArgs* CommandLineArgs::s_Singleton = nullptr;

CommandLineArgs::CommandLineArgs()
    : m_ResolutionX( 1280 )
    , m_ResolutionY( 720 )
    , m_ShaderDebugEnabled( false )
    , m_UseDebugDevice( false )
    , m_NoBVHAccel( false )
    , m_MultipleImportanceSamplingEnabled( true )
    , m_OutputBVHToFile( false )
{
    assert( s_Singleton == nullptr );
    s_Singleton = this;
}

void CommandLineArgs::Parse( const wchar_t* cmdLine )
{
    if ( cmdLine == nullptr || cmdLine[ 0 ] == '\0' )
        return;

    int numArgs = 0;
    wchar_t** argv = CommandLineToArgvW( cmdLine, &numArgs );
    if ( argv == nullptr )
        return;

    for ( int iArg = 0; iArg < numArgs; ++iArg )
    {
        wchar_t* argStr = argv[ iArg ];
        
        if ( wcscmp( argStr, L"-ResX" ) == 0 && iArg + 1 < numArgs )
        {
            wchar_t* argStr1 = argv[ ++iArg ];
            wchar_t* end;
            m_ResolutionX = (uint32_t) wcstol( argStr1, &end, 10 );
        }
        else if ( wcscmp( argStr, L"-ResY" ) == 0 && iArg + 1 < numArgs )
        {
            wchar_t* argStr1 = argv[ ++iArg ];
            wchar_t* end;
            m_ResolutionY = (uint32_t) wcstol( argStr1, &end, 10 );
        }
        else if ( wcscmp( argStr, L"-EnvMap" ) == 0 && iArg + 1 < numArgs )
        {
            wchar_t* argStr1 = argv[ ++iArg ];
            m_EnvironmentTextureFilename = argStr1;
        }
        else if ( wcscmp( argStr, L"-ShaderDebug" ) == 0 )
        {
            m_ShaderDebugEnabled = true;
        }
        else if ( wcscmp( argStr, L"-DebugDevice" ) == 0 )
        {
            m_UseDebugDevice = true;
        }
        else if ( wcscmp( argStr, L"-NoBVH" ) == 0 )
        {
            m_NoBVHAccel = true;
        }
        else if ( wcscmp( argStr, L"-DisableMIS" ) == 0 )
        {
            m_MultipleImportanceSamplingEnabled = false;
        }
        else if ( wcscmp( argStr, L"-OutputBVH" ) == 0 )
        {
            m_OutputBVHToFile = true;
        }
        else if ( iArg == numArgs - 1 )
        {
            char mbFinename[ MAX_PATH ];
            errno_t err = (errno_t)wcstombs( mbFinename, argStr, MAX_PATH );
            m_Filename = mbFinename;
        }
    }

    LocalFree( argv );
}
