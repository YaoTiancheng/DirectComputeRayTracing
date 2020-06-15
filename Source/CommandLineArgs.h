#pragma once

class CommandLineArgs
{
public:
    CommandLineArgs();

    void Parse( const wchar_t* cmdLine );

    uint32_t ResolutionX() const { return m_ResolutionX; }
    uint32_t ResolutionY() const { return m_ResolutionY; }

    bool ShaderDebugEnabled() const { return m_ShaderDebugEnabled; }
    bool UseDebugDevice() const { return m_UseDebugDevice; }

    static const CommandLineArgs* Singleton() { return s_Singleton; }

private:
    uint32_t    m_ResolutionX;
    uint32_t    m_ResolutionY;
    bool        m_ShaderDebugEnabled;
    bool        m_UseDebugDevice;

    static CommandLineArgs* s_Singleton;
};