#pragma once

#include <memory>

class GPUTexture;
using GPUTexturePtr = std::unique_ptr<GPUTexture>;

class GPUBuffer;
using GPUBufferPtr = std::unique_ptr<GPUBuffer>;

class GfxShader;
using GfxShaderPtr = std::unique_ptr<GfxShader>;

class ComputeShader;
using ComputeShaderPtr = std::unique_ptr<ComputeShader>;

