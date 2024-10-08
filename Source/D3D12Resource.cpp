#include "stdafx.h"
#include "D3D12Resource.h"
#include "D3D12Adapter.h"

struct SFrameResources
{
    void FlushDelete()
    {
        while ( !m_ComInterfaces.empty() )
        {
            IUnknown* Com = m_ComInterfaces.front();
            Com->Release();
            m_ComInterfaces.pop_front();
        }

        while ( !m_Resources.empty() )
        {
            CD3D12Resource* resource = m_Resources.front();
            delete resource;
            m_Resources.pop_front();
        }
    }

    std::list<CD3D12Resource*> m_Resources;
    std::list<IUnknown*> m_ComInterfaces;
};

static std::vector<SFrameResources> s_DeferredDeleteQueue;

void CD3D12Resource::CreateDeferredDeleteQueue()
{
    const uint32_t backbufferCount = D3D12Adapter::GetBackbufferCount();
    s_DeferredDeleteQueue.resize( backbufferCount );
}

void CD3D12Resource::FlushDelete()
{
    const uint32_t backbufferIndex = D3D12Adapter::GetBackbufferIndex();
    SFrameResources& frame = s_DeferredDeleteQueue[ backbufferIndex ];
    frame.FlushDelete();
}

void CD3D12Resource::FlushDeleteAll()
{
    for ( auto& frame : s_DeferredDeleteQueue )
    {
        frame.FlushDelete();
    }
}

void CD3D12Resource::AddToDeferredDeleteQueue( CD3D12Resource* resource )
{
    const uint32_t backbufferIndex = D3D12Adapter::GetBackbufferIndex();
    SFrameResources& frame = s_DeferredDeleteQueue[ backbufferIndex ];
    frame.m_Resources.push_back( resource );
}

void CD3D12Resource::AddToDeferredDeleteQueue( IUnknown* Com )
{
    const uint32_t backbufferIndex = D3D12Adapter::GetBackbufferIndex();
    SFrameResources& frame = s_DeferredDeleteQueue[ backbufferIndex ];
    frame.m_ComInterfaces.push_back( Com );
}

