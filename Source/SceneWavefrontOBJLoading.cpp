#include "stdafx.h"
#include "Scene.h"
#include "Mesh.h"
#include "Logging.h"
#include "MessageBox.h"
#include "MathHelper.h"

using namespace DirectX;

bool CScene::LoadFromWavefrontOBJFile( const std::filesystem::path& filepath )
{
    const std::string filepathString = filepath.u8string();
    const std::string MTLSearchPathString = filepath.parent_path().u8string();
    LOG_STRING_FORMAT( "Loading mesh from: %s, MTL search path at: %s\n", filepathString.c_str(), MTLSearchPathString.c_str() );

    XMFLOAT4X4 RHS2LHSMatrix = MathHelper::s_IdentityMatrix4x4;
    RHS2LHSMatrix._11 = -1.0f;
    if ( !CreateMeshAndMaterialsFromWavefrontOBJFile( filepathString.c_str(), MTLSearchPathString.c_str(), true, RHS2LHSMatrix, true, INVALID_MATERIAL_ID ) )
    {
        CMessagebox::GetSingleton().AppendFormat( "Failed to load mesh from %s.\n", filepath );
        return false;
    }

    m_InstanceTransforms.push_back( MathHelper::s_IdentityMatrix4x3 );
    // TODO: Assert only 1 mesh is created
    m_Meshes.back().SetName( filepath.stem().u8string() );

    return true;
}