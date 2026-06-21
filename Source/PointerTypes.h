#pragma once

#include <memory>

class GPUTexture;
using GPUTexturePtr = std::shared_ptr<GPUTexture>;

class GPUBuffer;
using GPUBufferPtr = std::shared_ptr<GPUBuffer>;

class CShader;
using ShaderPtr = std::shared_ptr<CShader>;

