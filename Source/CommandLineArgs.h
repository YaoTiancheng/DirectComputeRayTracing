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

    bool GetNoBVHAccel() const { return m_NoBVHAccel; }

    const std::wstring& GetEnvironmentTextureFilename() const { return m_EnvironmentTextureFilename; }

    bool GetOutputBVHToFile() const { return m_OutputBVHToFile; }

    static const CommandLineArgs* Singleton() { return s_Singleton; }

private:
    uint32_t    m_ResolutionX;
    uint32_t    m_ResolutionY;
    bool        m_ShaderDebugEnabled;
    bool        m_UseDebugDevice;
    bool        m_NoBVHAccel;
    std::string m_Filename;
    std::wstring m_EnvironmentTextureFilename;
    bool        m_OutputBVHToFile;

    static CommandLineArgs* s_Singleton;
};