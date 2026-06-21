#pragma once

class CScene;

struct SImageReadback
{
    bool ReadbackRenderResult( CScene* scene );
    void SaveToFile( const wchar_t* filepath ) const;

    ComPtr<ID3D12Resource> m_Buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_Footprint = {};
    UINT64 m_SizeInBytes = 0;
};
