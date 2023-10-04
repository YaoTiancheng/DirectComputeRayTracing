
#include "stdafx.h"
#include "BxDFTexturesBuilding.h"
#include "Shader.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "ComputeJob.h"
#include "ScopedRenderAnnotation.h"
#include "../Shaders/BxDFTextureDef.inc.hlsl"

struct SKernelCompilationParams
{
    float m_LutIntervalX;
    float m_LutIntervalY;
    float m_LutIntervalZ;
    float m_LutStartZ;
    float m_SampleWeight;
    uint32_t m_SampleCount;
    uint32_t m_GroupSizeX;
    uint32_t m_GroupSizeY;
    uint32_t m_GroupSizeZ;
    bool m_HasFresnel;
};

static ComputeShaderPtr CompileAndCreateKernel( const char* kernelName, const SKernelCompilationParams& params )
{
    std::vector<D3D_SHADER_MACRO> shaderDefines;

    if ( kernelName )
    {
        shaderDefines.push_back( { kernelName, "" } );
    }

    if ( params.m_HasFresnel )
    {
        shaderDefines.push_back( { "HAS_FRESNEL", "" } );
    }

    char sharedBuffer[ 512 ];
    int sharedBufferStart = 0;
    const int sharedBufferSize = sizeof( sharedBuffer );
#define ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( name, value, format ) \
    if ( sharedBufferStart < sharedBufferSize - 1 ) \
    { \
        int len = sprintf_s( sharedBuffer + sharedBufferStart, sharedBufferSize - sharedBufferStart, format, value ); \
        if ( len > 0 ) \
        { \
            shaderDefines.push_back( { name, sharedBuffer + sharedBufferStart } ); \
            sharedBufferStart += len + 1; \
        } \
        else \
        { \
            assert( "Failed to format the parameter to shared buffer!" ); \
        } \
    } \
    else \
    { \
        assert( "Shared buffer is too small!" ); \
    }

    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "LUT_INTERVAL_X", params.m_LutIntervalX, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "LUT_INTERVAL_Y", params.m_LutIntervalY, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "LUT_INTERVAL_Z", params.m_LutIntervalZ, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "LUT_START_Z", params.m_LutStartZ, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "SAMPLE_WEIGHT", params.m_SampleWeight, "%f" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "SAMPLE_COUNT", params.m_SampleCount, "%d" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "GROUP_SIZE_X", params.m_GroupSizeX, "%d" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "GROUP_SIZE_Y", params.m_GroupSizeY, "%d" );
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "GROUP_SIZE_Z", params.m_GroupSizeZ, "%d" );
#undef ADD_SHADER_DEFINE_WITH_SHARED_BUFFER

    shaderDefines.push_back( { NULL, NULL } );

    ComputeShaderPtr shader( ComputeShader::CreateFromFile( L"Shaders\\BxDFTexturesBuilding.hlsl", shaderDefines ) );
    return shader;
}

