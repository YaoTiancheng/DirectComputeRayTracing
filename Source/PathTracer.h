#pragma once

struct SRenderContext;
struct SRenderer;

class CPathTracer
{
public:
    virtual ~CPathTracer() = 0 {};

    virtual bool Create() = 0;

    virtual void Destroy() {};

    virtual void OnSceneLoaded( SRenderer* renderer ) {}

    virtual void Render( SRenderer* renderer, const SRenderContext& renderContext ) {}

    virtual void ResetImage() {}

    virtual bool IsImageComplete() = 0;

    virtual void OnImGUI( SRenderer* renderer ) {}

    virtual bool AcquireFilmClearTrigger() = 0;
};