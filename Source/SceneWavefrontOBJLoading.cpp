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

    if ( !CreateMeshAndMaterialsFromWavefrontOBJFile( filepathString.c_str(), MTLSearchPathString.c_str(), false, MathHelper::s_IdentityMatrix4x4, false, INVALID_MATERIAL_ID ) )
    {
        CMessagebox::GetSingleton().AppendFormat( "Failed to load mesh from %s.\n", filepath );
        return false;
    }

    m_InstanceTransforms.push_back( MathHelper::s_IdentityMatrix4x3 );

    return true;
}