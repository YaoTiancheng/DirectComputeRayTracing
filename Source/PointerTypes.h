#pragma once

#include <memory>

class GPUTexture;
using GPUTexturePtr = std::shared_ptr<GPUTexture>;

class GPUBuffer;
using GPUBufferPtr = std::shared_ptr<GPUBuffer>;

class GfxShader;
using GfxShaderPtr = std::shared_ptr<GfxShader>;

class ComputeShader;
using ComputeShaderPtr = std::shared_ptr<ComputeShader>;

