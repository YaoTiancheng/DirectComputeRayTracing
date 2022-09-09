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

    char BVHFilePath[ MAX_PATH ] = "\0";
    const char* MTLSearchPath = fileDir;
    if ( commandLineArgs->GetOutputBVHToFile() )
    {
        sprintf_s( BVHFilePath, MAX_PATH, "%s\\%s.xml", fileDir, fileName );
    }
    bool buildBVH = !commandLineArgs->GetNoBVHAccel();

    LOG_STRING_FORMAT( "Loading mesh from: %s, MTL search path at: %s, BVH file path at: %s\n", filepath, MTLSearchPath, BVHFilePath );

    size_t materialCount = m_Materials.size();
    if ( !CreateMeshAndMaterialsFromWavefrontOBJFile( filepath, MTLSearchPath, false, MathHelper::s_IdentityMatrix, INVALID_MATERIAL_ID ) )
    {
        CMessagebox::GetSingleton().AppendFormat( "Failed to load mesh from %s.\n", filepath );
        return false;
    }

    Mesh& mesh = m_Meshes.back();

    if ( buildBVH )
    {
        mesh.BuildBVH( BVHFilePath );
    }

    LOG_STRING_FORMAT( "Mesh loaded. Triangle count: %d, vertex count: %d, material count: %d\n", mesh.GetTriangleCount(), mesh.GetVertexCount(), m_Materials.size() - materialCount );

    if ( !commandLineArgs->GetNoBVHAccel() )
    {
        uint32_t BVHMaxDepth = mesh.GetBVHMaxDepth();
        uint32_t BVHMaxStackSize = mesh.GetBVHMaxStackSize();
        LOG_STRING_FORMAT( "BVH created from mesh. Node count:%d, max depth:%d, max stack size:%d\n", mesh.GetBVHNodeCount(), BVHMaxDepth, BVHMaxStackSize );
    }

    return true;
}