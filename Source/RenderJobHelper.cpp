#include "stdafx.h"
#include "RenderJobHelper.h"
#include "D3D11RenderSystem.h"
#include "Shader.h"
#include "GPUBuffer.h"

const uint32_t      s_NullPointersListLength = 32;
ID3D11DeviceChild*  s_NullPointersList[ s_NullPointersListLength ] = { nullptr };

void RenderJobHelper::DispatchCompute( uint32_t dispatchSizeX, uint32_t dispatchSizeY, uint32_t dispatchSizeZ, ComputeShader* shader, const ResourceViewList& SRVs, const UnorderedAccessViewList& UAVs, const SamplerStateList& samplers, const BufferList& constantBuffers )
{
    assert( SRVs.m_Count <= s_NullPointersListLength );
    assert( UAVs.m_Count <= s_NullPointersListLength );
    assert( constantBuffers.m_Count <= s_NullPointersListLength );

    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    deviceContext->CSSetShader( shader->GetNative(), nullptr, 0 );

    if ( samplers.m_Count > 0 )
    {
        deviceContext->CSSetSamplers( 0, samplers.m_Count, samplers.m_Address );
    }
    if ( SRVs.m_Count > 0 )
    {
        deviceContext->CSSetShaderResources( 0, SRVs.m_Count, SRVs.m_Address );
    }
    if ( UAVs.m_Count > 0 )
    {
        deviceContext->CSSetUnorderedAccessViews( 0, UAVs.m_Count, UAVs.m_Address, nullptr );
    }
    if ( constantBuffers.m_Count > 0 )
    {
        deviceContext->CSSetConstantBuffers( 0, constantBuffers.m_Count, constantBuffers.m_Address );
    }

    deviceContext->Dispatch( dispatchSizeX, dispatchSizeY, dispatchSizeZ );

    if ( SRVs.m_Count > 0 )
    {
        deviceContext->CSSetShaderResources( 0, SRVs.m_Count, (ID3D11ShaderResourceView**)s_NullPointersList );
    }
    if ( UAVs.m_Count > 0 )
    {
        deviceContext->CSSetUnorderedAccessViews( 0, UAVs.m_Count, (ID3D11UnorderedAccessView**)s_NullPointersList, nullptr );
    }
    if ( constantBuffers.m_Count > 0 )
    {
        deviceContext->CSSetConstantBuffers( 0, constantBuffers.m_Count, (ID3D11Buffer**)s_NullPointersList );
    }
}

void RenderJobHelper::DispatchComputeIndirect( ID3D11Buffer* indirectBuffer, ComputeShader* shader, const ResourceViewList& SRVs, const UnorderedAccessViewList& UAVs, const SamplerStateList& samplers, const BufferList& constantBuffers )
{
    assert( SRVs.m_Count <= s_NullPointersListLength );
    assert( UAVs.m_Count <= s_NullPointersListLength );
    assert( constantBuffers.m_Count <= s_NullPointersListLength );

    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    deviceContext->CSSetShader( shader->GetNative(), nullptr, 0 );

    if ( samplers.m_Count > 0 )
    {
        deviceContext->CSSetSamplers( 0, samplers.m_Count, samplers.m_Address );
    }
    if ( SRVs.m_Count > 0 )
    {
        deviceContext->CSSetShaderResources( 0, SRVs.m_Count, SRVs.m_Address );
    }
    if ( UAVs.m_Count > 0 )
    {
        deviceContext->CSSetUnorderedAccessViews( 0, UAVs.m_Count, UAVs.m_Address, nullptr );
    }
    if ( constantBuffers.m_Count > 0 )
    {
        deviceContext->CSSetConstantBuffers( 0, constantBuffers.m_Count, constantBuffers.m_Address );
    }

    deviceContext->DispatchIndirect( indirectBuffer, 0 );

    if ( SRVs.m_Count > 0 )
    {
        deviceContext->CSSetShaderResources( 0, SRVs.m_Count, (ID3D11ShaderResourceView**)s_NullPointersList );
    }
    if ( UAVs.m_Count > 0 )
    {
        deviceContext->CSSetUnorderedAccessViews( 0, UAVs.m_Count, (ID3D11UnorderedAccessView**)s_NullPointersList, nullptr );
    }
    if ( constantBuffers.m_Count > 0 )
    {
        deviceContext->CSSetConstantBuffers( 0, constantBuffers.m_Count, (ID3D11Buffer**)s_NullPointersList );
    }
}

void RenderJobHelper::DispatchDraw( GPUBuffer* vertexBuffer, GfxShader* shader, ID3D11InputLayout* inputLayout, const ResourceViewList& SRVs, const SamplerStateList& samplers, const BufferList& constantBuffers, uint32_t vertexCount, uint32_t vertexStride )
{
    assert( SRVs.m_Count <= s_NullPointersListLength );
    assert( constantBuffers.m_Count <= s_NullPointersListLength );

    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    ID3D11Buffer* nativeVertexBuffer = vertexBuffer->GetBuffer();
    uint32_t vertexOffset = 0;
    deviceContext->IASetVertexBuffers( 0, 1, &nativeVertexBuffer, &vertexStride, &vertexOffset );
    deviceContext->IASetInputLayout( inputLayout );
    deviceContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    deviceContext->VSSetShader( shader->GetVertexShader(), nullptr, 0 );
    deviceContext->PSSetShader( shader->GetPixelShader(), nullptr, 0 );

    if ( samplers.m_Count > 0 )
    {
        deviceContext->PSSetSamplers( 0, samplers.m_Count, samplers.m_Address );
    }
    if ( SRVs.m_Count > 0 )
    {
        deviceContext->PSSetShaderResources( 0, SRVs.m_Count, SRVs.m_Address );
    }
    if ( constantBuffers.m_Count > 0 )
    {
        deviceContext->VSSetConstantBuffers( 0, constantBuffers.m_Count, constantBuffers.m_Address );
        deviceContext->PSSetConstantBuffers( 0, constantBuffers.m_Count, constantBuffers.m_Address );
    }

    deviceContext->Draw( vertexCount, 0 );

    if ( SRVs.m_Count > 0 )
    {
        deviceContext->PSSetShaderResources( 0, SRVs.m_Count, (ID3D11ShaderResourceView**)s_NullPointersList );
    }
    if ( constantBuffers.m_Count > 0 )
    {
        deviceContext->VSSetConstantBuffers( 0, constantBuffers.m_Count, (ID3D11Buffer**)s_NullPointersList );
        deviceContext->PSSetConstantBuffers( 0, constantBuffers.m_Count, (ID3D11Buffer**)s_NullPointersList );
    }
}
