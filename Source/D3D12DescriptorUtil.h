#pragma once

#include "D3D12Adapter.h"
#include "D3D12DescriptorHandle.h"

namespace D3D12Util
{
    inline void CopyDescriptors( SD3D12DescriptorHandle dstDescriptor, const SD3D12DescriptorHandle* srcDescriptors, uint32_t count, uint32_t descriptorSize, D3D12_DESCRIPTOR_HEAP_TYPE type )
    {
        for ( uint32_t i = 0 ; i < count; ++i )
        {
            const SD3D12DescriptorHandle& srcDescriptor = srcDescriptors[ i ];
            D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, dstDescriptor.CPU, srcDescriptor.CPU, type );
            dstDescriptor.Offset( 1, descriptorSize );
        }
    }

    inline void CopyToDescriptorTable( SD3D12DescriptorHandle descriptorTable, const SD3D12DescriptorHandle** rangeSrcDescriptors, const uint32_t* rangeOffsets, const uint32_t* rangeSizes, uint32_t rangeCount, D3D12_DESCRIPTOR_HEAP_TYPE type )
    {
        const uint32_t descriptorSize = D3D12Adapter::GetDescriptorSize( type );
        for ( uint32_t i = 0; i < rangeCount; ++i )
        {
            uint32_t rangeOffset = rangeOffsets[ i ];
            uint32_t rangeSize = rangeSizes[ i ];
            SD3D12DescriptorHandle dstDescriptor = descriptorTable.Offsetted( rangeOffset, descriptorSize );
            const SD3D12DescriptorHandle* srcDescriptors = rangeSrcDescriptors[ i ];
            CopyDescriptors( dstDescriptor, srcDescriptors, rangeSize, descriptorSize, type );
        }
    }

    struct SD3D12DescriptorTableRanges
    {
        static const uint32_t s_MaxRangesCount = 2;
        CD3DX12_DESCRIPTOR_RANGE1 m_Array[ s_MaxRangesCount ];
    };

    struct SD3D12DescriptorTableLayout
    {
        SD3D12DescriptorTableLayout() = default;

        SD3D12DescriptorTableLayout( uint32_t SRVCount, uint32_t UAVCount )
            : m_SRVCount( SRVCount )
            , m_UAVCount( UAVCount )
        {
        }

        void InitRootParameter( CD3DX12_ROOT_PARAMETER1* rootParameter, SD3D12DescriptorTableRanges* ranges ) const
        {
            uint32_t rangesCount = 0;
            if ( m_SRVCount )
            { 
                ranges->m_Array[ rangesCount++ ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_SRVCount, 0 );
            }
            if ( m_UAVCount )
            { 
                ranges->m_Array[ rangesCount++ ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, m_UAVCount, 0 );
            }
            rootParameter->InitAsDescriptorTable( rangesCount, ranges->m_Array );
        }

        D3D12_GPU_DESCRIPTOR_HANDLE AllocateAndCopyToGPUDescriptorHeap( const SD3D12DescriptorHandle* SRVs, uint32_t SRVCount, const SD3D12DescriptorHandle* UAVs, uint32_t UAVCount );

        D3D12_GPU_DESCRIPTOR_HANDLE AllocateAndCopyToGPUDescriptorHeap( const SD3D12DescriptorHandle* descriptors, uint32_t count );

        uint32_t m_SRVCount = 0;
        uint32_t m_UAVCount = 0;
    };
}
