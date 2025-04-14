#include "stdafx.h"
#include "Scene.h"

using namespace DirectX;

static bool XM_CALLCONV RayTriangleIntersect( FXMVECTOR origin, FXMVECTOR direction, float tMin, float tMax, FXMVECTOR v0, GXMVECTOR v1, HXMVECTOR v2, float* t, float* u, float* v, bool* backface )
{
    XMVECTOR v0v1 = v1 - v0;
    XMVECTOR v0v2 = v2 - v0;

    XMVECTOR pvec = XMVector3Cross( direction, v0v2 );
    XMVECTOR det = XMVector3Dot( v0v1, pvec );

    XMVECTOR invDet = XMVectorReciprocal( det );

    XMVECTOR tvec = XMVectorSubtract( origin, v0 );
    XMVECTOR vU = XMVectorMultiply( XMVector3Dot( tvec, pvec ), invDet );

    XMVECTOR qvec = XMVector3Cross( tvec, v0v1 );
    XMVECTOR vV = XMVectorMultiply( XMVector3Dot( direction, qvec ), invDet );

    XMVECTOR vT = XMVectorMultiply( XMVector3Dot( v0v2, qvec ), invDet );

    XMStoreFloat( t, vT );
    XMStoreFloat( u, vU );
    XMStoreFloat( v, vV );
    float scalarDet;
    XMStoreFloat( &scalarDet, det );
    *backface = scalarDet > -1e-10;

    return std::abs( scalarDet ) >= 1e-10 && *u >= 0 && *u <= 1 && *v >= 0 && *u + *v <= 1 && *t >= tMin && *t < tMax;
}

static bool XM_CALLCONV RayAABBIntersect( FXMVECTOR origin, FXMVECTOR invDirection, float tMin, float tMax, FXMVECTOR bboxMin, GXMVECTOR bboxMax )
{
    XMVECTOR ta = XMVectorMultiply( XMVectorSubtract( bboxMin, origin ), invDirection );
    XMVECTOR tb = XMVectorMultiply( XMVectorSubtract( bboxMax, origin ), invDirection );
    XMVECTOR min = XMVectorMin( ta, tb );
    XMVECTOR max = XMVectorMax( ta, tb );

    XMVECTOR x = XMVectorSplatX( min );
    XMVECTOR y = XMVectorSplatY( min );
    XMVECTOR z = XMVectorSplatZ( min );
    XMVECTOR t0 = XMVectorMax( XMVectorMax( x, y ), z );

    x = XMVectorSplatX( max );
    y = XMVectorSplatY( max );
    z = XMVectorSplatZ( max );
    XMVECTOR t1 = XMVectorMin( XMVectorMin( x, y ), z );

    float scalarT0, scalarT1;
    XMStoreFloat( &scalarT0, t0 );
    XMStoreFloat( &scalarT1, t1 );
    return scalarT1 >= scalarT0 && ( scalarT0 < tMax&& scalarT1 >= tMin );
}

