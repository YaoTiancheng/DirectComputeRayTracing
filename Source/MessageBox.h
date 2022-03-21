#pragma once

class CMessagebox
{
public:
    static CMessagebox& GetSingleton()
    {
        static CMessagebox s_Singleton;
        return s_Singleton;
    }

    void Append( const char* error );

    void AppendFormat( const char* error, ... );

    bool IsEmpty() const { return m_Strings.empty(); }

    void OnImGUI();

private:
    std::vector<std::string> m_Strings;
};