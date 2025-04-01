#pragma once

#include "pix3.h"

class CScopedRenderAnnotation
{
public:
	explicit CScopedRenderAnnotation( ID3D12GraphicsCommandList* commandList, const wchar_t* name )
		: m_CommandList( commandList )
	{
		PIXBeginEvent( commandList, 0, name );
	}

	~CScopedRenderAnnotation()
	{
		PIXEndEvent( m_CommandList );
	}

	ID3D12GraphicsCommandList* m_CommandList;
};

#define SCOPED_RENDER_ANNOTATION( commandList, name ) CScopedRenderAnnotation __RenderAnnotation( commandList, name );