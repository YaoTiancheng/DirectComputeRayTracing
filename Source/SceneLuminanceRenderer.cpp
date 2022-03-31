#include "stdafx.h"
#include "SceneLuminanceRenderer.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "ScopedRenderAnnotation.h"
#include "imgui/imgui.h"
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
        uint32_t sumLuminanceBlockCountX = uint32_t( std::ceilf( resolutionWidth / float( SL_BLOCKSIZE ) ) );
        sumLuminanceBlockCountX = uint32_t( std::ceilf( sumLuminanceBlockCountX / 2.0f ) );
        uint32_t sumLuminanceBlockCountY = uint32_t( std::ceilf( resolutionHeight / float( SL_BLOCKSIZEY ) ) );
        sumLuminanceBlockCountY = uint32_t( std::ceilf( sumLuminanceBlockCountY / 2.0f ) );
        m_SumLuminanceBuffer0.reset( GPUBuffer::CreateStructured(
              sizeof( float ) * sumLuminanceBlockCountX * sumLuminanceBlockCountY
            , sizeof( float )
            , D3D11_USAGE_DEFAULT
            , D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE ) );
        if ( !m_SumLuminanceBuffer0 )
            return false;
        m_SumLuminanceBuffer1.reset( GPUBuffer::CreateStructured(
              sizeof( float ) * sumLuminanceBlockCountX * sumLuminanceBlockCountY
            , sizeof( float )
            , D3D11_USAGE_DEFAULT
            , D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE ) );
        if ( !m_SumLuminanceBuffer1 )
            return false;
    }

    {
        m_SumLuminanceConstantsBuffer0.reset( GPUBuffer::Create(
              sizeof( uint32_t ) * 4
            , 0
            , DXGI_FORMAT_UNKNOWN
            , D3D11_USAGE_DYNAMIC
            , D3D11_BIND_CONSTANT_BUFFER
            , GPUResourceCreationFlags_CPUWriteable ) );
        if ( !m_SumLuminanceConstantsBuffer0 )
            return false;

        m_SumLuminanceConstantsBuffer1.reset( GPUBuffer::Create(
              sizeof( uint32_t ) * 4
            , 0
            , DXGI_FORMAT_UNKNOWN
            , D3D11_USAGE_DYNAMIC
            , D3D11_BIND_CONSTANT_BUFFER
            , GPUResourceCreationFlags_CPUWriteable ) );
        if ( !m_SumLuminanceConstantsBuffer1 )
            return false;
    }

    {
        m_SumLuminanceTo1DJob.m_UAVs.push_back( m_SumLuminanceBuffer1->GetUAV() );
        m_SumLuminanceTo1DJob.m_SRVs.push_back( filmTexture->GetSRV() );
        m_SumLuminanceTo1DJob.m_ConstantBuffers.push_back( m_SumLuminanceConstantsBuffer0->GetBuffer() );
        m_SumLuminanceTo1DJob.m_Shader = m_SumLuminanceTo1DShader.get();
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

void SceneLuminanceRenderer::Dispatch( uint32_t resolutionWidth, uint32_t resolutionHeight )
{
    SCOPED_RENDER_ANNOTATION( L"Calculate scene luminance" );

    uint32_t sumLuminanceBlockCountX = uint32_t( std::ceilf( resolutionWidth / float( SL_BLOCKSIZE ) ) );
    sumLuminanceBlockCountX = uint32_t( std::ceilf( sumLuminanceBlockCountX / 2.0f ) );
    uint32_t sumLuminanceBlockCountY = uint32_t( std::ceilf( resolutionHeight / float( SL_BLOCKSIZEY ) ) );
    sumLuminanceBlockCountY = uint32_t( std::ceilf( sumLuminanceBlockCountY / 2.0f ) );

    m_SumLuminanceTo1DJob.m_DispatchSizeX = sumLuminanceBlockCountX;
    m_SumLuminanceTo1DJob.m_DispatchSizeY = sumLuminanceBlockCountY;

    if ( void* address = m_SumLuminanceConstantsBuffer0->Map() )
    {
        uint32_t* params = (uint32_t*)address;
        params[ 0 ] = sumLuminanceBlockCountX;
        params[ 1 ] = sumLuminanceBlockCountY;
        params[ 2 ] = resolutionWidth;
        params[ 3 ] = resolutionHeight;
        m_SumLuminanceConstantsBuffer0->Unmap();
    }

    m_SumLuminanceTo1DJob.Dispatch();

    GPUBuffer* sumLuminanceBuffer0 = m_SumLuminanceBuffer0.get();
    GPUBuffer* sumLuminanceBuffer1 = m_SumLuminanceBuffer1.get();
    uint32_t blockCount = sumLuminanceBlockCountX * sumLuminanceBlockCountY;
    while ( blockCount != 1 )
    {
        m_SumLuminanceToSingleJob.m_UAVs[ 0 ] = sumLuminanceBuffer0->GetUAV();
        m_SumLuminanceToSingleJob.m_SRVs[ 0 ] = sumLuminanceBuffer1->GetSRV();

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

        std::swap( sumLuminanceBuffer0, sumLuminanceBuffer1 );
    }
}

void SceneLuminanceRenderer::OnImGUI()
{
}
