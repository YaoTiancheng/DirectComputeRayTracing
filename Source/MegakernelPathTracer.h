#pragma once

#include "PathTracer.h"
#include "D3D12Resource.h"

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

    CD3D12ComPtr<ID3D12RootSignature> m_RootSignature;
    CD3D12ComPtr<ID3D12PipelineState> m_PSO;

    CD3D12ResourcePtr<GPUBuffer> m_RayTracingConstantsBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_DebugConstantsBuffer;

    uint32_t m_TileSize;
    uint32_t m_CurrentTileIndex;

    uint32_t m_OutputType;
    uint32_t m_IterationThreshold;

    bool m_FilmClearTrigger;
};