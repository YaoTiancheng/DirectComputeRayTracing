#pragma once

#include "PathTracer.h"
#include "D3D12Resource.h"

class CMegakernelPathTracer : public CPathTracer
{
public:
    CMegakernelPathTracer()
        : m_TileSize( 512 )
        , m_CurrentTileIndex( 0 )
        , m_IterationThreshold( 20 )
    {
    }

    virtual ~CMegakernelPathTracer()
    {
    }

    virtual bool Create() override;

    virtual void Destroy() override;

    virtual void OnSceneLoaded( SRenderer* renderer ) override;

    virtual void Render( SRenderer* renderer, const SRenderContext& renderContext ) override;

    virtual void ResetImage() override;

    virtual bool IsImageComplete() override;

    virtual void OnImGUI( SRenderer* renderer );

    virtual bool AcquireFilmClearTrigger();

private:
    bool CompileAndCreateRayTracingKernel( SRenderer* renderer );

    void ResetTileIndex();

    bool AreAllTilesRendered() const;

private:
    CD3D12ComPtr<ID3D12RootSignature> m_RootSignature;
    CD3D12ComPtr<ID3D12PipelineState> m_PSO;

    CD3D12ResourcePtr<GPUBuffer> m_RayTracingConstantsBuffer;

    uint32_t m_TileSize;
    uint32_t m_CurrentTileIndex;

    uint32_t m_OutputType;
    uint32_t m_IterationThreshold;

    bool m_FilmClearTrigger;
};