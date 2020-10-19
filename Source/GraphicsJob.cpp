#include "stdafx.h"
#include "GraphicsJob.h"
#include "RenderJobHelper.h"

GraphicsJob::GraphicsJob()
    : m_Shader( nullptr )
    , m_VertexStride( 0 )
    , m_VertexCount( 0 )
    , m_VertexBuffer( nullptr )
    , m_InputLayout( nullptr )
{
}

void GraphicsJob::Dispatch()
{
    RenderJobHelper::SamplerStateList samplerStates( m_SamplerStates.data(), (uint32_t)m_SamplerStates.size() );
    RenderJobHelper::ResourceViewList SRVs( m_SRVs.data(), (uint32_t)m_SRVs.size() );
    RenderJobHelper::BufferList constantBuffers( m_ConstantBuffers.data(), (uint32_t)m_ConstantBuffers.size() );
    RenderJobHelper::DispatchDraw( m_VertexBuffer, m_Shader, m_InputLayout, SRVs, samplerStates, constantBuffers, m_VertexCount, m_VertexStride );
}
