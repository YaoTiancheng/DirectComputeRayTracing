#pragma once

class GfxShader
{
public:
    static GfxShader* CreateFromFile( const wchar_t* filename, const std::vector<D3D_SHADER_MACRO>& defines );

    ~GfxShader();

    ID3D11InputLayout*  CreateInputLayout( const D3D11_INPUT_ELEMENT_DESC* elements, uint32_t count );

    ID3D11VertexShader* GetVertexShader() const { return m_VertexShader; }

    ID3D11PixelShader*  GetPixelShader() const { return m_PixelShader; }

private:
    GfxShader();

    ID3D11VertexShader* m_VertexShader;
    ID3D11PixelShader*  m_PixelShader;
    ID3DBlob*           m_VertexShaderBlob;
};


class ComputeShader
{
public:
    static ComputeShader* CreateFromFile( const wchar_t* filename, const std::vector<D3D_SHADER_MACRO>& defines );

    ~ComputeShader();

    ID3D11ComputeShader* GetNative() const { return m_ComputeShader; }

private:
    ComputeShader();

    ID3D11ComputeShader* m_ComputeShader;
};