bool CScene::TraceRay( FXMVECTOR origin, FXMVECTOR direction, float tMin, SRayHit* outRayHit ) const
{
    std::vector<XMFLOAT4X3> instanceInvTransforms;
    instanceInvTransforms.reserve( m_InstanceTransforms.size() );
    for ( auto& transform : m_InstanceTransforms )
    {
        XMMATRIX matrix = XMLoadFloat4x3( &transform );
        XMVECTOR det;
        matrix = XMMatrixInverse( &det, matrix );
        instanceInvTransforms.emplace_back();
        XMStoreFloat4x3( &instanceInvTransforms.back(), matrix );
    }

    struct SBVHTraversalNode
    {
        uint32_t m_NodeIndex;
        bool m_IsBLAS;
    };

    std::stack<SBVHTraversalNode> stack;

    float tMax = std::numeric_limits<float>::infinity();
    float t, u, v;
    bool backface;

    uint32_t nodeIndex = 0;
    uint32_t instanceIndex = 0;
    uint32_t meshIndex = 0;
    bool isBLAS = false;
    XMVECTOR localRayOrigin = origin;
    XMVECTOR localRayDirection = direction;
    while ( true )
    {
        bool popNode = false;

        XMVECTOR bboxMin, bboxMax;
        const BVHAccel::BVHNode* node;

        if ( isBLAS )
        {
            node = m_Meshes[ meshIndex ].GetBVHNodes() + nodeIndex;
        }
        else
        {
            node = m_TLAS.data() + nodeIndex;
        }

        {
            const BoundingBox& bbox = node->m_BoundingBox;
            XMVECTOR vCenter = DirectX::XMLoadFloat3( &bbox.Center );
            XMVECTOR vExtends = DirectX::XMLoadFloat3( &bbox.Extents );
            bboxMin = DirectX::XMVectorSubtract( vCenter, vExtends );
            bboxMax = DirectX::XMVectorAdd( vCenter, vExtends );
        }

        if ( RayAABBIntersect( localRayOrigin, XMVectorReciprocal( localRayDirection ), tMin, tMax, bboxMin, bboxMax ) )
        {
            bool hasBLAS = !isBLAS && node->m_IsLeaf;
            if ( hasBLAS )
            {
                instanceIndex = node->m_PrimIndex;
                meshIndex = m_OriginalInstanceIndices[ node->m_PrimIndex ];
                XMMATRIX instanceInvTransform = XMLoadFloat4x3( &instanceInvTransforms[ meshIndex ] );
                localRayOrigin = XMVector3Transform( origin, instanceInvTransform );
                localRayDirection = XMVector3TransformNormal( direction, instanceInvTransform );
                isBLAS = true;
                nodeIndex = 0;
            }
            else
            {
                if ( node->m_PrimCount == 0 )
                {
                    XMFLOAT3A scalarLocalRayDirection;
                    XMStoreFloat3A( &scalarLocalRayDirection, localRayDirection );
                    uint32_t splitAxis = node->m_SplitAxis;
                    bool isDirectionNegative = splitAxis == 0 ? scalarLocalRayDirection.x < 0.f : ( splitAxis == 1 ? scalarLocalRayDirection.y < 0.f : scalarLocalRayDirection.z < 0.f );
                    uint32_t pushNodeIndex = isDirectionNegative ? nodeIndex + 1 : node->m_ChildIndex;
                    nodeIndex = isDirectionNegative ? node->m_ChildIndex : nodeIndex + 1;
                    stack.push( { pushNodeIndex, isBLAS } );
                }
                else
                {
                    uint32_t primBegin = node->m_PrimIndex;
                    uint32_t primEnd = primBegin + node->m_PrimCount;
                    const GPU::Vertex* vertices = m_Meshes[ meshIndex ].GetVertices().data();
                    const uint32_t* indices = m_Meshes[ meshIndex ].GetIndices().data();
                    for ( uint32_t iPrim = primBegin; iPrim < primEnd; ++iPrim )
                    {
                        XMVECTOR v0 = XMLoadFloat3( &vertices[ indices[ iPrim * 3 ] ].position );
                        XMVECTOR v1 = XMLoadFloat3( &vertices[ indices[ iPrim * 3 + 1 ] ].position );
                        XMVECTOR v2 = XMLoadFloat3( &vertices[ indices[ iPrim * 3 + 2 ] ].position );
                        if ( RayTriangleIntersect( localRayOrigin, localRayDirection, tMin, tMax, v0, v1, v2, &t, &u, &v, &backface ) )
                        {
                            tMax = t;
                            outRayHit->m_T = t;
                            outRayHit->m_U = u;
                            outRayHit->m_V = v;
                            outRayHit->m_InstanceIndex = instanceIndex;
                            outRayHit->m_MeshIndex = meshIndex;
                            outRayHit->m_TriangleIndex = iPrim;
                        }
                    }
                    popNode = true;
                }
            }
        }
        else
        {
            popNode = true;
        }

        if ( popNode )
        {
            bool lastNodeIsBLAS = isBLAS;
            if ( !stack.empty() )
            {
                auto poppedNode = stack.top();
                stack.pop();

                nodeIndex = poppedNode.m_NodeIndex;
                isBLAS = poppedNode.m_IsBLAS;

                if ( lastNodeIsBLAS != isBLAS )
                {
                    localRayOrigin = origin;
                    localRayDirection = direction;
                }
            }
            else
            {
                break;
            }
        }
    }

    return !std::isinf( tMax );
}

void CScene::ScreenToCameraRay( const DirectX::XMFLOAT2& screenPos, DirectX::XMVECTOR* origin, DirectX::XMVECTOR* direction )
{
    XMFLOAT3A scalarFilmPos = XMFLOAT3A( screenPos.x - .5f, -screenPos.y + .5f, CalculateFilmDistance() );
    scalarFilmPos.x *= m_FilmSize.x;
    scalarFilmPos.y *= m_FilmSize.y;

    *direction = XMVector3Normalize( XMLoadFloat3A( &scalarFilmPos ) );

    XMFLOAT4X4 scalarMatrix;
    m_Camera.GetTransformMatrix( &scalarMatrix );
    XMMATRIX matrix = XMLoadFloat4x4( &scalarMatrix );

    *direction = XMVector3TransformNormal( *direction, matrix );
    *origin = matrix.r[ 3 ];
}
