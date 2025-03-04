
#include "stdafx.h"
#include "BxDFTexturesBuilding.h"
#include "Shader.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "D3D12Adapter.h"
#include "D3D12DescriptorUtil.h"
#include "D3D12Resource.h"
#include "ScopedRenderAnnotation.h"
#include "MathHelper.h"
#include "Logging.h"
#include "../Shaders/BxDFTextureDef.inc.hlsl"

using namespace D3D12Util;

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
    uint32_t m_BxDFType;
    bool m_HasFresnel;
};

static SD3D12DescriptorTableLayout s_DescriptorTableLayout = SD3D12DescriptorTableLayout( 1, 1 );

static CD3D12ComPtr<ID3D12PipelineState> CompileAndCreateKernel( const char* kernelName, const SKernelCompilationParams& params, ID3D12RootSignature* rootSignature )
{
    std::vector<D3D_SHADER_MACRO> shaderDefines;

    shaderDefines.push_back( { "GGX_SAMPLE_VNDF", "" } );

    if ( kernelName )
    {
        shaderDefines.push_back( { kernelName, "" } );
    }

    if ( params.m_HasFresnel )
    {
        shaderDefines.push_back( { "HAS_FRESNEL", "" } );
    }

    if ( params.m_BxDFType == 1 )
    {
        shaderDefines.push_back( { "REFRACTION_NO_SCALE_FACTOR", "" } );
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
    ADD_SHADER_DEFINE_WITH_SHARED_BUFFER( "BXDF_TYPE", params.m_BxDFType, "%d" );
#undef ADD_SHADER_DEFINE_WITH_SHARED_BUFFER

    shaderDefines.push_back( { NULL, NULL } );

    ComputeShaderPtr shader( ComputeShader::CreateFromFile( L"Shaders\\BxDFTexturesBuilding.hlsl", shaderDefines ) );
    if ( !shader )
    {
        return CD3D12ComPtr<ID3D12PipelineState>();
    }

    // Create PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.CS = shader->GetShaderBytecode();
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ID3D12PipelineState* PSO = nullptr;
    D3D12Adapter::GetDevice()->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( &PSO ) );

    return CD3D12ComPtr<ID3D12PipelineState>( PSO );
}

