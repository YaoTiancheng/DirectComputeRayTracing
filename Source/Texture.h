#pragma once

struct STextureCodec;

enum class ETexturePixelFormat
{
    Unknown,
    R8G8B8A8_sRGB,
};

uint32_t GetTexturePixelFormatBPP( ETexturePixelFormat format );

class CTexture
{
public:
    static STextureCodec* CreateCodec();

    static void DestroyCodec( STextureCodec* codec );

    bool LoadFromFile( const char* filename, STextureCodec* codec );

    void Clear();

    bool IsValid() const { return m_PixelFormat != ETexturePixelFormat::Unknown; }

    uint32_t CalculateRowPitch() const 
    {
        return GetTexturePixelFormatBPP( m_PixelFormat ) * m_Width;
    }

    std::string m_Name;
    std::vector<uint8_t> m_PixelData;
    ETexturePixelFormat m_PixelFormat;
    uint32_t m_Width;
    uint32_t m_Height;
};