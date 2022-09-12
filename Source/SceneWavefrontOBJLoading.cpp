#include "stdafx.h"
#include "Scene.h"
#include "Mesh.h"
#include "Logging.h"
#include "CommandLineArgs.h"
#include "MessageBox.h"
#include "MathHelper.h"

using namespace DirectX;

bool CScene::LoadFromWavefrontOBJFile( const char* filepath )
{
    const CommandLineArgs* commandLineArgs = CommandLineArgs::Singleton();

    char filePathNoExtension[ MAX_PATH ];
    char fileDir[ MAX_PATH ];
    strcpy( filePathNoExtension, filepath );
    PathRemoveExtensionA( filePathNoExtension );
    const char* fileName = PathFindFileNameA( filePathNoExtension );
    strcpy( fileDir, filepath );
    PathRemoveFileSpecA( fileDir );

    const char* MTLSearchPath = fileDir;
    LOG_STRING_FORMAT( "Loading mesh from: %s, MTL search path at: %s\n", filepath, MTLSearchPath );
    if ( !CreateMeshAndMaterialsFromWavefrontOBJFile( filepath, MTLSearchPath, false, MathHelper::s_IdentityMatrix4x4, false, INVALID_MATERIAL_ID ) )
    {
        CMessagebox::GetSingleton().AppendFormat( "Failed to load mesh from %s.\n", filepath );
        return false;
    }

    m_InstanceTransforms.push_back( MathHelper::s_IdentityMatrix4x3 );

    return true;
}