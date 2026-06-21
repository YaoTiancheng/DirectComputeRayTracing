#pragma once

enum EShaderCompileFlag
{
    EShaderCompileFlag_None = 0,
    EShaderCompileFlag_SkipOptimization = 0x1
};


class CShader
{
public:
    static CShader* CreateVertexFromFile( const wchar_t* filename, const std::vector<DxcDefine>& defines, uint32_t compileFlags = EShaderCompileFlag_None );

    static CShader* CreatePixelFromFile( const wchar_t* filename, const std::vector<DxcDefine>& defines, uint32_t compileFlags = EShaderCompileFlag_None );

    static CShader* CreateComputeFromFile( const wchar_t* filename, const std::vector<DxcDefine>& defines, uint32_t compileFlags = EShaderCompileFlag_None );

    CShader()
        : m_Bytecode( nullptr )
    {
    }

    ~CShader();

    D3D12_SHADER_BYTECODE GetBytecode() const { return { m_Bytecode->GetBufferPointer(), m_Bytecode->GetBufferSize() }; }

    IDxcBlob* m_Bytecode;
};