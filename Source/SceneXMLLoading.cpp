#include "stdafx.h"
#include "MessageBox.h"
#include "Logging.h"
#include "Scene.h"
#include "MathHelper.h"
#include "CommandLineArgs.h"
#include "Constants.h"
#include "RapidXml/rapidxml.hpp"

using namespace rapidxml;
using namespace DirectX;

enum EValueType
{
    eFloat,
    eInteger,
    eBoolean,
    eString,
    eRGB,
    eMatrix,
    eObject
};

enum EShapeType 
{
    Unsupported = 0, eObj = 1, eRectangle = 2 
};

enum EMaterialType
{
    eUnsupported,
    eDiffuse,
    eRoughDiffuse,
    eDielectric,
    eRoughDielectric,
    eConductor,
    eRoughConductor,
    ePlastic,
    eRoughPlastic,
    eTwosided,
};

struct SValue
{
    EValueType m_Type;
    union
    {
        float m_Float;
        int32_t m_Integer;
        bool m_Boolean;
        std::string_view m_String;
        XMFLOAT3 m_RGB;
        XMFLOAT4X4 m_Matrix;
        std::unordered_map<std::string_view, SValue*>* m_Object;
    };

    SValue() : m_Float( 0.0f )
    {
    }

    void SetAsObject()
    {
        m_Object = new std::unordered_map<std::string_view, SValue*>();
        m_Type = EValueType::eObject;
    }

    void InsertObjectField( std::string_view stringView, SValue* value )
    {
        m_Object->insert( { stringView, value } );
    }

    SValue* FindValue( std::string_view stringView ) const
    {
        auto iter = m_Object->find( stringView );
        if ( iter != m_Object->end() )
        {
            return iter->second;
        }
        else
        {
            return nullptr;
        }
    }
};

class CValueList
{
public:
    SValue* AllocateValue()
    {
        m_Values.emplace_back();
        return &m_Values.back();
    }

    void Clear()
    {
        m_Values.clear();
    }

private:
    std::list<SValue> m_Values;
};

namespace
{
    void SplitByDelimeter( std::string_view string, char delimeter, std::vector<std::string_view>* subStrings )
    {
        const char* p = string.data();
        size_t i = 0;
        while ( i < string.size() )
        {
            const char* pNew = p;
            size_t length = 0;
            while ( pNew[ 0 ] != delimeter && ( i + length ) < string.size() )
            {
                ++pNew;
                ++length;
            }
            subStrings->emplace_back( p, length );
            p = pNew + 1;
            i += length + 1;
        }
    }

    void SplitByComma( std::string_view string, std::vector<std::string_view>* subStrings )
    {
        return SplitByDelimeter( string, ',', subStrings );
    }

    void SplitBySpace( std::string_view string, std::vector<std::string_view>* subStrings )
    {
        return SplitByDelimeter( string, ' ', subStrings );
    }

    float ClampValueToValidRange( const std::string_view valueName, float value, float min, float max )
    {
        float result = value;
        if ( value < min || value > max )
        {
            result = std::clamp( result, min, max );
            LOG_STRING_FORMAT( "%.*s %f is out of valid range. Clamped to [%f, %f].\n", valueName.length(), valueName.data(), value, min, max );
        }
        return result;
    }

