#include "stdafx.h"
#include "ComputeJob.h"
#include "RenderJobHelper.h"

ComputeJob::ComputeJob()
    : m_DispatchSizeX( 1 )
    , m_DispatchSizeY( 1 )
    , m_DispatchSizeZ( 1 )
    , m_Shader( nullptr )
{
}

void ComputeJob::Dispatch()
{
    RenderJobHelper::SamplerStateList samplerStates( m_SamplerStates.data(), (uint32_t)m_SamplerStates.size() );
    RenderJobHelper::UnorderedAccessViewList UAVs( m_UAVs.data(), (uint32_t)m_UAVs.size() );
    RenderJobHelper::ResourceViewList SRVs( m_SRVs.data(), (uint32_t)m_SRVs.size() );
    RenderJobHelper::BufferList constantBuffers( m_ConstantBuffers.data(), (uint32_t)m_ConstantBuffers.size() );
    RenderJobHelper::DispatchCompute( m_DispatchSizeX, m_DispatchSizeY, m_DispatchSizeZ, m_Shader, SRVs, UAVs, samplerStates, constantBuffers );
}

void ComputeJob::DispatchIndirect( ID3D11Buffer* buffer )
{
    RenderJobHelper::SamplerStateList samplerStates( m_SamplerStates.data(), (uint32_t)m_SamplerStates.size() );
    RenderJobHelper::UnorderedAccessViewList UAVs( m_UAVs.data(), (uint32_t)m_UAVs.size() );
    RenderJobHelper::ResourceViewList SRVs( m_SRVs.data(), (uint32_t)m_SRVs.size() );
    RenderJobHelper::BufferList constantBuffers( m_ConstantBuffers.data(), (uint32_t)m_ConstantBuffers.size() );
    RenderJobHelper::DispatchComputeIndirect( buffer, m_Shader, SRVs, UAVs, samplerStates, constantBuffers );
}
