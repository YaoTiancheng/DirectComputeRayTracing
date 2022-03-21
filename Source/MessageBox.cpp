#include "stdafx.h"
#include "MessageBox.h"
#include "Logging.h"
#include "imgui/imgui.h"

void CMessagebox::Append( const char* error )
{
    m_Strings.emplace_back( error );
    LOG_STRING( error );
}

void CMessagebox::AppendFormat( const char* format, ... )
{
    const uint32_t s_MaxBufferLength = 512;
    char buffer[ s_MaxBufferLength ];

    va_list argptr;
    va_start( argptr, format );
    vsprintf_s( buffer, s_MaxBufferLength, format, argptr );
    va_end( argptr );

    Append( buffer );
}

void CMessagebox::OnImGUI()
{
    ImVec2 center( ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f );
    ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
    ImGui::OpenPopup( "Error##ErrorPopup" );
    if ( ImGui::BeginPopupModal( "Error##ErrorPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        for ( auto& error : m_Strings )
        {
            ImGui::Text( error.c_str() );
        }
        if ( ImGui::Button( "OK##ErrorPopup" ) )
        {
            m_Strings.clear();
        }
        ImGui::EndPopup();
    }
}