    SValue* BuildValueGraph( CValueList* valueList, xml_document<>* doc, std::vector<std::pair<std::string_view, SValue*>>* rootObjectValues )
    {
        std::unordered_set<std::string_view> objectTagNames = { "scene", "integrator", "sensor", "sampler", "film", "bsdf", "sampler", "rfilter", "emitter", "shape" };
        std::unordered_set<std::string_view> valueTagNames = { "float", "integer", "boolean", "string", "rgb" };

        xml_node<>* sceneNode = doc->first_node( "scene" );
        if ( !sceneNode )
        {
            LOG_STRING( "Cannot find scene node at the root.\n" );
            return nullptr;
        }

        std::stack<xml_node<>*> nodesStack;
        std::stack<SValue*> valuesStack;
        xml_node<>* currentNode = sceneNode;
        SValue* parentValue = valueList->AllocateValue();
        SValue* sceneValue = nullptr;
        parentValue->SetAsObject();
        SValue* currentValue = nullptr;
        std::vector<std::string_view> parameterSplit;
        std::unordered_map<std::string_view, std::pair<std::string_view, SValue*>> objectMap;
        while ( currentNode )
        {
            if ( objectTagNames.find( { currentNode->name(), currentNode->name_size() } ) != objectTagNames.end() )
            {
                currentValue = valueList->AllocateValue();
                currentValue->SetAsObject();

                if ( parentValue == sceneValue )
                {
                    rootObjectValues->push_back( { { currentNode->name(), currentNode->name_size() }, currentValue } );
                }
                else
                {
                    if ( !sceneValue && strncmp( "scene", currentNode->name(), currentNode->name_size() ) == 0 )
                    {
                        sceneValue = currentValue;
                    }

                    parentValue->InsertObjectField( { currentNode->name(), currentNode->name_size() }, currentValue );
                }

                xml_attribute<>* attribute = currentNode->first_attribute( "type", 0 );
                if ( attribute != nullptr )
                {
                    SValue* typeValue = valueList->AllocateValue();
                    currentValue->InsertObjectField( "type", typeValue );
                    typeValue->m_Type = EValueType::eString;
                    typeValue->m_String = { attribute->value(), attribute->value_size() };

                    attribute = attribute->next_attribute( "id", 0 );
                    if ( attribute != nullptr )
                    {
                        if ( objectMap.find( { attribute->value(), attribute->value_size() } ) == objectMap.end() )
                        {
                            objectMap[ { attribute->value(), attribute->value_size() } ] = { { currentNode->name(), currentNode->name_size() }, currentValue };
                        }
                        else
                        {
                            LOG_STRING_FORMAT( "Duplicated id \'%.*s\' found.\n", attribute->value_size(), attribute->value() );
                        }

                        SValue* idValue = valueList->AllocateValue();
                        currentValue->InsertObjectField( "id", idValue );
                        idValue->m_Type = EValueType::eString;
                        idValue->m_String = { attribute->value(), attribute->value_size() };
                    }
                }

                nodesStack.push( currentNode );
                currentNode = currentNode->first_node();

                valuesStack.push( parentValue );
                parentValue = currentValue;
            }
            else if ( strncmp( "transform", currentNode->name(), currentNode->name_size() ) == 0 )
            {
                currentValue = valueList->AllocateValue();
                parentValue->InsertObjectField( { currentNode->name(), currentNode->name_size() }, currentValue );
                currentValue->m_Type = EValueType::eMatrix;

                currentValue->m_Matrix = XMFLOAT4X4( 1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f );

                xml_node<>* childNode = currentNode->first_node();
                while ( childNode )
                {
                    if ( strncmp( "matrix", childNode->name(), childNode->name_size() ) == 0 )
                    {
                        xml_attribute<>* valueAttribute = childNode->first_attribute( "value", 0 );
                        if ( !valueAttribute )
                        {
                            LOG_STRING( "Expect value attribute for matrix.\n" );
                        }
                        else
                        {
                            parameterSplit.clear();
                            SplitBySpace( { valueAttribute->value(), valueAttribute->value_size() }, &parameterSplit );
                            if ( parameterSplit.size() != 16 )
                            {
                                LOG_STRING_FORMAT( "Unrecognized matrix value \'%.*s\'.\n", valueAttribute->value_size(), valueAttribute->value() );
                                return nullptr;
                            }
                            for ( int32_t r = 0; r < 4; ++r )
                            {
                                for ( int32_t c = 0; c < 4; ++c )
                                {
                                    // Mitsuba uses row-major matrix with column vectors, we use row-major matrix with row vectors, so transpose the matrix
                                    currentValue->m_Matrix( c, r ) = (float)atof( parameterSplit[ r * 4 + c ].data() ); 
                                }
                            }
                            // Mitsuba scene is in right handed coordinate system, convert to left handed one.
                            currentValue->m_Matrix( 0, 0 ) = -currentValue->m_Matrix( 0, 0 );
                            currentValue->m_Matrix( 1, 0 ) = -currentValue->m_Matrix( 1, 0 );
                            currentValue->m_Matrix( 2, 0 ) = -currentValue->m_Matrix( 2, 0 );
                            currentValue->m_Matrix( 3, 0 ) = -currentValue->m_Matrix( 3, 0 );
                        }
                    }
                    else
                    {
                        LOG_STRING_FORMAT( "Unsupported child tag for transform \'%.*s\'.\n", childNode->name_size(), childNode->name() );
                    }

                    childNode = childNode->next_sibling();
                }

                currentNode = currentNode->next_sibling();
            }
            else if ( strncmp( "ref", currentNode->name(), currentNode->name_size() ) == 0 )
            {
                xml_attribute<>* attribute = currentNode->first_attribute( "id", 0 );
                if ( attribute != nullptr )
                {
                    auto iter = objectMap.find( { attribute->value(), attribute->value_size() } );
                    if ( iter != objectMap.end() )
                    {
                        const std::string_view& refName = iter->second.first;
                        SValue* refValue = iter->second.second;
                        parentValue->InsertObjectField( refName, refValue );
                    }
                    else
                    {
                        LOG_STRING_FORMAT( "id \'%.*s\' not found.", attribute->value_size(), attribute->value() );
                    }
                }
                else
                {
                    LOG_STRING_FORMAT( "Expect a value attribute in the ref tag.\n" );
                    return nullptr;
                }

                currentNode = currentNode->next_sibling();
            }
            else if ( valueTagNames.find( { currentNode->name(), currentNode->name_size() } ) != valueTagNames.end() )
            {
                xml_attribute<>* nameAttribute = currentNode->first_attribute( "name", 0 );
                if ( !nameAttribute )
                {
                    LOG_STRING_FORMAT( "Expect a name attribute from tag \'%.*s\'.\n", currentNode->name_size(), currentNode->name() );
                    return nullptr;
                }
                currentValue = valueList->AllocateValue();
                parentValue->InsertObjectField( { nameAttribute->value(), nameAttribute->value_size() }, currentValue );

                xml_attribute<>* valueAttribute = currentNode->first_attribute( "value", 0 );
                if ( !valueAttribute )
                {
                    LOG_STRING( "Expect a value attribute.\n" );
                    return nullptr;
                }

                if ( strncmp( currentNode->name(), "integer", currentNode->name_size() ) == 0 )
                {
                    currentValue->m_Type = EValueType::eInteger;
                    currentValue->m_Integer = (int32_t)atoi( valueAttribute->value() );
                }
                else if ( strncmp( currentNode->name(), "float", currentNode->name_size() ) == 0 )
                {
                    currentValue->m_Type = EValueType::eFloat;
                    currentValue->m_Float = (float)atof( valueAttribute->value() );
                }
                else if ( strncmp( currentNode->name(), "boolean", currentNode->name_size() ) == 0 )
                {
                    currentValue->m_Type = EValueType::eBoolean;
                    if ( strncmp( valueAttribute->value(), "false", valueAttribute->value_size() ) == 0 )
                    {
                        currentValue->m_Boolean = false;
                    }
                    else if ( strncmp( valueAttribute->value(), "true", valueAttribute->value_size() ) == 0 )
                    {
                        currentValue->m_Boolean = true;
                    }
                    else
                    {
                        LOG_STRING( "Unrecognized boolean value.\n " );
                        return nullptr;
                    }
                }
                else if ( strncmp( currentNode->name(), "string", currentNode->name_size() ) == 0 )
                {
                    currentValue->m_Type = EValueType::eString;
                    currentValue->m_String = { valueAttribute->value(), valueAttribute->value_size() };
                }
                else if ( strncmp( currentNode->name(), "rgb", currentNode->name_size() ) == 0 )
                {
                    currentValue->m_Type = EValueType::eRGB;
                    parameterSplit.clear();
                    SplitByComma( { valueAttribute->value(), valueAttribute->value_size() }, &parameterSplit );
                    if ( parameterSplit.size() != 3 )
                    {
                        LOG_STRING( "Unrecognized RGB value.\n" );
                        return nullptr;
                    }
                    currentValue->m_RGB.x = (float)atof( parameterSplit[ 0 ].data() );
                    currentValue->m_RGB.y = (float)atof( parameterSplit[ 1 ].data() );
                    currentValue->m_RGB.z = (float)atof( parameterSplit[ 2 ].data() );
                }

                currentNode = currentNode->next_sibling();
            }
            else
            {
                LOG_STRING_FORMAT( "Unsupported tag name \"%.*s\"\n", currentNode->name_size(), currentNode->name() );
                currentNode = currentNode->next_sibling();
            }

            while ( !currentNode && !nodesStack.empty() )
            {
                currentNode = nodesStack.top();
                nodesStack.pop();
                currentNode = currentNode->next_sibling();

                parentValue = valuesStack.top();
                valuesStack.pop();
            }
        }

        return parentValue;
    }

