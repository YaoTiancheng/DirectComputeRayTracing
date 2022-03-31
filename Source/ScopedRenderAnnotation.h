#pragma once

#include "D3D11RenderSystem.h"

class CScopedRenderAnnotation
{
public:
	explicit CScopedRenderAnnotation( const wchar_t* name )
	{
		GetAnnotation()->BeginEvent( name );
	}

	~CScopedRenderAnnotation()
	{
		GetAnnotation()->EndEvent();
	}
};

#define SCOPED_RENDER_ANNOTATION( name ) CScopedRenderAnnotation __RenderAnnotation( name );