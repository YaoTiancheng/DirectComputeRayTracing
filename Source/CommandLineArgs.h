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

    const std::string& GetFilename() const { return m_Filename; }

    const std::string& GetMtlFileSearchPath() const { return m_MtlFileSearchPath; }

    bool GetNoBVHAccel() const { return m_NoBVHAccel; }

    const std::wstring& GetEnvironmentTextureFilename() const { return m_EnvironmentTextureFilename; }

    static const CommandLineArgs* Singleton() { return s_Singleton; }

private:
    uint32_t    m_ResolutionX;
    uint32_t    m_ResolutionY;
    bool        m_ShaderDebugEnabled;
    bool        m_UseDebugDevice;
    bool        m_NoBVHAccel;
    std::string m_Filename;
    std::string m_MtlFileSearchPath;
    std::wstring m_EnvironmentTextureFilename;

    static CommandLineArgs* s_Singleton;
};