    bool TranslateMaterialFromBSDF( const SValue& BSDF, const std::unordered_map<std::string_view, EMaterialType>& materialNameToEnumMap, SMaterialSetting* material, std::string_view* name )
    {
        EMaterialType materialType = EMaterialType::eUnsupported;

        SValue* typeValue = BSDF.FindValue( "type" );
        if ( !typeValue )
        {
            LOG_STRING( "Cannot obtain bsdf type.\n" );
            return false;
        }

        {
            auto it = materialNameToEnumMap.find( typeValue->m_String );
            if ( it != materialNameToEnumMap.end() )
            {
                materialType = it->second;
            }
        }

        SValue* idValue = BSDF.FindValue( "id" );
        if ( idValue )
        {
            *name = idValue->m_String;
        }

        if ( materialType == EMaterialType::eTwosided )
        {
            SValue* childBSDF = BSDF.FindValue( "bsdf" );
            if ( !childBSDF )
            {
                LOG_STRING( "Cannot find child BSDF inside a twosided BSDF.\n" );
                return false;
            }
            return TranslateMaterialFromBSDF( *childBSDF, materialNameToEnumMap, material, name );
        }

        bool hasDielectricIOR = false;
        bool hasConductorIOR = false;
        bool isTransmissive = false;
        bool hasRoughness = false;
        bool hasDiffuseReflectance = false;
        switch ( materialType )
        {
        case EMaterialType::eDiffuse:
        {
            hasDiffuseReflectance = true;
            break;
        }
        case EMaterialType::eRoughDiffuse:
        {
            hasDiffuseReflectance = true;
            hasRoughness = true;
            break;
        }
        case EMaterialType::eDielectric:
        {
            hasDielectricIOR = true;
            isTransmissive = true;
            break;
        }
        case EMaterialType::eRoughDielectric:
        {
            hasDielectricIOR = true;
            isTransmissive = true;
            hasRoughness = true;
            break;
        }
        case EMaterialType::eConductor:
        {
            hasConductorIOR = true;
            break;
        }
        case EMaterialType::eRoughConductor:
        {
            hasConductorIOR = true;
            hasRoughness = true;
            break;
        }
        case EMaterialType::ePlastic:
        {
            hasDielectricIOR = true;
            hasDiffuseReflectance = true;
            break;
        }
        case EMaterialType::eRoughPlastic:
        {
            hasDielectricIOR = true;
            hasDiffuseReflectance = true;
            hasRoughness = true;
            break;
        }
        case EMaterialType::eTwosided:
        {
            assert(false && "Should recursively translate child BSDF and never gets here.");
            return false;
        }
        default:
        {
            LOG_STRING_FORMAT( "Unsupported material type \'%.*s\', assigning default values.\n", typeValue->m_String.length(), typeValue->m_String.data() );
            break;
        }
        }

        assert( ( !hasDielectricIOR && !hasConductorIOR ) || ( hasDielectricIOR != hasConductorIOR ) );
        assert( ( !hasConductorIOR && !isTransmissive ) || ( hasConductorIOR != isTransmissive ) );
        assert( !isTransmissive || hasDielectricIOR );

        material->m_Albedo = { 0.0f, 0.0f, 0.0f };
        material->m_Emission = { 0.0f, 0.0f, 0.0f };
        material->m_Roughness = 0.0f;
        material->m_IOR = { 1.0f, 1.0f, 1.0f };
        material->m_K = { 1.0f, 1.0f, 1.0f };
        material->m_Transmission = 0.0f;
        material->m_Tiling = { 1.0f, 1.0f };
        material->m_IsMetal = hasConductorIOR;
        material->m_HasAlbedoTexture = false;
        material->m_HasEmissionTexture = false;
        material->m_HasRoughnessTexture = false;

        if ( hasRoughness )
        {
            SValue* alphaValue = BSDF.FindValue( "alpha" );
            float alpha = alphaValue ? alphaValue->m_Float : 0.0f;
            material->m_Roughness = sqrt( alpha );
        }

        if ( hasDielectricIOR )
        {
            SValue* intIORValue = BSDF.FindValue( "intIOR" );
            SValue* extIORValue = BSDF.FindValue( "extIOR" );
            float intIOR = 1.49f, extIOR = 1.000277f;
            if ( intIORValue )
            {
                if ( intIORValue->m_Type == EValueType::eFloat )
                {
                    intIOR = intIORValue->m_Float;
                }
                else
                {
                    LOG_STRING( "Non-float IOR value is not supported.\n" );
                }
            }
            if ( extIORValue )
            {
                if ( extIORValue->m_Type == EValueType::eFloat )
                {
                    extIOR = extIORValue->m_Float;
                }
                else
                {
                    LOG_STRING( "Non-float IOR value is not supported.\n" );
                }
            }
            material->m_IOR.x = intIOR / extIOR;
        }
        else if ( hasConductorIOR )
        {
            SValue* etaValue = BSDF.FindValue( "eta" );
            SValue* extEtaValue = BSDF.FindValue( "extEta" );
            XMFLOAT3 eta = { 0.0f, 0.0f, 0.0f };
            float extEta = 1.000277f;
            if ( etaValue )
            {
                if ( etaValue->m_Type == EValueType::eRGB )
                {
                    eta = etaValue->m_RGB;
                }
                else
                {
                    LOG_STRING( "Non-RGB eta value is not supported.\n" );
                }
            }
            if ( extEtaValue )
            {
                if ( extEtaValue->m_Type == EValueType::eFloat )
                {
                    extEta = extEtaValue->m_Float;
                }
                else
                {
                    LOG_STRING( "Non-float extEta value is not supported.\n" );
                }
            }
            material->m_IOR.x = eta.x / extEta;
            material->m_IOR.y = eta.y / extEta;
            material->m_IOR.z = eta.z / extEta;

            SValue* kValue = BSDF.FindValue( "k" );
            XMFLOAT3 k = { 1.0f, 1.0f, 1.0f };
            if ( kValue )
            {
                if ( kValue->m_Type == EValueType::eRGB )
                {
                    k = kValue->m_RGB;
                }
                else
                {
                    LOG_STRING( "Non-RGB k value is not supported.\n" );
                }
            }
            material->m_K = k;
        }

        if ( hasDiffuseReflectance )
        {
            XMFLOAT3 albedo = { 0.5f, 0.5f, 0.5f };
            SValue* diffuseReflectanceValue = BSDF.FindValue( !hasDielectricIOR ? "reflectance" : "diffuseReflectance" );
            if ( diffuseReflectanceValue )
            {
                if ( diffuseReflectanceValue->m_Type == EValueType::eRGB )
                {
                    albedo = diffuseReflectanceValue->m_RGB;
                }
                else
                {
                    LOG_STRING( "Non-RGB diffuse reflectance value is not supported.\n" );
                }
            }
            material->m_Albedo = albedo;
        }

        if ( isTransmissive )
        {
            float specularTransmittance = 1.0f;
            SValue* specularTransmittanceValue = BSDF.FindValue( "specularTransmittance" );
            if ( specularTransmittanceValue )
            {
                if ( specularTransmittanceValue->m_Type == EValueType::eFloat )
                {
                    specularTransmittance = specularTransmittanceValue->m_Float;
                }
                else
                {
                    LOG_STRING( "Non-float specular transmittance value is not supported.\n" );
                }
            }
            material->m_Transmission = specularTransmittance;
        }

        material->m_IOR.x = ClampValueToValidRange( "Material IOR.x", material->m_IOR.x, material->m_IsMetal ? 0.0f : 1.0f, MAX_MATERIAL_IOR );
        material->m_IOR.y = ClampValueToValidRange( "Material IOR.y", material->m_IOR.y, material->m_IsMetal ? 0.0f : 1.0f, MAX_MATERIAL_IOR );
        material->m_IOR.z = ClampValueToValidRange( "Material IOR.z", material->m_IOR.z, material->m_IsMetal ? 0.0f : 1.0f, MAX_MATERIAL_IOR );
        material->m_K.x = ClampValueToValidRange( "Material K.x", material->m_K.x, 0.0f, MAX_MATERIAL_K );
        material->m_K.y = ClampValueToValidRange( "Material K.y", material->m_K.y, 0.0f, MAX_MATERIAL_K );
        material->m_K.z = ClampValueToValidRange( "Material K.z", material->m_K.z, 0.0f, MAX_MATERIAL_K );

        return true;
    }

