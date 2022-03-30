#pragma once

struct SRenderContext;
struct SRenderData;

class CPathTracer
{
public:
    virtual ~CPathTracer() = 0 {};

    virtual bool Create() = 0;

    virtual void Destroy() {};

    virtual void OnSceneLoaded() {}

    virtual void Render( const SRenderContext& renderContext, const SRenderData& renderData ) {}

    virtual void ResetImage() {}

    virtual bool IsImageComplete() = 0;

    virtual void OnImGUI() {}

    virtual bool AcquireFilmClearTrigger() = 0;
};