#include "stdafx.h"
#include "SaveImageToFile.h"
#include "Scene.h"
#include "D3D12Adapter.h"
#include "GPUTexture.h"
#include "Logging.h"
#include <wincodec.h>
#include <wincodecsdk.h>

bool SImageReadback::ReadbackRenderResult( CScene* scene )
{
    ID3D12Resource* renderResultTexture = scene->m_RenderResultTexture->GetTexture();
    ID3D12Device* device = D3D12Adapter::GetDevice();

    D3D12_RESOURCE_DESC textureDesc = renderResultTexture->GetDesc();
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints( &textureDesc, 0, 1, 0, &m_Footprint, nullptr, nullptr, &totalBytes );

    ComPtr<ID3D12Resource> readbackBuffer;
    CD3DX12_HEAP_PROPERTIES heapProperties( D3D12_HEAP_TYPE_READBACK );
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( totalBytes );
    HRESULT hr = device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( readbackBuffer.GetAddressOf() ) );
    if ( FAILED( hr ) )
    {
        LOG_STRING_FORMAT( "Failed to create image readback buffer: %x\n", hr );
        return false;
    }

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    assert( scene->m_IsRenderResultTextureRead );

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readbackBuffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = m_Footprint;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = renderResultTexture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    commandList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );

    m_Buffer = readbackBuffer;
    m_SizeInBytes = totalBytes;
    return true;
}

static bool CopyReadbackToBGR( const SImageReadback& readback, std::vector<uint8_t>* outBGRPixels )
{
    const uint32_t width = readback.m_Footprint.Footprint.Width;
    const uint32_t height = readback.m_Footprint.Footprint.Height;
    const uint32_t bgrStride = width * 3;
    const UINT64 bgrSize = (UINT64)bgrStride * height;
    if ( bgrSize > (UINT64)std::numeric_limits<UINT>::max() )
    {
        LOG_STRING( "Image is too large to encode with WIC.\n" );
        return false;
    }

    uint8_t* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, (SIZE_T)readback.m_SizeInBytes };
    HRESULT hr = readback.m_Buffer->Map( 0, &readRange, reinterpret_cast<void**>( &mappedData ) );
    if ( FAILED( hr ) )
    {
        LOG_STRING_FORMAT( "Failed to map image readback buffer: %x\n", hr );
        return false;
    }

    outBGRPixels->resize( (size_t)bgrSize );
    for ( uint32_t y = 0; y < height; ++y )
    {
        const uint8_t* srcRow = mappedData + (SIZE_T)y * readback.m_Footprint.Footprint.RowPitch;
        uint8_t* dstRow = outBGRPixels->data() + (size_t)y * bgrStride;
        for ( uint32_t x = 0; x < width; ++x )
        {
            const uint8_t* srcPixel = srcRow + (size_t)x * 4;
            uint8_t* dstPixel = dstRow + (size_t)x * 3;
            dstPixel[ 0 ] = srcPixel[ 2 ];
            dstPixel[ 1 ] = srcPixel[ 1 ];
            dstPixel[ 2 ] = srcPixel[ 0 ];
        }
    }

    D3D12_RANGE writeRange = { 0, 0 };
    readback.m_Buffer->Unmap( 0, &writeRange );
    return true;
}

static bool SaveBGRPixelsToBMP( const wchar_t* filepath, uint32_t width, uint32_t height, const std::vector<uint8_t>& BGRPixels )
{
    HRESULT coInitializeHR = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
    const bool shouldUninitializeCOM = SUCCEEDED( coInitializeHR );
    if ( FAILED( coInitializeHR ) && coInitializeHR != RPC_E_CHANGED_MODE )
    {
        LOG_STRING_FORMAT( "Failed to initialize COM for WIC: %x\n", coInitializeHR );
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( factory.GetAddressOf() ) );
    if ( SUCCEEDED( hr ) )
    {
        ComPtr<IWICStream> stream;
        hr = factory->CreateStream( stream.GetAddressOf() );
        if ( SUCCEEDED( hr ) )
        {
            hr = stream->InitializeFromFilename( filepath, GENERIC_WRITE );
        }

        ComPtr<IWICBitmapEncoder> encoder;
        if ( SUCCEEDED( hr ) )
        {
            hr = factory->CreateEncoder( GUID_ContainerFormatBmp, nullptr, encoder.GetAddressOf() );
        }
        if ( SUCCEEDED( hr ) )
        {
            hr = encoder->Initialize( stream.Get(), WICBitmapEncoderNoCache );
        }

        ComPtr<IWICBitmapFrameEncode> frame;
        if ( SUCCEEDED( hr ) )
        {
            hr = encoder->CreateNewFrame( frame.GetAddressOf(), nullptr );
        }
        if ( SUCCEEDED( hr ) )
        {
            hr = frame->Initialize( nullptr );
        }
        if ( SUCCEEDED( hr ) )
        {
            hr = frame->SetSize( width, height );
        }
        if ( SUCCEEDED( hr ) )
        {
            WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
            hr = frame->SetPixelFormat( &pixelFormat );
            if ( SUCCEEDED( hr ) && !IsEqualGUID( pixelFormat, GUID_WICPixelFormat24bppBGR ) )
            {
                hr = E_FAIL;
            }
        }
        if ( SUCCEEDED( hr ) )
        {
            const uint32_t stride = width * 3;
            hr = frame->WritePixels( height, stride, (UINT)BGRPixels.size(), const_cast<BYTE*>( BGRPixels.data() ) );
        }
        if ( SUCCEEDED( hr ) )
        {
            hr = frame->Commit();
        }
        if ( SUCCEEDED( hr ) )
        {
            hr = encoder->Commit();
        }
    }

    if ( shouldUninitializeCOM )
    {
        CoUninitialize();
    }

    if ( FAILED( hr ) )
    {
        LOG_STRING_FORMAT( "Failed to save BMP image: %x\n", hr );
        return false;
    }
    return true;
}

void SImageReadback::SaveToFile( const wchar_t* filepath ) const
{
    std::vector<uint8_t> BGRPixels;
    if ( !CopyReadbackToBGR( *this, &BGRPixels ) )
    {
        return;
    }

    SaveBGRPixelsToBMP( filepath, m_Footprint.Footprint.Width, m_Footprint.Footprint.Height, BGRPixels );
}
