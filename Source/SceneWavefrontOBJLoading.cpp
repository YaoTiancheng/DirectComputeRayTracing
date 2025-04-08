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

    SMeshProcessingParams processingParams;
    processingParams.m_ApplyTransform = true;
    XMFLOAT4X4 RHS2LHSMatrix = MathHelper::s_IdentityMatrix4x4;
    RHS2LHSMatrix._11 = -1.0f;
    processingParams.m_Transform = RHS2LHSMatrix;
    processingParams.m_ChangeWindingOrder = true;
    processingParams.m_FlipTexcoordV = false;
    processingParams.m_MaterialIdOverride = INVALID_MATERIAL_ID;
    if ( !CreateMeshAndMaterialsFromWavefrontOBJFile( filepathString.c_str(), MTLSearchPathString.c_str(), processingParams ) )
    {
        CMessagebox::GetSingleton().AppendFormat( "Failed to load mesh from %s.\n", filepath );
        return false;
    }

    m_InstanceTransforms.push_back( MathHelper::s_IdentityMatrix4x3 );
    // TODO: Assert only 1 mesh is created
    m_Meshes.back().SetName( filepath.stem().u8string() );

    return true;
}