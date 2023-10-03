
#include "stdafx.h"
#include "BxDFTexturesBuilding.h"
#include "Shader.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "ComputeJob.h"
#include "ScopedRenderAnnotation.h"
#include "../Shaders/BxDFTextureDef.inc.hlsl"

struct SIntegralKernelParams
{
    float m_LutIntervalX;
    float m_LutIntervalY;
    float m_LutIntervalZ;
    float m_SampleWeight;
    uint32_t m_SampleCount;
    uint32_t m_GroupSizeX;
    uint32_t m_GroupSizeY;
};

static ComputeShaderPtr CompileAndCreateKernel( const char* kernelName, const SIntegralKernelParams& Params )
{
    std::vector<D3D_SHADER_MACRO> shaderDefines;

    if ( kernelName )
    {
        shaderDefines.push_back( { kernelName, "0" } );
    }

    char sharedBuffer[ 512 ];
    int sharedBufferStart = 0;
    const int sharedBufferSize = sizeof( sharedBuffer );
#define ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( name, value, format ) \
    if ( sharedBufferStart < sharedBufferSize - 1 ) \
    { \
        int len = sprintf_s( sharedBuffer + sharedBufferStart, sharedBufferSize - sharedBufferStart, format, value ); \
        shaderDefines.push_back( { name, sharedBuffer + sharedBufferStart } ); \
        sharedBufferStart += len + 1; \
    } \
    else \
    { \
        assert( "Shared buffer is too small!" ); \
    }

    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "LUT_INTERVAL_X", Params.m_LutIntervalX, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "LUT_INTERVAL_Y", Params.m_LutIntervalY, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "LUT_INTERVAL_Z", Params.m_LutIntervalZ, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "SAMPLE_WEIGHT", Params.m_SampleWeight, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "SAMPLE_COUNT", Params.m_SampleCount, "%d" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "GROUP_SIZE_X", Params.m_GroupSizeX, "%d" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "GROUP_SIZE_Y", Params.m_GroupSizeY, "%d" );
#undef ADD_SHADER_DEFINE_WITH_SHARED_BUFFER

    shaderDefines.push_back( { NULL, NULL } );

    ComputeShaderPtr shader( ComputeShader::CreateFromFile( L"Shaders\\BxDFTexturesBuilding.hlsl", shaderDefines ) );
    return shader;
}

BxDFTexturesBuilding::STextures BxDFTexturesBuilding::Build()
{
    const uint32_t sampleCountPerBatch = 4096;
    const uint32_t batchCount = 5;
    const uint32_t totalSampleCount = batchCount * sampleCountPerBatch;

    SIntegralKernelParams kernelParams;
    kernelParams.m_LutIntervalX = 1.0f / ( BXDFTEX_COOKTORRANCE_E_SIZE_X - 1 );
    kernelParams.m_LutIntervalY = 1.0f / ( BXDFTEX_COOKTORRANCE_E_SIZE_Y - 1 );
    kernelParams.m_LutIntervalZ = 1.0f;
    kernelParams.m_SampleCount = sampleCountPerBatch;
    kernelParams.m_SampleWeight = 1.0f / totalSampleCount;
    kernelParams.m_GroupSizeX = BXDFTEX_COOKTORRANCE_E_SIZE_X;
    kernelParams.m_GroupSizeY = BXDFTEX_COOKTORRANCE_E_SIZE_Y;

    BxDFTexturesBuilding::STextures outputTextures;

    ComputeShaderPtr integralShader = CompileAndCreateKernel( "INTEGRATE_COOKTORRANCE_BRDF", kernelParams );
    ComputeShaderPtr copyShader = CompileAndCreateKernel( "COPY", kernelParams );
    ComputeShaderPtr averageShader = CompileAndCreateKernel( "INTEGRATE_AVERAGE", kernelParams );

    if ( integralShader && copyShader && averageShader )
    {
        SCOPED_RENDER_ANNOTATION( L"Integrate CookTorrance BRDF" );

        GPUTexturePtr accumulationTexture( GPUTexture::Create( BXDFTEX_COOKTORRANCE_E_SIZE_X, BXDFTEX_COOKTORRANCE_E_SIZE_Y, DXGI_FORMAT_R32_FLOAT, GPUResourceCreationFlags_HasUAV, 1 ) );
        outputTextures.m_CookTorranceBRDF.reset( GPUTexture::Create( BXDFTEX_COOKTORRANCE_E_SIZE_X, BXDFTEX_COOKTORRANCE_E_SIZE_Y, DXGI_FORMAT_R16_UNORM, GPUResourceCreationFlags_HasUAV, 1 ) );
        outputTextures.m_CookTorranceBRDFAverage.reset( GPUTexture::Create( BXDFTEX_COOKTORRANCE_E_SIZE_Y, 1, DXGI_FORMAT_R16_UNORM, GPUResourceCreationFlags_HasUAV, 1 ) );

        GPUBufferPtr batchConstantBuffers[ batchCount ];
        ComputeJob batchJob;
        batchJob.m_Shader = integralShader.get();
        batchJob.m_DispatchSizeX = BXDFTEX_COOKTORRANCE_E_SIZE_X;
        batchJob.m_DispatchSizeY = BXDFTEX_COOKTORRANCE_E_SIZE_Y;
        batchJob.m_DispatchSizeZ = 1;

        for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
        {
            uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 0, 0 };
            batchConstantBuffers[ batchIndex ].reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, GPUResourceCreationFlags_None, initData ) );

            batchJob.m_UAVs.clear();
            batchJob.m_UAVs.push_back( accumulationTexture->GetUAV() );
            batchJob.m_ConstantBuffers.clear();
            batchJob.m_ConstantBuffers.push_back( batchConstantBuffers[ batchIndex ]->GetBuffer() );
            batchJob.Dispatch();
        }

        batchJob.m_Shader = copyShader.get();
        batchJob.m_UAVs.clear();
        batchJob.m_UAVs.push_back( outputTextures.m_CookTorranceBRDF->GetUAV() );
        batchJob.m_SRVs.push_back( accumulationTexture->GetSRV() );
        batchJob.Dispatch();
        
        batchJob.m_Shader = averageShader.get();
        batchJob.m_UAVs.clear();
        batchJob.m_UAVs.push_back( outputTextures.m_CookTorranceBRDFAverage->GetUAV() );
        batchJob.Dispatch();
    }

    return outputTextures;
}
