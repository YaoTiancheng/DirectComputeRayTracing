#pragma once

struct SRenderContext;
class CScene;

class CPathTracer
{
public:
    virtual ~CPathTracer() = 0 {};

    virtual bool Create() = 0;

    virtual void Destroy() {};

    virtual void OnSceneLoaded( CScene* scene ) {}

    virtual void Render( CScene* scene, const SRenderContext& renderContext ) {}

    virtual void ResetImage() {}

    virtual bool IsImageComplete() = 0;

    virtual void OnImGUI( CScene* scene ) {}

    virtual bool AcquireFilmClearTrigger() = 0;
};