SBxDFTextures BxDFTexturesBuilding::Build()
{
    SBxDFTextures outputTextures;

    CD3D12ComPtr<ID3D12RootSignature> rootSignature;
    {
        CD3DX12_ROOT_PARAMETER1 rootParameters[ 2 ];
        rootParameters[ 0 ].InitAsConstantBufferView( 0 );
        s_DescriptorTableLayout.InitRootParameter( &rootParameters[ 1 ] );
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 2, rootParameters );

        ComPtr<ID3DBlob> serializedRootSignature;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() );
        LOG_STRING_FORMAT( "Create BxDFTextureBuilding root signature with error: %s\n", (const char*)error->GetBufferPointer() );
        if ( FAILED( hr ) )
        {
            return outputTextures;
        }

        ID3D12RootSignature* D3D12RootSignature = nullptr;
        if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS( &D3D12RootSignature ) ) ) )
        {
            return outputTextures;
        }

        rootSignature.Reset( D3D12RootSignature );
    }

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    {
        const uint32_t sampleCountPerBatch = 4096;
        const uint32_t batchCount = 5;
        const uint32_t totalSampleCount = batchCount * sampleCountPerBatch;

        SKernelCompilationParams compilationParams;
        compilationParams.m_LutIntervalX = 1.0f / ( BXDFTEX_BRDF_SIZE_X - 1 );
        compilationParams.m_LutIntervalY = 1.0f / ( BXDFTEX_BRDF_SIZE_Y - 1 );
        compilationParams.m_LutIntervalZ = 1.0f;
        compilationParams.m_SampleCount = sampleCountPerBatch;
        compilationParams.m_SampleWeight = 1.0f / totalSampleCount;
        compilationParams.m_GroupSizeX = BXDFTEX_BRDF_SIZE_X;
        compilationParams.m_GroupSizeY = BXDFTEX_BRDF_SIZE_Y;
        compilationParams.m_GroupSizeZ = 1;
        compilationParams.m_BxDFType = 0;
        compilationParams.m_HasFresnel = false;

        CD3D12ComPtr<ID3D12PipelineState> integralShader = CompileAndCreateKernel( "INTEGRATE_COOKTORRANCE_BXDF", compilationParams, rootSignature.Get() );
        CD3D12ComPtr<ID3D12PipelineState> copyShader = CompileAndCreateKernel( "COPY", compilationParams, rootSignature.Get() );

        compilationParams.m_SampleCount = BXDFTEX_BRDF_SIZE_X;
        CD3D12ComPtr<ID3D12PipelineState> averageShader = CompileAndCreateKernel( "INTEGRATE_AVERAGE", compilationParams, rootSignature.Get() );

        commandList->SetComputeRootSignature( rootSignature.Get() );

        if ( integralShader && copyShader && averageShader )
        {
            SCOPED_RENDER_ANNOTATION( commandList, L"Integrate CookTorrance BRDF" );

            CD3D12ResourcePtr<GPUTexture> accumulationTexture( GPUTexture::Create( BXDFTEX_BRDF_SIZE_X, BXDFTEX_BRDF_SIZE_Y, DXGI_FORMAT_R32_FLOAT, EGPUTextureBindFlag_UnorderedAccess,
                1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"CookTorranceBRDF Accumulation" ) );
            outputTextures.m_CookTorranceBRDF.reset( GPUTexture::Create( BXDFTEX_BRDF_SIZE_X, BXDFTEX_BRDF_SIZE_Y, DXGI_FORMAT_R16_UNORM, EGPUTextureBindFlag_UnorderedAccess,
                1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"CookTorranceBRDF" ) );
            outputTextures.m_CookTorranceBRDFAverage.reset( GPUTexture::Create( BXDFTEX_BRDF_SIZE_Y, 1, DXGI_FORMAT_R16_UNORM, EGPUTextureBindFlag_UnorderedAccess,
                1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"CookTorranceBRDFAverage" ) );

            CD3D12ResourcePtr<GPUBuffer> batchConstantBuffers[ batchCount ];
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
                uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 0, 0 };
                batchConstantBuffers[ batchIndex ].Reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Default,
                    EGPUBufferBindFlag_ConstantBuffer, initData, D3D12_RESOURCE_STATE_COPY_DEST ) );
            }

            // 1. Integral
            {
                commandList->SetPipelineState( integralShader.Get() );

                D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( nullptr, 0, &accumulationTexture->GetUAV(), 1 );
                commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

                for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
                {
                    commandList->SetComputeRootConstantBufferView( 0, batchConstantBuffers[ batchIndex ]->GetGPUVirtualAddress() );
                    D3D12_RESOURCE_BARRIER barriers[ 2 ] = 
                    {
                        CD3DX12_RESOURCE_BARRIER::Transition( batchConstantBuffers[ batchIndex ]->GetBuffer(),
                            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ),
                        CD3DX12_RESOURCE_BARRIER::UAV( accumulationTexture->GetTexture() ),
                    };
                    commandList->ResourceBarrier( batchIndex == 0 ? 1 : 2, barriers ); // No UAV barrier for the first batch
                    commandList->Dispatch( 1, 1, 1 );
                }
            }

            // 2. Copy
            {
                commandList->SetPipelineState( copyShader.Get() );

                D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable =
                    s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &accumulationTexture->GetSRV(), 1, &outputTextures.m_CookTorranceBRDF->GetUAV(), 1 );
                commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

                D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( accumulationTexture->GetTexture(), 
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                commandList->ResourceBarrier( 1, &barrier );
                commandList->Dispatch( 1, 1, 1 );
            }

            // 3. Average
            {
                commandList->SetPipelineState( averageShader.Get() );

                D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable =
                    s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &accumulationTexture->GetSRV(), 1, &outputTextures.m_CookTorranceBRDFAverage->GetUAV(), 1 );
                commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

                commandList->Dispatch( 1, 1, 1 );
            }
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
        compilationParams.m_LutIntervalX = 1.0f / ( BXDFTEX_BRDF_DIELECTRIC_SIZE_X - 1 );
        compilationParams.m_LutIntervalY = 1.0f / ( BXDFTEX_BRDF_DIELECTRIC_SIZE_Y - 1 );
        compilationParams.m_LutIntervalZ = ( fresnelEnd - fresnelStart ) / ( BXDFTEX_BRDF_DIELECTRIC_SIZE_Z - 1 );
        compilationParams.m_LutStartZ = fresnelStart;
        compilationParams.m_SampleCount = sampleCountPerBatch;
        compilationParams.m_SampleWeight = 1.0f / totalSampleCount;
        compilationParams.m_GroupSizeX = groupSizeX;
        compilationParams.m_GroupSizeY = groupSizeY;
        compilationParams.m_GroupSizeZ = groupSizeZ;
        compilationParams.m_BxDFType = 0;
        compilationParams.m_HasFresnel = true;

        CD3D12ComPtr<ID3D12PipelineState> integralShader = CompileAndCreateKernel( "INTEGRATE_COOKTORRANCE_BXDF", compilationParams, rootSignature.Get() );
        CD3D12ComPtr<ID3D12PipelineState> copyShader = CompileAndCreateKernel( "COPY", compilationParams, rootSignature.Get() );
        if ( integralShader && copyShader )
        {
            SCOPED_RENDER_ANNOTATION( commandList, L"Integrate CookTorrance BRDF Dielectric" );

            CD3D12ResourcePtr<GPUTexture> accumulationTexture( GPUTexture::Create( BXDFTEX_BRDF_DIELECTRIC_SIZE_X, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y, DXGI_FORMAT_R32_FLOAT,
                EGPUTextureBindFlag_UnorderedAccess, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z * 2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"CookTorranceBRDFDielectric Accumulation" ) );
            outputTextures.m_CookTorranceBRDFDielectric.reset( GPUTexture::Create( BXDFTEX_BRDF_DIELECTRIC_SIZE_X, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y, DXGI_FORMAT_R16_UNORM,
                EGPUTextureBindFlag_UnorderedAccess, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z * 2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"CookTorranceBRDFDielectric" ) );
            
            CD3D12ResourcePtr<GPUBuffer> batchConstantBuffers[ batchCount * 2 ];
            // Leaving
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
                uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 0, 0 };
                batchConstantBuffers[ batchIndex ].Reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Default,
                    EGPUBufferBindFlag_ConstantBuffer, initData, D3D12_RESOURCE_STATE_COPY_DEST ) );
            }
            // Entering
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
                uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 1, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z };
                batchConstantBuffers[ batchIndex + batchCount ].Reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Default,
                    EGPUBufferBindFlag_ConstantBuffer, initData, D3D12_RESOURCE_STATE_COPY_DEST ) );
            }

            assert( BXDFTEX_BRDF_DIELECTRIC_SIZE_X % groupSizeX == 0 );
            assert( BXDFTEX_BRDF_DIELECTRIC_SIZE_Y % groupSizeY == 0 );
            assert( BXDFTEX_BRDF_DIELECTRIC_SIZE_Z % groupSizeZ == 0 );

            // 1. Integral
            {
                commandList->SetPipelineState( integralShader.Get() );

                D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( nullptr, 0, &accumulationTexture->GetUAV(), 1 );
                commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

                for ( uint32_t jobIndex = 0; jobIndex < batchCount * 2; ++jobIndex )
                {
                    commandList->SetComputeRootConstantBufferView( 0, batchConstantBuffers[ jobIndex ]->GetGPUVirtualAddress() );
                    D3D12_RESOURCE_BARRIER barriers[ 2 ] = 
                    {
                        CD3DX12_RESOURCE_BARRIER::Transition( batchConstantBuffers[ jobIndex ]->GetBuffer(),
                            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ),
                        CD3DX12_RESOURCE_BARRIER::UAV( accumulationTexture->GetTexture() ),
                    };
                    commandList->ResourceBarrier( jobIndex == 0 ? 1 : 2, barriers ); // No UAV barrier for the first batch
                    commandList->Dispatch( BXDFTEX_BRDF_DIELECTRIC_SIZE_X / groupSizeX, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y / groupSizeY, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z / groupSizeZ );
                }
            }

            // 2. Copy
            {
                commandList->SetPipelineState( copyShader.Get() );

                D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable =
                    s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &accumulationTexture->GetSRV(), 1, &outputTextures.m_CookTorranceBRDFDielectric->GetUAV(), 1 );
                commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

                D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( accumulationTexture->GetTexture(), 
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                commandList->ResourceBarrier( 1, &barrier );
                commandList->Dispatch( BXDFTEX_BRDF_DIELECTRIC_SIZE_X / groupSizeX, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y / groupSizeY, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z * 2 / groupSizeZ );
            }
        }
    }

    {
        const uint32_t sampleCountPerBatch = 4096;
        const uint32_t batchCount = 24;
        const uint32_t totalSampleCount = batchCount * sampleCountPerBatch;
        const uint32_t groupSizeX = 16;
        const uint32_t groupSizeY = 16;
        const uint32_t groupSizeZ = 4;
        const float fresnelStart = 1.0f;
        const float fresnelEnd = 3.0f;

        SKernelCompilationParams compilationParams;
        compilationParams.m_LutIntervalX = 1.0f / ( BXDFTEX_BRDF_DIELECTRIC_SIZE_X - 1 );
        compilationParams.m_LutIntervalY = 1.0f / ( BXDFTEX_BRDF_DIELECTRIC_SIZE_Y - 1 );
        compilationParams.m_LutIntervalZ = ( fresnelEnd - fresnelStart ) / ( BXDFTEX_BRDF_DIELECTRIC_SIZE_Z - 1 );
        compilationParams.m_LutStartZ = fresnelStart;
        compilationParams.m_SampleCount = sampleCountPerBatch;
        compilationParams.m_SampleWeight = 1.0f / totalSampleCount;
        compilationParams.m_GroupSizeX = groupSizeX;
        compilationParams.m_GroupSizeY = groupSizeY;
        compilationParams.m_GroupSizeZ = groupSizeZ;
        compilationParams.m_BxDFType = 1;
        compilationParams.m_HasFresnel = true;

        CD3D12ComPtr<ID3D12PipelineState> integralShader = CompileAndCreateKernel( "INTEGRATE_COOKTORRANCE_BXDF", compilationParams, rootSignature.Get() );
        CD3D12ComPtr<ID3D12PipelineState> copyShader = CompileAndCreateKernel( "COPY", compilationParams, rootSignature.Get() );

        compilationParams.m_SampleCount = BXDFTEX_BRDF_DIELECTRIC_SIZE_X;
        CD3D12ComPtr<ID3D12PipelineState> averageShader = CompileAndCreateKernel( "INTEGRATE_AVERAGE", compilationParams, rootSignature.Get() );

        if ( integralShader && copyShader && averageShader )
        {
            SCOPED_RENDER_ANNOTATION( commandList, L"Integrate CookTorrance BSDF" );

            CD3D12ResourcePtr<GPUTexture> accumulationTexture( GPUTexture::Create( BXDFTEX_BRDF_DIELECTRIC_SIZE_X, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y, DXGI_FORMAT_R32_FLOAT,
                EGPUTextureBindFlag_UnorderedAccess, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z * 2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"CookTorranceBSDF Accumulation" ) );
            outputTextures.m_CookTorranceBSDF.reset( GPUTexture::Create( BXDFTEX_BRDF_DIELECTRIC_SIZE_X, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y, DXGI_FORMAT_R16_UNORM,
                EGPUTextureBindFlag_UnorderedAccess, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z * 2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"CookTorranceBSDF" ) );
            outputTextures.m_CookTorranceBSDFAverage.reset( GPUTexture::Create( BXDFTEX_BRDF_DIELECTRIC_SIZE_Y, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z, DXGI_FORMAT_R16_UNORM,
                EGPUTextureBindFlag_UnorderedAccess, 2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"CookTorranceBSDFAverage" ) );

            CD3D12ResourcePtr<GPUBuffer> batchConstantBuffers[ batchCount * 2 ];
            // Leaving
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
                uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 0, 0 };
                batchConstantBuffers[ batchIndex ].Reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Default,
                    EGPUBufferBindFlag_ConstantBuffer, initData, D3D12_RESOURCE_STATE_COPY_DEST ) );
            }
            // Entering
            for ( uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
            {
                uint32_t initData[ 4 ] = { batchIndex, batchIndex == 0 ? 1u : 0, 1, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z };
                batchConstantBuffers[ batchIndex + batchCount ].Reset( GPUBuffer::Create( sizeof( initData ), 1, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Default,
                    EGPUBufferBindFlag_ConstantBuffer, initData, D3D12_RESOURCE_STATE_COPY_DEST ) );
            }

            assert( BXDFTEX_BRDF_DIELECTRIC_SIZE_X % groupSizeX == 0 );
            assert( BXDFTEX_BRDF_DIELECTRIC_SIZE_Y % groupSizeY == 0 );
            assert( BXDFTEX_BRDF_DIELECTRIC_SIZE_Z % groupSizeZ == 0 );

            // 1. Integral
            {
                commandList->SetPipelineState( integralShader.Get() );

                D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( nullptr, 0, &accumulationTexture->GetUAV(), 1 );
                commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

                for ( uint32_t jobIndex = 0; jobIndex < batchCount * 2; ++jobIndex )
                {
                    commandList->SetComputeRootConstantBufferView( 0, batchConstantBuffers[ jobIndex ]->GetGPUVirtualAddress() );
                    D3D12_RESOURCE_BARRIER barriers[ 2 ] = 
                    {
                        CD3DX12_RESOURCE_BARRIER::Transition( batchConstantBuffers[ jobIndex ]->GetBuffer(),
                            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ),
                        CD3DX12_RESOURCE_BARRIER::UAV( accumulationTexture->GetTexture() ),
                    };
                    commandList->ResourceBarrier( jobIndex == 0 ? 1 : 2, barriers ); // No UAV barrier for the first batch
                    commandList->Dispatch( BXDFTEX_BRDF_DIELECTRIC_SIZE_X / groupSizeX, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y / groupSizeY, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z / groupSizeZ );
                }
            }

            // 2. Copy
            {
                commandList->SetPipelineState( copyShader.Get() );

                D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable =
                    s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &accumulationTexture->GetSRV(), 1, &outputTextures.m_CookTorranceBSDF->GetUAV(), 1 );
                commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

                D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( accumulationTexture->GetTexture(), 
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                commandList->ResourceBarrier( 1, &barrier );
                commandList->Dispatch( BXDFTEX_BRDF_DIELECTRIC_SIZE_X / groupSizeX, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y / groupSizeY, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z * 2 / groupSizeZ );
            }

            // 3. Average
            {
                commandList->SetPipelineState( averageShader.Get() );

                D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable =
                    s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &accumulationTexture->GetSRV(), 1, &outputTextures.m_CookTorranceBSDFAverage->GetUAV(), 1 );
                commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

                commandList->Dispatch( 1, 
                    MathHelper::DivideAndRoundUp( (uint32_t)BXDFTEX_BRDF_DIELECTRIC_SIZE_Y, compilationParams.m_GroupSizeY ),
                    MathHelper::DivideAndRoundUp( (uint32_t)BXDFTEX_BRDF_DIELECTRIC_SIZE_Z * 2, compilationParams.m_GroupSizeZ ) );
            }
        }
    }

    // Transition all the result textures to read states
    {
        D3D12_RESOURCE_BARRIER barriers[ 5 ] = 
        {
            CD3DX12_RESOURCE_BARRIER::Transition( outputTextures.m_CookTorranceBRDF->GetTexture(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ),
            CD3DX12_RESOURCE_BARRIER::Transition( outputTextures.m_CookTorranceBRDFAverage->GetTexture(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ),
            CD3DX12_RESOURCE_BARRIER::Transition( outputTextures.m_CookTorranceBRDFDielectric->GetTexture(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ),
            CD3DX12_RESOURCE_BARRIER::Transition( outputTextures.m_CookTorranceBSDF->GetTexture(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ),
            CD3DX12_RESOURCE_BARRIER::Transition( outputTextures.m_CookTorranceBSDFAverage->GetTexture(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ),
        };
        commandList->ResourceBarrier( 5, barriers );
    }

    return outputTextures;
}
