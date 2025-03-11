#pragma once

enum EShaderCompileFlag
{
    EShaderCompileFlag_None = 0,
    EShaderCompileFlag_SkipOptimization = 0x1
};


class GfxShader
{
public:
    static GfxShader* CreateFromFile( const wchar_t* filename, const std::vector<D3D_SHADER_MACRO>& defines, uint32_t compileFlags = EShaderCompileFlag_None );

    ~GfxShader();

    ID3DBlob* GetVertexShader() const { return m_VertexShader; }

    ID3DBlob* GetPixelShader() const { return m_PixelShader; }

    D3D12_SHADER_BYTECODE GetVertexShaderBytecode() const { return { m_VertexShader->GetBufferPointer(), m_VertexShader->GetBufferSize() }; }

    D3D12_SHADER_BYTECODE GetPixelShaderBytecode() const { return { m_PixelShader->GetBufferPointer(), m_PixelShader->GetBufferSize() }; }

private:
    GfxShader();

    ID3DBlob* m_VertexShader;
    ID3DBlob* m_PixelShader;
};


class ComputeShader
{
public:
    static ComputeShader* CreateFromFile( const wchar_t* filename, const std::vector<D3D_SHADER_MACRO>& defines, uint32_t compileFlags = EShaderCompileFlag_None );

    ~ComputeShader();

    ID3DBlob* GetNative() const { return m_ComputeShader; }

    D3D12_SHADER_BYTECODE GetShaderBytecode() const { return { m_ComputeShader->GetBufferPointer(), m_ComputeShader->GetBufferSize() }; }

private:
    ComputeShader();

    ID3DBlob* m_ComputeShader;
};