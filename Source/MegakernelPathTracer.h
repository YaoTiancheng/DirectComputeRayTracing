#pragma once

#include "PathTracer.h"

class CScene;

class CMegakernelPathTracer : public CPathTracer
{
public:
    explicit CMegakernelPathTracer( CScene* scene )
        : m_TileSize( 512 )
        , m_CurrentTileIndex( 0 )
        , m_Scene( scene )
    {
    }

    virtual ~CMegakernelPathTracer()
    {
    }

    virtual bool Create() override;

    virtual void Destroy() override;

    virtual void OnSceneLoaded() override;

    virtual void Render( const SRenderContext& renderContext, const SRenderData& renderData ) override;

    virtual void ResetImage() override;

    virtual bool IsImageComplete() override;

    virtual void OnImGUI();

    virtual bool AcquireFilmClearTrigger();

private:
    bool CompileAndCreateRayTracingKernel();

    void ResetTileIndex();

    bool AreAllTilesRendered() const;

private:
    CScene* m_Scene;

    ComputeShaderPtr m_RayTracingShader;
    GPUBufferPtr m_RayTracingConstantsBuffer;

    uint32_t m_TileSize;
    uint32_t m_CurrentTileIndex;

    uint32_t m_OutputType;

    bool m_FilmClearTrigger;
};