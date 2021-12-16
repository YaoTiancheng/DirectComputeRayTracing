#pragma once

struct SRenderContext
{
    uint32_t  m_CurrentResolutionWidth;
    uint32_t  m_CurrentResolutionHeight;
    float     m_CurrentResolutionRatio;
    bool      m_IsResolutionChanged;
    bool      m_IsSmallResolutionEnabled;
    uint32_t  m_TileOffsetX;
    uint32_t  m_TileOffsetY;
    bool      m_EnablePostFX;
};