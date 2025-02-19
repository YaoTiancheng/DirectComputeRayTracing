#pragma once

#include "D3D12Adapter.h"
#include "D3D12DescriptorHandle.h"

namespace D3D12Util
{
    inline void CopyDescriptors( CD3D12DescritorHandle dstDescriptor, const CD3D12DescritorHandle* srcDescriptors, uint32_t count, uint32_t descriptorSize )
    {
        for ( uint32_t i = 0 ; i < count; ++i )
        {
            const CD3D12DescritorHandle& srcDescriptor = srcDescriptors[ i ];
            D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, dstDescriptor.CPU, srcDescriptor.CPU, type );
            dstDescriptor.Offset( 1, descriptorSize );
        }
    }

    inline void CopyToDescriptorTable( CD3D12DescritorHandle descriptorTable, const const CD3D12DescritorHandle** rangeSrcDescriptors, const uint32_t* rangeOffsets, const uint32_t* rangeSizes, uint32_t rangeCount, D3D12_DESCRIPTOR_HEAP_TYPE type )
    {
        const uint32_t descriptorSize = D3D12Adapter::GetDescriptorSize( type );
        for ( uint32_t i = 0; i < rangeCount; ++i )
        {
            uint32_t rangeOffset = rangeOffsets[ i ];
            uint32_t rangeSize = rangeSizes[ i ];
            CD3D12DescritorHandle dstDescriptor = descriptorTable.Offsetted( rangeOffset, descriptorSize );
            const CD3D12DescritorHandle* srcDescriptors = rangeSrcDescriptors[ i ];
            CopyDescriptors( dstDescriptor, srcDescriptors, rangeSize, descriptorSize );
        }
    }
}
