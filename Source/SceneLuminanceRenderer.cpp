#include "stdafx.h"
#include "SceneLuminanceRenderer.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "../Shaders/SumLuminanceDef.inc.hlsl"

bool SceneLuminanceRenderer::Init( uint32_t resolutionWidth, uint32_t resolutionHeight, const GPUTexturePtr& filmTexture )
{
    {
        std::vector<D3D_SHADER_MACRO> sumLuminanceShaderDefines;
        sumLuminanceShaderDefines.push_back( { NULL, NULL } );
        m_SumLuminanceToSingleShader.reset( ComputeShader::CreateFromFile( L"Shaders\\SumLuminance.hlsl", sumLuminanceShaderDefines ) );
        if ( !m_SumLuminanceToSingleShader )
            return false;

        sumLuminanceShaderDefines.insert( sumLuminanceShaderDefines.begin(), { "REDUCE_TO_1D", "0" } );
        m_SumLuminanceTo1DShader.reset( ComputeShader::CreateFromFile( L"Shaders\\SumLuminance.hlsl", sumLuminanceShaderDefines ) );
        if ( !m_SumLuminanceTo1DShader )
            return false;
    }

    {
        m_SumLuminanceBlockCountX = uint32_t( std::ceilf( resolutionWidth / float( SL_BLOCKSIZE ) ) );
        m_SumLuminanceBlockCountX = uint32_t( std::ceilf( m_SumLuminanceBlockCountX / 2.0f ) );
        m_SumLuminanceBlockCountY = uint32_t( std::ceilf( resolutionHeight / float( SL_BLOCKSIZEY ) ) );
        m_SumLuminanceBlockCountY = uint32_t( std::ceilf( m_SumLuminanceBlockCountY / 2.0f ) );
        m_SumLuminanceBuffer0.reset( GPUBuffer::Create(
            sizeof( float ) * m_SumLuminanceBlockCountX * m_SumLuminanceBlockCountY
            , sizeof( float )
            , GPUResourceCreationFlags_IsStructureBuffer | GPUResourceCreationFlags_HasUAV ) );
        if ( !m_SumLuminanceBuffer0 )
            return false;
        m_SumLuminanceBuffer1.reset( GPUBuffer::Create(
            sizeof( float ) * m_SumLuminanceBlockCountX * m_SumLuminanceBlockCountY
            , sizeof( float )
            , GPUResourceCreationFlags_IsStructureBuffer | GPUResourceCreationFlags_HasUAV ) );
        if ( !m_SumLuminanceBuffer1 )
            return false;
    }

    {
        uint32_t params[ 4 ] = { m_SumLuminanceBlockCountX, m_SumLuminanceBlockCountY, resolutionWidth, resolutionHeight };
        m_SumLuminanceConstantsBuffer0.reset( GPUBuffer::Create(
            sizeof( uint32_t ) * 4
            , 0
            , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsConstantBuffer
            , params ) );
        if ( !m_SumLuminanceConstantsBuffer0 )
            return false;

        m_SumLuminanceConstantsBuffer1.reset( GPUBuffer::Create(
            sizeof( uint32_t ) * 4
            , 0
            , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer ) );
        if ( !m_SumLuminanceConstantsBuffer1 )
            return false;
    }

    {
        m_SumLuminanceTo1DJob.m_UAVs.push_back( m_SumLuminanceBuffer1->GetUAV() );
        m_SumLuminanceTo1DJob.m_SRVs.push_back( filmTexture->GetSRV() );
        m_SumLuminanceTo1DJob.m_ConstantBuffers.push_back( m_SumLuminanceConstantsBuffer0->GetBuffer() );
        m_SumLuminanceTo1DJob.m_Shader = m_SumLuminanceTo1DShader.get();
        m_SumLuminanceTo1DJob.m_DispatchSizeX = m_SumLuminanceBlockCountX;
        m_SumLuminanceTo1DJob.m_DispatchSizeY = m_SumLuminanceBlockCountY;
        m_SumLuminanceTo1DJob.m_DispatchSizeZ = 1;

        m_SumLuminanceToSingleJob.m_UAVs.push_back( nullptr );
        m_SumLuminanceToSingleJob.m_SRVs.push_back( nullptr );
        m_SumLuminanceToSingleJob.m_ConstantBuffers.push_back( m_SumLuminanceConstantsBuffer1->GetBuffer() );
        m_SumLuminanceToSingleJob.m_Shader = m_SumLuminanceToSingleShader.get();
        m_SumLuminanceToSingleJob.m_DispatchSizeY = 1;
        m_SumLuminanceToSingleJob.m_DispatchSizeZ = 1;
    }

    return true;
}

void SceneLuminanceRenderer::Dispatch()
{
    m_SumLuminanceTo1DJob.Dispatch();

    uint32_t blockCount = m_SumLuminanceBlockCountX * m_SumLuminanceBlockCountY;
    while ( blockCount != 1 )
    {
        m_SumLuminanceToSingleJob.m_UAVs[ 0 ] = m_SumLuminanceBuffer0->GetUAV();
        m_SumLuminanceToSingleJob.m_SRVs[ 0 ] = m_SumLuminanceBuffer1->GetSRV();

        uint32_t threadGroupCount = uint32_t( std::ceilf( blockCount / float( SL_REDUCE_TO_SINGLE_GROUPTHREADS ) ) );

        if ( void* address = m_SumLuminanceConstantsBuffer1->Map() )
        {
            uint32_t* params = (uint32_t*)address;
            params[ 0 ] = blockCount;
            params[ 1 ] = threadGroupCount;
            m_SumLuminanceConstantsBuffer1->Unmap();
        }

        m_SumLuminanceToSingleJob.m_DispatchSizeX = threadGroupCount;

        m_SumLuminanceToSingleJob.Dispatch();

        blockCount = threadGroupCount;

        std::swap( m_SumLuminanceBuffer0, m_SumLuminanceBuffer1 );
    }
}
