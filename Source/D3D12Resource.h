#pragma once

class CD3D12Resource
{
public:
	virtual ~CD3D12Resource()
	{
	}

	static void CreateDeferredDeleteQueue();

	static void FlushDelete();

    static void FlushDeleteAll();

private:
	friend struct SD3D12ResourceDeferredDeleter;
	friend struct SD3D12ComDeferredDeleter;
	static void AddToDeferredDeleteQueue( CD3D12Resource* resource );
    static void AddToDeferredDeleteQueue( IUnknown* Com );
};

struct SD3D12ResourceDeferredDeleter
{
	void operator()( CD3D12Resource* resource ) const
	{
		CD3D12Resource::AddToDeferredDeleteQueue( resource );
	}
};

struct SD3D12ComDeferredDeleter
{
    void operator()( IUnknown* Com ) const
    {
        CD3D12Resource::AddToDeferredDeleteQueue( Com );
    }
};