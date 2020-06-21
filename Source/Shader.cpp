#include "stdafx.h"
#include "Shader.h"
#include "D3D11RenderSystem.h"
#include "CommandLineArgs.h"

static ID3DBlob* CompileFromFile( LPCWSTR filename, LPCSTR entryPoint, LPCSTR target )
{
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    UINT flags1 = CommandLineArgs::Singleton()->ShaderDebugEnabled() ? D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_PREFER_FLOW_CONTROL : D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr  = D3DCompileFromFile( filename, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, flags1, 0, &shaderBlob, &errorBlob );
    if ( FAILED( hr ) )
    {
        if ( errorBlob )
        {
            OutputDebugStringA( ( char* ) errorBlob->GetBufferPointer() );
            errorBlob->Release();
        }
        if ( shaderBlob )
            shaderBlob->Release();
    }
    return shaderBlob;
}

GfxShader* GfxShader::CreateFromFile( const wchar_t* filename )
{
    ID3DBlob* vertexShaderBlob = CompileFromFile( filename, "MainVS", "vs_5_0" );
    if ( !vertexShaderBlob )
        return nullptr;

    ID3D11VertexShader* vertexShader = nullptr;
    HRESULT hr = GetDevice()->CreateVertexShader( vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &vertexShader );
    if ( FAILED( hr ) )
    {
        vertexShaderBlob->Release();
        return nullptr;
    }

    ID3DBlob* pixelShaderBlob = CompileFromFile( filename, "MainPS", "ps_5_0" );
    if ( !pixelShaderBlob )
    {
        vertexShaderBlob->Release();
        vertexShader->Release();
        return nullptr;
    }

    ID3D11PixelShader* pixelShader = nullptr;
    hr = GetDevice()->CreatePixelShader( pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &pixelShader );
    if ( FAILED( hr ) )
    {
        vertexShaderBlob->Release();
        vertexShader->Release();
        pixelShaderBlob->Release();
        return nullptr;
    }

    GfxShader* gfxShader = new GfxShader();
    gfxShader->m_VertexShader       = vertexShader;
    gfxShader->m_PixelShader        = pixelShader;
    gfxShader->m_VertexShaderBlob   = vertexShaderBlob;

    return gfxShader;
}

GfxShader::~GfxShader()
{
    if ( m_VertexShader )
        m_VertexShader->Release();
    if ( m_PixelShader )
        m_PixelShader->Release();
    if ( m_VertexShaderBlob )
        m_VertexShaderBlob->Release();
}

ID3D11InputLayout* GfxShader::CreateInputLayout( const D3D11_INPUT_ELEMENT_DESC* elements, uint32_t count )
{
    ID3D11InputLayout* inputLayout = nullptr;
    HRESULT hr = GetDevice()->CreateInputLayout( elements, count, m_VertexShaderBlob->GetBufferPointer(), m_VertexShaderBlob->GetBufferSize(), &inputLayout );
    return inputLayout;
}

GfxShader::GfxShader()
    : m_VertexShader( nullptr )
    , m_PixelShader( nullptr )
    , m_VertexShaderBlob( nullptr )
{
}

ComputeShader* ComputeShader::CreateFromFile( const wchar_t* filename )
{
    ID3DBlob* shaderBlob = CompileFromFile( filename, "main", "cs_5_0" );
    if ( !shaderBlob )
        return nullptr;

    ID3D11ComputeShader* shader = nullptr;
    HRESULT hr = GetDevice()->CreateComputeShader( shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &shader );
    if ( FAILED( hr ) )
    {
        shaderBlob->Release();
        return nullptr;
    }

    shaderBlob->Release();

    ComputeShader* computeShader = new ComputeShader();
    computeShader->m_ComputeShader = shader;

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

