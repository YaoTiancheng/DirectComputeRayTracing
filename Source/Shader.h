#pragma once

enum EShaderCompileFlag
{
    EShaderCompileFlag_None = 0,
    EShaderCompileFlag_SkipOptimization = 0x1
};


class GfxShader
{
public:
    static GfxShader* CreateFromFile( const wchar_t* filename, const std::vector<DxcDefine>& defines, uint32_t compileFlags = EShaderCompileFlag_None );

    ~GfxShader();

    IDxcBlob* GetVertexShader() const { return m_VertexShader; }

    IDxcBlob* GetPixelShader() const { return m_PixelShader; }

    D3D12_SHADER_BYTECODE GetVertexShaderBytecode() const { return { m_VertexShader->GetBufferPointer(), m_VertexShader->GetBufferSize() }; }

    D3D12_SHADER_BYTECODE GetPixelShaderBytecode() const { return { m_PixelShader->GetBufferPointer(), m_PixelShader->GetBufferSize() }; }

private:
    GfxShader();

    IDxcBlob* m_VertexShader;
    IDxcBlob* m_PixelShader;
};


class ComputeShader
{
public:
    static ComputeShader* CreateFromFile( const wchar_t* filename, const std::vector<DxcDefine>& defines, uint32_t compileFlags = EShaderCompileFlag_None );

    ~ComputeShader();

    IDxcBlob* GetNative() const { return m_ComputeShader; }

    D3D12_SHADER_BYTECODE GetShaderBytecode() const { return { m_ComputeShader->GetBufferPointer(), m_ComputeShader->GetBufferSize() }; }

private:
    ComputeShader();

    IDxcBlob* m_ComputeShader;
};