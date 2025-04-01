#include "stdafx.h"
#include "Shader.h"
#include "D3D12Adapter.h"
#include "CommandLineArgs.h"
#include "Logging.h"

static IDxcBlob* CompileFromFile( LPCWSTR filename, LPCWSTR entryPoint, LPCWSTR target, const std::vector<DxcDefine>& defines, uint32_t compileFlags )
{
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    DxcCreateInstance( CLSID_DxcUtils, IID_PPV_ARGS( utils.GetAddressOf() ) );
    DxcCreateInstance( CLSID_DxcCompiler, IID_PPV_ARGS( compiler.GetAddressOf() ) );

    ComPtr<IDxcIncludeHandler> includeHandler;
    utils->CreateDefaultIncludeHandler( includeHandler.GetAddressOf() );

    std::vector<const wchar_t*> arguments;
    arguments.reserve( 4 );
    arguments.emplace_back( L"-Gis" ); // IEEE strictness
    const bool shaderDebugEnabled = CommandLineArgs::Singleton()->ShaderDebugEnabled();
    const bool disableOptimizations = shaderDebugEnabled || ( compileFlags & EShaderCompileFlag_SkipOptimization ) != 0;
    if ( shaderDebugEnabled | disableOptimizations )
    {
        if ( shaderDebugEnabled )
        { 
            arguments.emplace_back( L"-Zi" ); // Enable debug information
            arguments.emplace_back( L"-Qembed_debug" ); // Embed PDB in shader container
        }
        if ( disableOptimizations )
        {
            arguments.emplace_back( L"-Od" ); // Disable optimizations
        }
    }
    else 
    {
        arguments.emplace_back( L"-O3" );
    }

    ComPtr<IDxcCompilerArgs> compilerArgs;
    HRESULT hr = utils->BuildArguments( filename, entryPoint, target, arguments.data(), (uint32_t)arguments.size(), defines.data(), (uint32_t)defines.size(),
        compilerArgs.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return nullptr;
    }

    ComPtr<IDxcResult> result;
    bool needRecompile = false;
    do
    { 
        ComPtr<IDxcBlobEncoding> blobEncoding;
        hr = utils->LoadFile( filename, nullptr, blobEncoding.GetAddressOf() );
        if ( FAILED( hr ) )
        {
            return nullptr;
        }

        DxcBuffer source;
        source.Ptr = blobEncoding->GetBufferPointer();
        source.Size = blobEncoding->GetBufferSize();
        source.Encoding = DXC_CP_ACP;

        hr = compiler->Compile( &source, compilerArgs->GetArguments(), compilerArgs->GetCount(), includeHandler.Get(), IID_PPV_ARGS( result.GetAddressOf() ) );
        if ( FAILED( hr ) )
        {
            return nullptr;
        }

        ComPtr<IDxcBlobUtf8> errors;
        hr = result->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( errors.GetAddressOf() ), nullptr );
        if ( errors && errors->GetStringLength() > 0 )
        {
            LOG_STRING( errors->GetStringPointer() );
        }

        result->GetStatus( &hr );
        needRecompile = false;
        if ( FAILED( hr ) )
        {
            const char* errorText = errors ? errors->GetStringPointer() : nullptr;
            needRecompile = MessageBoxA( NULL, errorText, "Shader Compiler Failure", MB_RETRYCANCEL | MB_TASKMODAL ) == IDRETRY;
        }
    }
    while ( needRecompile );

    IDxcBlob* shaderObject = nullptr;
    ComPtr<IDxcBlobUtf16> shaderName;
    hr = result->GetOutput( DXC_OUT_OBJECT, IID_PPV_ARGS( &shaderObject ), shaderName.GetAddressOf() );
    return shaderObject;
}

GfxShader* GfxShader::CreateFromFile( const wchar_t* filename, const std::vector<DxcDefine>& defines, uint32_t compileFlags )
{
    IDxcBlob* vertexShaderBlob = CompileFromFile( filename, L"MainVS", L"vs_6_0", defines, compileFlags );
    if ( !vertexShaderBlob )
        return nullptr;

    IDxcBlob* pixelShaderBlob = CompileFromFile( filename, L"MainPS", L"ps_6_0", defines, compileFlags );
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

ComputeShader* ComputeShader::CreateFromFile( const wchar_t* filename, const std::vector<DxcDefine>& defines, uint32_t compileFlags )
{
    IDxcBlob* shaderBlob = CompileFromFile( filename, L"main", L"cs_6_6", defines, compileFlags );
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

