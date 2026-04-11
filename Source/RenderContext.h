#pragma once

struct SRenderContext
{
    uint32_t  m_CurrentResolutionWidth;
    uint32_t  m_CurrentResolutionHeight;
    float     m_CurrentResolutionRatio;
    bool      m_IsSmallResolutionEnabled;
};