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

template <typename T, typename TDeleter>
class CD3D12BasePtr
{
public:
	CD3D12BasePtr()
		: m_SharedPtr()
	{
	}

	CD3D12BasePtr( T* ptr )
		: m_SharedPtr( ptr, TDeleter() )
	{}

	CD3D12BasePtr( const CD3D12BasePtr& other )
		: m_SharedPtr( other.m_SharedPtr )
	{
	}

	CD3D12BasePtr(CD3D12BasePtr&& other )
		: m_SharedPtr( std::move( other.m_SharedPtr ) )
	{
	}

	CD3D12BasePtr& operator=( const CD3D12ComPtr& other )
	{
		m_SharedPtr = other.m_SharedPtr;
		return *this;
	}

	CD3D12BasePtr& operator=(CD3D12BasePtr&& other )
	{
		m_SharedPtr = std::move( other.m_SharedPtr );
		return *this;
	}

	T& operator*() const { return m_SharedPtr.operator*(); }

	T* operator->() const { return m_SharedPtr.operator->(); }

	T* Get() const { return m_SharedPtr.get(); }

	explicit operator bool() const { return m_SharedPtr.operator bool(); }

	void Reset() { m_SharedPtr.reset(); }

	template <typename Y>
	void Reset( Y* ptr )
	{
		m_SharedPtr.reset( ptr, TDeleter() );
	}

	void Swap( CD3D12ComPtr& other )
	{
		m_SharedPtr.swap( other.m_SharedPtr );
	}

	long UseCount() const { return m_SharedPtr.use_count(); }

private:
	std::shared_ptr<T> m_SharedPtr;
};

template <typename T>
using CD3D12ComPtr = CD3D12BasePtr<T, SD3D12ComDeferredDeleter>;

template <typename T>
using CD3D12ResourcePtr = CD3D12BasePtr<T, SD3D12ResourceDeferredDeleter>;