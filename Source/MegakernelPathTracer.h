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
        , m_IterationThreshold( 20 )
    {
    }

    virtual ~CMegakernelPathTracer()
    {
    }

    virtual bool Create() override;

    virtual void Destroy() override;

    virtual void OnSceneLoaded() override;

    virtual void Render( const SRenderContext& renderContext, const SBxDFTextures& BxDFTextures ) override;

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

    ComPtr<ID3D12RootSignature> m_RootSignature;
    std::shared_ptr<ID3D12PipelineState> m_PSO;

    GPUBufferPtr m_RayTracingConstantsBuffer;
    GPUBufferPtr m_DebugConstantsBuffer;

    uint32_t m_TileSize;
    uint32_t m_CurrentTileIndex;

    uint32_t m_OutputType;
    uint32_t m_IterationThreshold;

    bool m_FilmClearTrigger;
};