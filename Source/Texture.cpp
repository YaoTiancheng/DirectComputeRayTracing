#include "stdafx.h"
#include "Texture.h"
#include <wincodec.h>
#include <wincodecsdk.h>
#include "Logging.h"

struct STextureCodec
{
    ComPtr<IWICImagingFactory> m_WICFactory;
};

static ETexturePixelFormat GetTexturePixelFormat( WICPixelFormatGUID WICPixelFormat )
{
    if ( WICPixelFormat == GUID_WICPixelFormat32bppBGRA ||
        WICPixelFormat == GUID_WICPixelFormat32bppRGBA ||
        WICPixelFormat == GUID_WICPixelFormat24bppBGR ||
        WICPixelFormat == GUID_WICPixelFormat24bppRGB ||
        WICPixelFormat == GUID_WICPixelFormat32bppBGR ||
        WICPixelFormat == GUID_WICPixelFormat32bppRGB )
    {
        return ETexturePixelFormat::R8G8B8A8_sRGB;
    }
    return ETexturePixelFormat::Unknown;
}

static WICPixelFormatGUID GetWICPixelFormat( ETexturePixelFormat texturePixelFormat )
{
    switch ( texturePixelFormat )
    {
    case ETexturePixelFormat::R8G8B8A8_sRGB:
    {
        return GUID_WICPixelFormat32bppRGBA;
    }
    default:
        return GUID_WICPixelFormatDontCare;
    }
    return GUID_WICPixelFormatDontCare;
}

uint32_t GetTexturePixelFormatBPP( ETexturePixelFormat format )
{
    switch ( format )
    {
    case ETexturePixelFormat::R8G8B8A8_sRGB:
    {
        return 4;
    }
    default:
    {
        return 0;
    }
    }
    return 0;
}

STextureCodec* CTexture::CreateCodec()
{
    ComPtr<IWICImagingFactory> factory;
    if ( SUCCEEDED( CoCreateInstance( CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)factory.GetAddressOf() ) ) )
    {
        STextureCodec* codec = new STextureCodec();
        codec->m_WICFactory = factory;
        return codec;
    }
    return nullptr;
}

void CTexture::DestroyCodec( STextureCodec* codec )
{
    codec->m_WICFactory.Reset();
    delete codec;
}

bool CTexture::LoadFromFile( const char* filename, STextureCodec* codec )
{
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;

    wchar_t wideFilenameBuffer[ MAX_PATH ];
    if ( MultiByteToWideChar( CP_UTF8, MB_PRECOMPOSED, filename, -1, wideFilenameBuffer, MAX_PATH ) == 0 )
    {
        return false;
    }

    if ( FAILED( codec->m_WICFactory->CreateDecoderFromFilename( wideFilenameBuffer, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( decoder->GetFrame( 0, frame.GetAddressOf() ) ) )
    {
        return false;
    }

    UINT width, height;
    if ( FAILED( frame->GetSize( &width, &height ) ) )
    {
        return false;
    }

    WICPixelFormatGUID srcWICPixelFormat;
    if ( FAILED( frame->GetPixelFormat( &srcWICPixelFormat ) ) )
    {
        return false;
    }

    ETexturePixelFormat texturePixelFormat = GetTexturePixelFormat( srcWICPixelFormat );
    if ( texturePixelFormat == ETexturePixelFormat::Unknown )
    {
        LOG_STRING( "Unsupported texture pixel format.\n" );
        return false;
    }

    IWICBitmapSource* bitmapSource = frame.Get();

    ComPtr<IWICFormatConverter> convertedFrame;
    WICPixelFormatGUID dstWICPixelFormat = GetWICPixelFormat( texturePixelFormat );
    if ( dstWICPixelFormat != srcWICPixelFormat )
    {
        // Pixel format conversion
        if ( FAILED( codec->m_WICFactory->CreateFormatConverter( convertedFrame.GetAddressOf() ) ) )
        {
            return false;
        }

        if ( FAILED( convertedFrame->Initialize( frame.Get(), dstWICPixelFormat, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom ) ) )
        {
            return false;
        }

        bitmapSource = convertedFrame.Get();
    }

    const uint32_t BPP = GetTexturePixelFormatBPP( texturePixelFormat );
    const uint32_t byteSize = width * height * BPP;
    std::vector<uint8_t> pixelData;
    pixelData.resize( byteSize );
    if ( FAILED( convertedFrame->CopyPixels( nullptr, width * BPP, byteSize, (BYTE*)pixelData.data() ) ) )
    {
        return false;
    }

    m_PixelData = std::move( pixelData );
    m_PixelFormat = texturePixelFormat;
    m_Width = width;
    m_Height = height;

    return true;
}

void CTexture::Clear()
{
    m_PixelData.clear();
    m_PixelFormat = ETexturePixelFormat::Unknown;
    m_Width = 0;
    m_Height = 0;
}