BxDFTexturesBuilding::STextures BxDFTexturesBuilding::Build()
{
    BxDFTexturesBuilding::STextures outputTextures;

    {
        const uint32_t sampleCountPerBatch = 4096;
        const uint32_t batchCount = 5;
        const uint32_t totalSampleCount = batchCount * sampleCountPerBatch;

        SKernelCompilationParams compilationParams;
        compilationParams.m_LutIntervalX = 1.0f / ( BXDFTEX_COOKTORRANCE_E_SIZE_X - 1 );
        compilationParams.m_LutIntervalY = 1.0f / ( BXDFTEX_COOKTORRANCE_E_SIZE_Y - 1 );
        compilationParams.m_LutIntervalZ = 1.0f;
        compilationParams.m_SampleCount = sampleCountPerBatch;
        compilationParams.m_SampleWeight = 1.0f / totalSampleCount;
        compilationParams.m_GroupSizeX = BXDFTEX_COOKTORRANCE_E_SIZE_X;
        compilationParams.m_GroupSizeY = BXDFTEX_COOKTORRANCE_E_SIZE_Y;
        compilationParams.m_GroupSizeZ = 1;
        compilationParams.m_HasFresnel = false;

        ComputeShaderPtr integralShader = CompileAndCreateKernel( "INTEGRATE_COOKTORRANCE_BRDF", compilationParams );
        ComputeShaderPtr copyShader = CompileAndCreateKernel( "COPY", compilationParams );
        ComputeShaderPtr averageShader = CompileAndCreateKernel( "INTEGRATE_AVERAGE", compilationParams );

        if ( integralShader && copyShader && averageShader )
        {
            SCOPED_RENDER_ANNOTATION( L"Integrate CookTorrance BRDF" );

            GPUTexturePtr accumulationTexture( GPUTexture::Create( BXDFTEX_COOKTORRANCE_E_SIZE_X, BXDFTEX_COOKTORRANCE_E_SIZE_Y, DXGI_FORMAT_R32_FLOAT, GPUResourceCreationFlags_HasUAV, 1, nullptr, "CookTorranceBRDF Accumulation" ) );
            outputTextures.m_CookTorranceBRDF.reset( GPUTexture::Create( BXDFTEX_COOKTORRANCE_E_SIZE_X, BXDFTEX_COOKTORRANCE_E_SIZE_Y, DXGI_FORMAT_R16_UNORM, GPUResourceCreationFlags_HasUAV, 1, nullptr, "CookTorranceBRDF" ) );
            outputTextures.m_CookTorranceBRDFAverage.reset( GPUTexture::Create( BXDFTEX_COOKTORRANCE_E_SIZE_Y, 1, DXGI_FORMAT_R16_UNORM, GPUResourceCreationFlags_HasUAV, 1, nullptr, "CookTorranceBRDFAverage" ) );

            GPUBufferPtr batchConstantBuffers[ batchCount ];
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
                uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 0, 0 };
                batchConstantBuffers[ batchIndex ].reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, GPUResourceCreationFlags_None, initData ) );
            }

            ComputeJob batchJob;
            batchJob.m_DispatchSizeX = BXDFTEX_COOKTORRANCE_E_SIZE_X;
            batchJob.m_DispatchSizeY = BXDFTEX_COOKTORRANCE_E_SIZE_Y;
            batchJob.m_DispatchSizeZ = 1;

            batchJob.m_Shader = integralShader.get();
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
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
    }

    {
        const uint32_t sampleCountPerBatch = 4096;
        const uint32_t batchCount = 5;
        const uint32_t totalSampleCount = batchCount * sampleCountPerBatch;
        const uint32_t groupSizeX = 16;
        const uint32_t groupSizeY = 16;
        const uint32_t groupSizeZ = 4;
        const float fresnelStart = 1.0f;
        const float fresnelEnd = 3.0f;

        SKernelCompilationParams compilationParams;
        compilationParams.m_LutIntervalX = 1.0f / ( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_X - 1 );
        compilationParams.m_LutIntervalY = 1.0f / ( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y - 1 );
        compilationParams.m_LutIntervalZ = ( fresnelEnd - fresnelStart ) / ( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z - 1 );
        compilationParams.m_LutStartZ = fresnelStart;
        compilationParams.m_SampleCount = sampleCountPerBatch;
        compilationParams.m_SampleWeight = 1.0f / totalSampleCount;
        compilationParams.m_GroupSizeX = groupSizeX;
        compilationParams.m_GroupSizeY = groupSizeY;
        compilationParams.m_GroupSizeZ = groupSizeZ;
        compilationParams.m_HasFresnel = true;

        ComputeShaderPtr integralShader = CompileAndCreateKernel( "INTEGRATE_COOKTORRANCE_BRDF", compilationParams );
        ComputeShaderPtr copyShader = CompileAndCreateKernel( "COPY", compilationParams );
        if ( integralShader && copyShader )
        {
            SCOPED_RENDER_ANNOTATION( L"Integrate CookTorrance BRDF Dielectric" );

            GPUTexturePtr accumulationTexture( GPUTexture::Create( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_X, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y, DXGI_FORMAT_R32_FLOAT, GPUResourceCreationFlags_HasUAV, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z * 2, nullptr, "CookTorranceBRDFDielectric Accumulation" ) );
            outputTextures.m_CookTorranceBRDFDielectric.reset( GPUTexture::Create( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_X, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y, DXGI_FORMAT_R16_UNORM, GPUResourceCreationFlags_HasUAV, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z * 2, nullptr, "CookTorranceBRDFDielectric" ) );
            
            GPUBufferPtr batchConstantBuffers[ batchCount * 2 ];
            // Leaving
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
                uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 0, 0 };
                batchConstantBuffers[ batchIndex ].reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, GPUResourceCreationFlags_None, initData ) );
            }
            // Entering
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
                uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 1, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z };
                batchConstantBuffers[ batchIndex + batchCount ].reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, GPUResourceCreationFlags_None, initData ) );
            }

            ComputeJob batchJob;
            assert( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_X % groupSizeX == 0 );
            assert( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y % groupSizeY == 0 );
            assert( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z % groupSizeZ == 0 );
            batchJob.m_DispatchSizeX = BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_X / groupSizeX;
            batchJob.m_DispatchSizeY = BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y / groupSizeY;
            batchJob.m_DispatchSizeZ = BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z / groupSizeZ;

            batchJob.m_Shader = integralShader.get();
            for ( uint32_t jobIndex = 0; jobIndex < batchCount * 2; ++jobIndex )
            {
                batchJob.m_UAVs.clear();
                batchJob.m_UAVs.push_back( accumulationTexture->GetUAV() );
                batchJob.m_ConstantBuffers.clear();
                batchJob.m_ConstantBuffers.push_back( batchConstantBuffers[ jobIndex ]->GetBuffer() );
                batchJob.Dispatch();
            }

            batchJob.m_Shader = copyShader.get();
            batchJob.m_DispatchSizeZ = BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z * 2 / groupSizeZ;
            batchJob.m_UAVs.clear();
            batchJob.m_UAVs.push_back( outputTextures.m_CookTorranceBRDFDielectric->GetUAV() );
            batchJob.m_SRVs.push_back( accumulationTexture->GetSRV() );
            batchJob.Dispatch();
        }
    }

    return outputTextures;
}
