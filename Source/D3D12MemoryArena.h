#pragma once

#include "MathHelper.h"

class CMemoryArenaAllocator
{
public:
    CMemoryArenaAllocator()
        : m_Top( 0 )
        , m_Size( 0 )
    {
    }

    uint64_t Allocate( uint64_t byteSize, uint64_t alignment )
    {
        uint64_t offset = MathHelper::DivideAndRoundUp( m_Top, alignment ) * alignment;
        uint64_t newTop = offset + byteSize;
        if ( newTop > m_Size )
        {
            return 0;
        }

        m_Top = newTop;
        return offset;
    }

    void SetTop( uint64_t top ) { m_Top = top; }
    void SetSize( uint64_t size ) { m_Size = size; }
    uint64_t GetTop() const { return m_Top; }
    uint64_t GetSize() const { return m_Size; }

private:
    uint64_t m_Top;
    uint64_t m_Size;
};


template <typename T>
class TD3D12MemoryArena
{
public:
    using MemoryType = typename T;
    TD3D12MemoryArena() = default;
    TD3D12MemoryArena( const TD3D12MemoryArena& ) = delete;
    TD3D12MemoryArena& operator=( const TD3D12MemoryArena& ) = delete;

    ~TD3D12MemoryArena()
    {
        Destroy();
    }

    void Destroy()
    {
        m_Memory.Reset();
        m_Allocator.SetSize( 0 );
        m_Allocator.SetTop( 0 );
    }

    uint64_t Allocate( uint64_t byteSize, uint64_t alignment ) { return m_Allocator.Allocate( byteSize, alignment ); }

    void Reset() { m_Allocator.SetTop( 0 ); }

    T* GetMemory() const { return m_Memory.Get(); }

protected:
    ComPtr<T> m_Memory;
    CMemoryArenaAllocator m_Allocator;
};


template <typename TArena>
struct TD3D12ArenaMemoryLocation
{
    TD3D12ArenaMemoryLocation()
        : m_Memory( nullptr ), m_Offset( 0 )
    {
    }

    TD3D12ArenaMemoryLocation( typename TArena::MemoryType* memory, uint64_t offset )
        : m_Memory( memory ), m_Offset( offset )
    {
    }

    bool IsValid() const { return m_Memory != nullptr; }

    typename TArena::MemoryType* m_Memory;
    uint64_t m_Offset;
};


template <typename TArena>
class TD3D12GrowingMemoryArena
{
public:
    TD3D12GrowingMemoryArena() = default;
    ~TD3D12GrowingMemoryArena()
    {
        Destroy();
    }

    TD3D12GrowingMemoryArena( const TD3D12GrowingMemoryArena& ) = delete;
    TD3D12GrowingMemoryArena& operator=( const TD3D12GrowingMemoryArena& ) = delete;

    bool Create( const typename TArena::InitializerType& initializer, uint32_t capacity = 0 ) 
    {
        assert( m_Arenas.empty() ); 
        for ( uint32_t i = 0; i < capacity; ++i )
        { 
            m_Arenas.emplace_back();
            TArena& newArena = m_Arenas.back();
            if ( !newArena.Create( initializer ) )
            {
                m_Arenas.clear();
                return false;
            }
        }
        return true;
    }

    void Destroy()
    {
        m_Arenas.clear();
    }

    void Reset( uint32_t capacity = 0 )
    {
        auto it = m_Arenas.begin();
        for ( uint32_t i = 0; i < capacity && it != m_Arenas.end(); ++i )
        {
            it->Reset();
            ++it;
        }

        while ( it != m_Arenas.end() )
        {
            auto itToErase = it;
            ++it;
            m_Arenas.erase( itToErase );
        }
    }

    TD3D12ArenaMemoryLocation<TArena> Allocate( const typename TArena::InitializerType& initializer, uint64_t byteSize, uint64_t alignment );

private:
    std::list<TArena> m_Arenas;
};


template <typename TArena>
class TD3D12BufferedGrowingMemoryArena
{
public:
    TD3D12BufferedGrowingMemoryArena()
        : m_Initializer{}
    {
    }

    ~TD3D12BufferedGrowingMemoryArena()
    {
        Destroy();
    }

    TD3D12BufferedGrowingMemoryArena( const TD3D12BufferedGrowingMemoryArena& ) = delete;
    TD3D12BufferedGrowingMemoryArena& operator=( const TD3D12BufferedGrowingMemoryArena& ) = delete;

    bool Create( const typename TArena::InitializerType& initializer, uint32_t capacity = 0 );

    void Destroy()
    {
        m_Arenas.clear();
        m_Initializer = {};
    }

    void Reset( uint32_t capacity = 0 );

    TD3D12ArenaMemoryLocation<TArena> Allocate( uint64_t byteSize, uint64_t alignment );

private:
    typename TArena::InitializerType m_Initializer;
    std::vector<std::shared_ptr<TD3D12GrowingMemoryArena<TArena>>> m_Arenas;
};


class CD3D12HeapArena : public TD3D12MemoryArena<ID3D12Heap>
{
public:
    using InitializerType = typename D3D12_HEAP_DESC;

    bool Create( const InitializerType& initializer );
};


class CD3D12BufferArena : public TD3D12MemoryArena<ID3D12Resource>
{
public:
    struct SInitializer
    {
        D3D12_HEAP_PROPERTIES m_HeapProperties;
        D3D12_RESOURCE_STATES m_State;
        uint64_t SizeInBytes;
    };
    using InitializerType = typename SInitializer;

    bool Create( const InitializerType& initializer );
};


using SD3D12ArenaHeapLocation = TD3D12ArenaMemoryLocation<CD3D12HeapArena>;
using SD3D12ArenaBufferLocation = TD3D12ArenaMemoryLocation<CD3D12BufferArena>;

class CD3D12BufferedGrowingHeapArena : public TD3D12BufferedGrowingMemoryArena<CD3D12HeapArena> {};
class CD3D12BufferedGrowingBufferArena : public TD3D12BufferedGrowingMemoryArena<CD3D12BufferArena> {};