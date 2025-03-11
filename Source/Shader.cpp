#include "stdafx.h"
#include "Shader.h"
#include "D3D12Adapter.h"
#include "CommandLineArgs.h"

static ID3DBlob* CompileFromFile( LPCWSTR filename, LPCSTR entryPoint, LPCSTR target, const std::vector<D3D_SHADER_MACRO>& defines, uint32_t compileFlags )
{
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    UINT flags1 = D3DCOMPILE_IEEE_STRICTNESS;
    const bool shaderDebugEnabled = CommandLineArgs::Singleton()->ShaderDebugEnabled();
    if ( shaderDebugEnabled )
    {
        flags1 |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_PREFER_FLOW_CONTROL;
    }
    if ( ( compileFlags & EShaderCompileFlag_SkipOptimization ) != 0 )
    {
        flags1 |= D3DCOMPILE_SKIP_OPTIMIZATION;
    }
    else if ( !shaderDebugEnabled )
    {
        flags1 |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    }

    HRESULT hr  = D3DCompileFromFile( filename, defines.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, flags1, 0, &shaderBlob, &errorBlob );
    if ( errorBlob )
    {
        OutputDebugStringA( (char*)errorBlob->GetBufferPointer() );
        errorBlob->Release();
    }
    if ( FAILED( hr ) )
    {
        if ( shaderBlob )
            shaderBlob->Release();
    }
    return shaderBlob;
}

GfxShader* GfxShader::CreateFromFile( const wchar_t* filename, const std::vector<D3D_SHADER_MACRO>& defines, uint32_t compileFlags )
{
    ID3DBlob* vertexShaderBlob = CompileFromFile( filename, "MainVS", "vs_5_0", defines, compileFlags );
    if ( !vertexShaderBlob )
        return nullptr;

    ID3DBlob* pixelShaderBlob = CompileFromFile( filename, "MainPS", "ps_5_0", defines, compileFlags );
    if ( !pixelShaderBlob )
    {
        vertexShaderBlob->Release();
        return nullptr;
    }

    GfxShader* gfxShader = new GfxShader();
    gfxShader->m_VertexShader = vertexShaderBlob;
    gfxShader->m_PixelShader = pixelShaderBlob;
    return gfxShader;
}

GfxShader::GfxShader()
    : m_VertexShader( nullptr )
    , m_PixelShader( nullptr )
{
}

GfxShader::~GfxShader()
{
    if ( m_VertexShader )
        m_VertexShader->Release();
    if ( m_PixelShader )
        m_PixelShader->Release();
}

ComputeShader* ComputeShader::CreateFromFile( const wchar_t* filename, const std::vector<D3D_SHADER_MACRO>& defines, uint32_t compileFlags )
{
    ID3DBlob* shaderBlob = CompileFromFile( filename, "main", "cs_5_0", defines, compileFlags );
    if ( !shaderBlob )
        return nullptr;

    ComputeShader* computeShader = new ComputeShader();
    computeShader->m_ComputeShader = shaderBlob;
    return computeShader;
}

ComputeShader::ComputeShader()
    : m_ComputeShader( nullptr )
{
}

ComputeShader::~ComputeShader()
{
    if ( m_ComputeShader )
        m_ComputeShader->Release();
}

