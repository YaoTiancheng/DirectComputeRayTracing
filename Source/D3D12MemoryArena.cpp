#include "stdafx.h"
#include "D3D12Adapter.h"
#include "D3D12MemoryArena.h"

template <typename TArena>
TD3D12ArenaMemoryLocation<TArena> TD3D12GrowingMemoryArena<TArena>::Allocate( const typename TArena::InitializerType& initializer, uint64_t byteSize, uint64_t alignment )
{
    if ( byteSize > initializer.SizeInBytes )
    {
        return TD3D12ArenaMemoryLocation<TArena>();
    }

    for ( auto& arena : m_Arenas )
    {
        const uint64_t address = arena.Allocate( byteSize, alignment );
        if ( address != 0 )
        {
            return TD3D12ArenaMemoryLocation<TArena>( arena.GetMemory(), address );
        }
    }

    m_Arenas.emplace_back();
    TArena& newArena = m_Arenas.back();
    if ( !newArena.Create( initializer ) )
    {
        m_Arenas.pop_back();
        return TD3D12ArenaMemoryLocation<TArena>();
    }

    TD3D12ArenaMemoryLocation<TArena> allocation;
    const uint64_t address = newArena.Allocate( byteSize, alignment );
    if ( address != 0 )
    {
        allocation.m_Memory = newArena.GetMemory();
        allocation.m_Offset = address;
    }
    return allocation;
}

template TD3D12ArenaMemoryLocation<CD3D12HeapArena> TD3D12GrowingMemoryArena<CD3D12HeapArena>::Allocate( const CD3D12HeapArena::InitializerType& initializer, uint64_t byteSize, uint64_t alignment );
template TD3D12ArenaMemoryLocation<CD3D12BufferArena> TD3D12GrowingMemoryArena<CD3D12BufferArena>::Allocate( const CD3D12BufferArena::InitializerType& initializer, uint64_t byteSize, uint64_t alignment );

template <typename TArena>
bool TD3D12BufferedGrowingMemoryArena<TArena>::Create( const typename TArena::InitializerType& initializer, uint32_t capacity /*= 0 */ )
{
    assert( m_Arenas.empty() );
    m_Arenas.resize( D3D12Adapter::GetBackbufferCount() );
    for ( auto& arena : m_Arenas )
    {
        arena = std::make_shared<TD3D12GrowingMemoryArena<TArena>>();
        if ( !arena->Create( initializer, capacity ) )
        {
            m_Arenas.clear();
            return false;
        }
    }
    m_Initializer = initializer;
    return true;
}

template bool TD3D12BufferedGrowingMemoryArena<CD3D12HeapArena>::Create( const CD3D12HeapArena::InitializerType& initializer, uint32_t capacity );
template bool TD3D12BufferedGrowingMemoryArena<CD3D12BufferArena>::Create( const CD3D12BufferArena::InitializerType& initializer, uint32_t capacity );

template <typename TArena>
void TD3D12BufferedGrowingMemoryArena<TArena>::Reset( uint32_t capacity /*= 0 */ )
{
    const uint32_t backbufferIndex = D3D12Adapter::GetBackbufferIndex();
    m_Arenas[ backbufferIndex ]->Reset( capacity );
}

template void TD3D12BufferedGrowingMemoryArena<CD3D12HeapArena>::Reset( uint32_t capacity );
template void TD3D12BufferedGrowingMemoryArena<CD3D12BufferArena>::Reset( uint32_t capacity );

template <typename TArena>
TD3D12ArenaMemoryLocation<TArena> TD3D12BufferedGrowingMemoryArena<TArena>::Allocate( uint64_t byteSize, uint64_t alignment )
{
    const uint32_t backbufferIndex = D3D12Adapter::GetBackbufferIndex();
    return m_Arenas[ backbufferIndex ]->Allocate( m_Initializer, byteSize, alignment );
}

template TD3D12ArenaMemoryLocation<CD3D12HeapArena> TD3D12BufferedGrowingMemoryArena<CD3D12HeapArena>::Allocate( uint64_t byteSize, uint64_t alignment );
template TD3D12ArenaMemoryLocation<CD3D12BufferArena> TD3D12BufferedGrowingMemoryArena<CD3D12BufferArena>::Allocate( uint64_t byteSize, uint64_t alignment );

bool CD3D12HeapArena::Create( const CD3D12HeapArena::InitializerType& desc )
{
    HRESULT hr = D3D12Adapter::GetDevice()->CreateHeap( &desc, IID_PPV_ARGS( m_Memory.GetAddressOf() ) );
    if ( SUCCEEDED( hr ) )
    {
        m_Allocator.SetTop( 0 );
        m_Allocator.SetSize( desc.SizeInBytes );
        return true;
    }
    return false;
}

bool CD3D12BufferArena::Create( const CD3D12BufferArena::InitializerType& initializer )
{
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width =  initializer.SizeInBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if ( FAILED( D3D12Adapter::GetDevice()->CreateCommittedResource( &initializer.m_HeapProperties, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, &bufferDesc,
        initializer.m_State, nullptr, IID_PPV_ARGS( m_Memory.GetAddressOf() ) ) ) )
    {
        return false;
    }

    m_Allocator.SetTop( 0 );
    m_Allocator.SetSize( initializer.SizeInBytes );
    return true;
}