    bool CreateAndAddMaterial( const SValue& BSDF, const std::unordered_map<std::string_view, EMaterialType>& materialNameToEnumMap, std::vector<SMaterialSetting>* materials
        , std::vector<std::string>* names, std::unordered_map<const SValue*, uint32_t>* BSDFValuePointerToIdMap, uint32_t* materialId )
    {
        SMaterialSetting newMaterial;
        std::string_view newMaterialName;
        if ( TranslateMaterialFromBSDF( BSDF, materialNameToEnumMap, &newMaterial, &newMaterialName ) )
        {
            uint32_t newMaterialId = (uint32_t)materials->size();
            materials->emplace_back( newMaterial );
            names->emplace_back( newMaterialName.data(), newMaterialName.length() );
            BSDFValuePointerToIdMap->insert( std::make_pair( &BSDF, newMaterialId ) );
            *materialId = newMaterialId;
            return true;
        }
        return false;
    }
}

bool CScene::LoadFromXMLFile( const std::filesystem::path& filepath )
{
    std::ifstream ifstream( filepath );
    if ( !ifstream )
        return false;

    std::vector<char> xml( ( std::istreambuf_iterator<char>( ifstream ) ), std::istreambuf_iterator<char>() );
    ifstream.close();

    xml.emplace_back( '\0' );
    xml_document<> doc;
    doc.parse<parse_non_destructive>( xml.data() );

    CValueList valueList;
    std::vector<std::pair<std::string_view, SValue*>> rootObjectValues;
    SValue* rootValue = BuildValueGraph( &valueList, &doc, &rootObjectValues );
    if ( !rootValue )
    {
        LOG_STRING( "Failed to build value graph.\n" );
        return false;
    }

    std::unordered_map<std::string_view, EShapeType> shapeNameToEnumMap = { { "obj", EShapeType::eObj }, { "rectangle", EShapeType::eRectangle } };
    std::unordered_map<std::string_view, EMaterialType> materialNameToEnumMap =
    {
          { "diffuse", EMaterialType::eDiffuse }
        , { "roughdiffuse", EMaterialType::eRoughDiffuse }
        , { "dielectric", EMaterialType::eDielectric }
        , { "roughdielectric", EMaterialType::eRoughDielectric }
        , { "conductor", EMaterialType::eConductor }
        , { "roughconductor", EMaterialType::eRoughConductor }
        , { "plastic", EMaterialType::ePlastic }
        , { "roughplastic", EMaterialType::eRoughPlastic }
        , { "twosided", EMaterialType::eTwosided }
    };

    std::unordered_map<const SValue*, uint32_t> BSDFValuePointerToIdMap;
    for ( auto& rootObjectValue : rootObjectValues )
    {
        if ( strncmp( "integrator", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            SValue* typeValue = rootObjectValue.second->FindValue( "type" );
            if ( strncmp( "path", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
            {
                SValue* maxDepthValue = rootObjectValue.second->FindValue( "maxDepth" );
                if ( maxDepthValue )
                {
                    m_MaxBounceCount = maxDepthValue->m_Integer;
                }
            }
            else
            {
                LOG_STRING_FORMAT( "Unsupported integrator type \'%.*s\'.\n", typeValue->m_String.length(), typeValue->m_String.data() );
            }
        }
        else if ( strncmp( "sensor", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            {
                SValue* transformValue = rootObjectValue.second->FindValue( "transform" );
                XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
                XMFLOAT3 eulerAngles = { 0.0f, 0.0f, 0.0f };
                if ( transformValue )
                {
                    position = { transformValue->m_Matrix._41, transformValue->m_Matrix._42, transformValue->m_Matrix._43 };
                    eulerAngles = MathHelper::MatrixRotationToRollPitchYall( transformValue->m_Matrix );
                }
                m_Camera.SetPositionAndEulerAngles( position, eulerAngles );
            }

            {
                SValue* focalLengthValue = rootObjectValue.second->FindValue( "focalLength" );
                if ( focalLengthValue )
                {
                    float focalLengthMilliMeter = (float)atof( focalLengthValue->m_String.data() );
                    m_FocalLength = focalLengthMilliMeter * 0.001f;
                }
                else
                {
                    m_FocalLength = 0.05f;
                }
            }

            {
                SValue* apertureRadiusValue = rootObjectValue.second->FindValue( "apertureRadius" );
                m_RelativeAperture = apertureRadiusValue ? m_FocalLength / ( apertureRadiusValue->m_Float * 2 ) : 8.0f;
            }

            {
                SValue* focalDistanceValue = rootObjectValue.second->FindValue( "focusDistance" );
                m_FocalDistance = focalDistanceValue ? focalDistanceValue->m_Float : 2.0f;
            }

            {
                SValue* filmValue = rootObjectValue.second->FindValue( "film" );
                if ( filmValue )
                {
                    SValue* filterValue = filmValue->FindValue( "rfilter" );
                    if ( filterValue )
                    {
                        SValue* typeValue = filterValue->FindValue( "type" );
                        if ( typeValue )
                        {
                            if ( strncmp( "box", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                SValue* radiusValue = filterValue->FindValue( "radius" );
                                m_Filter = EFilter::Box;
                                m_FilterRadius = radiusValue ? radiusValue->m_Float : 0.5f;
                            }
                            else if ( strncmp( "gaussian", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                SValue* stddevValue = filterValue->FindValue( "stddev" );
                                m_Filter = EFilter::Gaussian;
                                m_GaussianFilterAlpha = stddevValue ? stddevValue->m_Float : 0.5f;
                                m_FilterRadius = m_GaussianFilterAlpha * 4;
                            }
                            else if ( strncmp( "mitchell", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                SValue* bValue = filterValue->FindValue( "B" );
                                SValue* cValue = filterValue->FindValue( "C" );
                                m_Filter = EFilter::Mitchell;
                                m_MitchellB = bValue ? bValue->m_Float : 1.0f / 3.0f;
                                m_MitchellB = cValue ? cValue->m_Float : 1.0f / 3.0f;
                                m_FilterRadius = 2.0f;
                            }
                            else if ( strncmp( "lanczos", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                SValue* tauValue = filterValue->FindValue( "lobes" );
                                m_Filter = EFilter::LanczosSinc;
                                m_LanczosSincTau = tauValue ? tauValue->m_Integer : 3;
                                m_FilterRadius = (float)m_LanczosSincTau;
                            }
                            else
                            {
                                LOG_STRING_FORMAT( "Unsupported reconstruction filter \'%.*s\'\n", typeValue->m_String.length(), typeValue->m_String.data() );
                            }
                        }
                    }
                }
            }
        }
        else if ( strncmp( "bsdf", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            uint32_t materialId = 0;
            CreateAndAddMaterial( *rootObjectValue.second, materialNameToEnumMap, &m_Mesh.GetMaterials(), &m_Mesh.GetMaterialNames(), &BSDFValuePointerToIdMap, &materialId );
        }
        else if ( strncmp( "shape", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            SValue* typeValue = rootObjectValue.second->FindValue( "type" );
            if ( !typeValue )
            {
                LOG_STRING( "Cannot determine the type of shape.\n" );
            }
            else
            {
                SValue* transformValue = rootObjectValue.second->FindValue( "transform" );
                XMFLOAT4X4 transform = transformValue ? transformValue->m_Matrix : MathHelper::s_IdentityMatrix;

                SValue* emitterValue = rootObjectValue.second->FindValue( "emitter" );
                bool isALight = emitterValue != nullptr;

                uint32_t materialId = -1;
                SValue* bsdfValue = rootObjectValue.second->FindValue( "bsdf" );
                if ( bsdfValue )
                {
                    auto iter = BSDFValuePointerToIdMap.find( bsdfValue );
                    if ( iter == BSDFValuePointerToIdMap.end() )
                    {
                        CreateAndAddMaterial( *bsdfValue, materialNameToEnumMap, &m_Mesh.GetMaterials(), &m_Mesh.GetMaterialNames(), &BSDFValuePointerToIdMap, &materialId );
                    }
                    else
                    {
                        materialId = iter->second;
                    }
                }
                else if ( isALight )
                {
                    // Assign a pitch black non-reflective material to light
                    materialId = (uint32_t)m_Mesh.GetMaterials().size();
                    SMaterialSetting material;
                    material.m_Albedo = XMFLOAT3( 0.f, 0.f, 0.f );
                    material.m_Roughness = 0.f;
                    material.m_IOR = XMFLOAT3( 1.f, 1.f, 1.f );
                    material.m_Transmission = 0.f;
                    material.m_IsMetal = false;
                    material.m_HasAlbedoTexture = false;
                    material.m_HasRoughnessTexture = false;
                    material.m_HasRoughnessTexture = false;
                    m_Mesh.GetMaterials().emplace_back( material );
                    m_Mesh.GetMaterialNames().emplace_back();
                }

                EShapeType shapeType = EShapeType::Unsupported;
                auto itShapeType = shapeNameToEnumMap.find( typeValue->m_String );
                if ( itShapeType != shapeNameToEnumMap.end() )
                {
                    shapeType = itShapeType->second;
                }

                uint32_t triangleCountBase = m_Mesh.GetTriangleCount();

                switch ( shapeType )
                {
                case EShapeType::eObj:
                {
                    SValue* filenameValue = rootObjectValue.second->FindValue( "filename" );
                    if ( !filenameValue )
                    {
                        LOG_STRING( "Cannot find filename of an obj shape.\n" );
                    }
                    else
                    {
                        char zeroTerminatedFilename[ MAX_PATH ];
                        sprintf_s( zeroTerminatedFilename, sizeof( zeroTerminatedFilename ), "%.*s\0", (int)filenameValue->m_String.length(), filenameValue->m_String.data() );
                        std::filesystem::path objFilepath = zeroTerminatedFilename;
                        if ( objFilepath.is_relative() )
                        {
                            objFilepath = filepath.parent_path() / objFilepath;
                        }
                        if ( !m_Mesh.LoadFromOBJFile( objFilepath.u8string().c_str(), "", true, transform, materialId ) )
                        {
                            LOG_STRING_FORMAT( "Failed to load wavefront obj file \'%s\'.\n", objFilepath.u8string().c_str() );
                        }
                    }
                    break;
                }
                case EShapeType::eRectangle:
                {
                    if ( !m_Mesh.GenerateRectangle( materialId, true, transform ) )
                    {
                        LOG_STRING( "Failed to generate rectangle shape.\n" );
                    }
                    break;
                }
                case EShapeType::Unsupported:
                default:
                {
                    LOG_STRING_FORMAT( "Unsupported shape type \'%.*s\'\n", typeValue->m_String.length(), typeValue->m_String.data() );
                    break;
                }
                }

                if ( emitterValue )
                {
                    uint32_t triangleCount = m_Mesh.GetTriangleCount();
                    uint32_t addedTriangleCount = triangleCount - triangleCountBase;
                    if ( GetLightCount() + addedTriangleCount <= s_MaxLightsCount )
                    {
                        SValue* emitterTypeValue = emitterValue->FindValue( "type" );
                        if ( emitterTypeValue && emitterTypeValue->m_Type == EValueType::eString )
                        {
                            if ( strncmp( "area", emitterTypeValue->m_String.data(), emitterTypeValue->m_String.length() ) == 0 )
                            {
                                XMFLOAT3 radiance( 1.0f, 1.0f, 1.0f );
                                SValue* radianceValue = emitterValue->FindValue( "radiance" );
                                if ( radianceValue )
                                {
                                    if ( radianceValue->m_Type == EValueType::eRGB )
                                    {
                                        radiance = radianceValue->m_RGB;
                                    }
                                    else
                                    {
                                        LOG_STRING( "Unsupported radiance type.\n" );
                                    }
                                }

                                // Create lights
                                const std::vector<uint32_t>& indices = m_Mesh.GetIndices();
                                const std::vector<GPU::Vertex>& vertices = m_Mesh.GetVertices();
                                for ( uint32_t iTriangle = triangleCountBase; iTriangle < triangleCount; ++iTriangle )
                                {
                                    XMVECTOR vP0 = XMLoadFloat3( &vertices[ indices[ iTriangle * 3 ] ].position );
                                    XMVECTOR vP1 = XMLoadFloat3( &vertices[ indices[ iTriangle * 3 + 1 ] ].position );
                                    XMVECTOR vP2 = XMLoadFloat3( &vertices[ indices[ iTriangle * 3 + 2 ] ].position );
                                    XMVECTOR vP0P1 = vP1 - vP0;
                                    XMVECTOR vP0P2 = vP2 - vP0;
                                    XMVECTOR vCross = XMVector3Cross( vP0P1, vP0P2 );
                                    XMVECTOR vArea = XMVector3Length( vCross );
                                    float area;
                                    XMStoreFloat( &area, vArea );
                                    float invSurfaceArea = 1.f / ( area * .5f );

                                    m_TriangleLights.emplace_back();
                                    STriangleLight& light = m_TriangleLights.back();
                                    light.m_Radiance = radiance;
                                    light.m_TriangleId = iTriangle;
                                    light.m_InvSurfaceArea = invSurfaceArea;
                                }
                            }
                            else
                            {
                                LOG_STRING_FORMAT( "Unsupported emitter type \'%.*s\' for shape.\n", emitterTypeValue->m_String.length(), emitterTypeValue->m_String.data() );
                            }
                        }
                        else
                        {
                            LOG_STRING( "Cannot determine type from emitter.\n" );
                        }
                    }
                    else
                    {
                        LOG_STRING( "Max light count exceeded.\n" );
                    }
                }
            }
        }
        else if ( strncmp( "emitter", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            SValue* emitterTypeValue = rootObjectValue.second->FindValue( "type" );
            if ( emitterTypeValue && emitterTypeValue->m_Type == EValueType::eString )
            {
                if ( strncmp( "constant", emitterTypeValue->m_String.data(), emitterTypeValue->m_String.length() ) == 0 )
                {
                    SValue* radianceValue = rootObjectValue.second->FindValue( "radiance" );
                    if ( radianceValue )
                    {
                        if ( radianceValue->m_Type == EValueType::eRGB )
                        {
                            m_BackgroundColor = XMFLOAT4( radianceValue->m_RGB.x, radianceValue->m_RGB.y, radianceValue->m_RGB.z, 1.0f );
                        }
                        else
                        {
                            LOG_STRING_FORMAT( "Non-RGB radiance type is not supported.\n" );
                        }
                    }
                }
                else
                {
                    LOG_STRING_FORMAT( "Unsupported emitter type \'%.*s\'.\n", emitterTypeValue->m_String.length(), emitterTypeValue->m_String.data() );
                }
            }
            else
            {
                LOG_STRING( "Cannot determine emitter type.\n" );
            }
        }
    }

    valueList.Clear();

    LOG_STRING_FORMAT( "Mesh loaded. Triangle count: %d, vertex count: %d, material count: %d\n", m_Mesh.GetTriangleCount(), m_Mesh.GetVertexCount(), m_Mesh.GetMaterials().size() );

    const CommandLineArgs* commandLineArgs = CommandLineArgs::Singleton();
    bool buildBVH = !commandLineArgs->GetNoBVHAccel();
    if ( buildBVH )
    {
        std::vector<uint32_t> reorderedTriangleId;
        m_Mesh.BuildBVH( nullptr, &reorderedTriangleId );

        // Remap the triangle lights to the reordered triangles
        for ( size_t i = 0; i < m_TriangleLights.size(); ++i )
        {
            auto it = std::find( reorderedTriangleId.begin(), reorderedTriangleId.end(), m_TriangleLights[ i ].m_TriangleId );
            assert( it != reorderedTriangleId.end() );
            m_TriangleLights[ i ].m_TriangleId = (uint32_t)std::distance( reorderedTriangleId.begin(), it );
        }

        uint32_t BVHMaxDepth = m_Mesh.GetBVHMaxDepth();
        uint32_t BVHMaxStackSize = m_Mesh.GetBVHMaxStackSize();
        LOG_STRING_FORMAT( "BVH created from mesh. Node count:%d, max depth:%d, max stack size:%d\n", m_Mesh.GetBVHNodeCount(), BVHMaxDepth, BVHMaxStackSize );
    }

    return true;
}
