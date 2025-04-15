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

enum class EValueType
{
    eFloat = 0,
    eInteger = 1,
    eBoolean = 2,
    eString = 3,
    eVector = 4,
    eRGB = 4,
    eMatrix = 5,
    eObject = 6,
};

enum class EShapeType 
{
    Unsupported = 0, eObj = 1, eRectangle = 2 
};

enum class EXMLMaterialType
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

template <typename T> struct SGetValueType {};
template <> struct SGetValueType<int32_t> { static const EValueType s_ValueType = EValueType::eInteger; };
template <> struct SGetValueType<float> { static const EValueType s_ValueType = EValueType::eFloat; };
template <> struct SGetValueType<bool> { static const EValueType s_ValueType = EValueType::eBoolean; };
template <> struct SGetValueType<std::string_view> { static const EValueType s_ValueType = EValueType::eString; };
template <> struct SGetValueType<XMFLOAT3> { static const EValueType s_ValueType = EValueType::eVector; };
template <> struct SGetValueType<XMFLOAT4X4> { static const EValueType s_ValueType = EValueType::eMatrix; };

struct SValue
{
    SValue() : m_Float( 0.0f )
    {
    }

    ~SValue()
    {
        if ( m_Type == EValueType::eObject )
        {
            delete m_ObjectFields;
            delete m_NestedObjects;
        }
    }

    template <typename T> T& Get() { return T(); }
    template <typename T> const T& Get() const { return T(); }

    template <> int32_t& Get<int32_t>() { return m_Integer; }
    template <> const int32_t& Get<int32_t>() const { return m_Integer; }

    template <> float& Get<float>() { return m_Float; }
    template <> const float& Get<float>() const { return m_Float; }

    template <> bool& Get<bool>() { return m_Boolean; }
    template <> const bool& Get<bool>() const { return m_Boolean; }

    template <> std::string_view& Get<std::string_view>() { return m_String; }
    template <> const std::string_view& Get<std::string_view>() const { return m_String; }

    template <> XMFLOAT3& Get<XMFLOAT3>() { return m_Vector; }
    template <> const XMFLOAT3& Get<XMFLOAT3>() const { return m_Vector; }

    template <> XMFLOAT4X4& Get<XMFLOAT4X4>() { return m_Matrix; }
    template <> const XMFLOAT4X4& Get<XMFLOAT4X4>() const { return m_Matrix; }

    void SetAsObject()
    {
        m_ObjectFields = new std::unordered_map<std::string_view, SValue*>();
        m_NestedObjects = new std::vector<std::pair<std::string_view, SValue*>>();
        m_Type = EValueType::eObject;
    }

    void InsertObjectField( std::string_view stringView, SValue* value )
    {
        m_ObjectFields->insert( { stringView, value } );
    }

    void InsertNestedObject( std::string_view stringView, SValue* value )
    {
        m_NestedObjects->emplace_back( std::make_pair( stringView, value ) );
    }

    SValue* FindObjectField( std::string_view stringView ) const
    {
        auto iter = m_ObjectFields->find( stringView );
        if ( iter != m_ObjectFields->end() )
        {
            return iter->second;
        }
        else
        {
            return nullptr;
        }
    }

    template <typename T>
    T GetObjectField( std::string_view stringView, T defaultValue ) const
    {
        SValue* value = FindObjectField( stringView );
        const bool fieldExist = value && value->m_Type == SGetValueType<T>::s_ValueType;
        return fieldExist ? value->Get<T>() : defaultValue;
    }

    SValue* FindFirstNestedObject( std::string_view stringView ) const
    {
        for ( auto& iter : *m_NestedObjects )
        {
            if ( iter.first == stringView ) 
            {
                return iter.second;
            }
        }
        return nullptr;
    }

    EValueType m_Type;
    union
    {
        float m_Float;
        int32_t m_Integer;
        bool m_Boolean;
        std::string_view m_String;
        union
        {
            XMFLOAT3 m_RGB;
            XMFLOAT3 m_Vector;
        };
        XMFLOAT4X4 m_Matrix;
        struct
        {
            std::unordered_map<std::string_view, SValue*>* m_ObjectFields;
            std::vector<std::pair<std::string_view, SValue*>>* m_NestedObjects;
        };
    };
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

static bool TryGetAttribute( xml_node<>* node, const char* attributeName, std::string_view* attributeValue )
{
    xml_attribute<>* attribute = node->first_attribute( attributeName );
    if ( !attribute )
    {
        return false;
    }

    *attributeValue = std::string_view( attribute->value(), attribute->value_size() );
    return true;
}

static bool TryEvaluateValueString( std::string_view original, const std::unordered_map<std::string_view, std::string_view>& defaultParameters, std::string_view* actual )
{
    if ( original.length() == 0 )
    {
        *actual = original;
        return true;
    }

    if ( original[ 0 ] == '$' )
    {
        std::string_view defaultParameterName( original.data() + 1, original.length() - 1 );
        auto iter = defaultParameters.find( defaultParameterName );
        if ( iter != defaultParameters.end() )
        {
            *actual = iter->second;
            return true;
        }
        else
        {
            LOG_STRING_FORMAT( "Failed to find default parameter with name %.*s\n", defaultParameterName.length(), defaultParameterName.data() );
            return false;
        }
    }

    *actual = original;
    return true;
}

static void SplitByDelimeter( std::string_view string, char delimeter, std::vector<std::string_view>* subStrings )
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

static void SplitByComma( std::string_view string, std::vector<std::string_view>* subStrings )
{
    return SplitByDelimeter( string, ',', subStrings );
}

static void SplitBySpace( std::string_view string, std::vector<std::string_view>* subStrings )
{
    return SplitByDelimeter( string, ' ', subStrings );
}

static bool BuildValueGraph( CValueList* valueList, xml_document<>* doc, std::vector<SValue*>* sceneValues )
{
    std::unordered_set<std::string_view> objectTagNames = { "scene", "integrator", "sensor", "sampler", "film", "bsdf", "sampler", "rfilter", "emitter", "shape", "texture" };
    std::unordered_set<std::string_view> valueTagNames = { "float", "integer", "boolean", "string", "point", "vector", "rgb" };

    std::stack<xml_node<>*> nodesStack;
    std::stack<SValue*> valuesStack;
    xml_node<>* currentNode = doc->first_node( "scene" );
    SValue* parentValue = nullptr;
    SValue* currentValue = nullptr;
    std::vector<std::string_view> parameterSplit;
    std::unordered_map<std::string_view, std::pair<std::string_view, SValue*>> objectMap;
    std::unordered_map<std::string_view, std::string_view> defaultParameterMap;
    while ( currentNode )
    {
        if ( objectTagNames.find( { currentNode->name(), currentNode->name_size() } ) != objectTagNames.end() )
        {
            currentValue = valueList->AllocateValue();
            currentValue->SetAsObject();

            if ( parentValue == nullptr )
            {
                // No parent, must be a scene node, verify scene version
                {
                    xml_attribute<>* versionAttribute = currentNode->first_attribute( "version" );
                    if ( !versionAttribute )
                    {
                        LOG_STRING( "Cannot find version attribute at the scene tag.\n" );
                        return false;
                    }

                    SplitByDelimeter( std::string_view( versionAttribute->value(), versionAttribute->value_size() ), '.', &parameterSplit );
                    if ( parameterSplit.size() == 3 )
                    {
                        int32_t majorVersion = atoi( parameterSplit[ 0 ].data() );
                        if ( majorVersion < 3 )
                        {
                            LOG_STRING_FORMAT( "Unsupported scene version %.*s.\n", versionAttribute->value_size(), versionAttribute->value() );
                            return false;
                        }
                    }
                    else
                    {
                        LOG_STRING_FORMAT( "Unsupported scene version format %.*s.\n", versionAttribute->value_size(), versionAttribute->value() );
                        return false;
                    }
                }
                sceneValues->emplace_back( currentValue );
            }
            else
            {
                // If the node has a name attribute, then it is an object field, otherwise it is a nested object.
                xml_attribute<>* attribute = currentNode->first_attribute( "name" );
                if ( attribute != nullptr )
                {
                    parentValue->InsertObjectField( std::string_view( attribute->value(), attribute->value_size() ), currentValue );
                }
                else
                {
                    // Add it as nested object
                    parentValue->InsertNestedObject( std::string_view( currentNode->name(), currentNode->name_size() ), currentValue );
                }

                attribute = currentNode->first_attribute( "id", 0 );
                if ( attribute != nullptr )
                {
                    const std::string_view id( attribute->value(), attribute->value_size() );
                    if ( objectMap.find( id ) == objectMap.end() )
                    {
                        objectMap[ id ] = { std::string_view( currentNode->name(), currentNode->name_size() ), currentValue };
                    }
                    else
                    {
                        LOG_STRING_FORMAT( "Duplicated id \'%.*s\' found.\n", id.length(), id.data() );
                    }

                    SValue* idValue = valueList->AllocateValue();
                    currentValue->InsertObjectField( "id", idValue );
                    idValue->m_Type = EValueType::eString;
                    idValue->m_String = id;
                }

                attribute = currentNode->first_attribute( "type" );
                if ( attribute != nullptr )
                {
                    SValue* typeValue = valueList->AllocateValue();
                    currentValue->InsertObjectField( "type", typeValue );
                    typeValue->m_Type = EValueType::eString;
                    if ( !TryEvaluateValueString( std::string_view( attribute->value(), attribute->value_size() ), defaultParameterMap, &typeValue->m_String ) )
                    {
                        LOG_STRING_FORMAT( "Evaluating type string %.*s failed.\n", attribute->value_size(), attribute->value() );
                        return false;
                    }
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
            currentValue->m_Type = EValueType::eMatrix;

            // If the node has a name attribute, then it is an object field, otherwise it is a nested object.
            xml_attribute<>* attribute = currentNode->first_attribute( "name" );
            if ( attribute != nullptr )
            {
                parentValue->InsertObjectField( std::string_view( attribute->value(), attribute->value_size() ), currentValue );
            }
            else
            {
                // Add it as nested object
                parentValue->InsertNestedObject( std::string_view( currentNode->name(), currentNode->name_size() ), currentValue );
            }

            attribute = currentNode->first_attribute( "id", 0 );
            if ( attribute != nullptr )
            {
                const std::string_view id( attribute->value(), attribute->value_size() );
                if ( objectMap.find( id ) == objectMap.end() )
                {
                    objectMap[ id ] = { std::string_view( currentNode->name(), currentNode->name_size() ), currentValue };
                }
                else
                {
                    LOG_STRING_FORMAT( "Duplicated id \'%.*s\' found.\n", id.length(), id.data() );
                }

                SValue* idValue = valueList->AllocateValue();
                currentValue->InsertObjectField( "id", idValue );
                idValue->m_Type = EValueType::eString;
                idValue->m_String = id;
            }

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
                        std::string_view value( valueAttribute->value(), valueAttribute->value_size() );
                        if ( !TryEvaluateValueString( value, defaultParameterMap, &value ) )
                        {
                            LOG_STRING_FORMAT( "Evaluating value string %.*s failed.\n", valueAttribute->value_size(), valueAttribute->value() );
                            return false;
                        }

                        parameterSplit.clear();
                        SplitBySpace( value, &parameterSplit );
                        if ( parameterSplit.size() != 16 )
                        {
                            LOG_STRING_FORMAT( "Unrecognized matrix value \'%.*s\'.\n", valueAttribute->value_size(), valueAttribute->value() );
                            return false;
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
            xml_attribute<>* idAttribute = currentNode->first_attribute( "id", 0 );
            if ( idAttribute != nullptr )
            {
                auto iter = objectMap.find( { idAttribute->value(), idAttribute->value_size() } );
                if ( iter != objectMap.end() )
                {
                    SValue* refValue = iter->second.second;
                    xml_attribute<>* nameAttribute = currentNode->first_attribute( "name" );
                    // If the node has a name attribute, then it is an object field, otherwise it is a nested object.
                    if ( nameAttribute )
                    {
                        const std::string_view name( nameAttribute->value(), nameAttribute->value_size() );
                        parentValue->InsertObjectField( name, refValue );
                    }
                    else
                    {
                        const std::string_view& name = iter->second.first;
                        parentValue->InsertNestedObject( name, refValue );
                    }
                }
                else
                {
                    LOG_STRING_FORMAT( "id \'%.*s\' not found.", idAttribute->value_size(), idAttribute->value() );
                }
            }
            else
            {
                LOG_STRING_FORMAT( "Expect id attribute in the ref tag.\n" );
                return false;
            }

            currentNode = currentNode->next_sibling();
        }
        else if ( valueTagNames.find( { currentNode->name(), currentNode->name_size() } ) != valueTagNames.end() )
        {
            xml_attribute<>* nameAttribute = currentNode->first_attribute( "name", 0 );
            if ( !nameAttribute )
            {
                LOG_STRING_FORMAT( "Expect a name attribute from tag \'%.*s\'.\n", currentNode->name_size(), currentNode->name() );
                return false;
            }
            currentValue = valueList->AllocateValue();
            parentValue->InsertObjectField( { nameAttribute->value(), nameAttribute->value_size() }, currentValue );

            xml_attribute<>* valueAttribute = currentNode->first_attribute( "value", 0 );
            if ( !valueAttribute )
            {
                LOG_STRING( "Expect a value attribute.\n" );
                return false;
            }

            // Value string could references a default parameter, must evaluate the value string to get the actual value string before parsing.
            std::string_view evaluatedValueString( valueAttribute->value(), valueAttribute->value_size() );
            if ( !TryEvaluateValueString( evaluatedValueString, defaultParameterMap, &evaluatedValueString ) )
            {
                LOG_STRING_FORMAT( "Evaluating value string %.*s failed.\n", valueAttribute->value_size(), valueAttribute->value() );
                return false;
            }

            if ( strncmp( currentNode->name(), "integer", currentNode->name_size() ) == 0 )
            {
                currentValue->m_Type = EValueType::eInteger;
                currentValue->m_Integer = (int32_t)atoi( evaluatedValueString.data() );
            }
            else if ( strncmp( currentNode->name(), "float", currentNode->name_size() ) == 0 )
            {
                currentValue->m_Type = EValueType::eFloat;
                currentValue->m_Float = (float)atof( evaluatedValueString.data() );
            }
            else if ( strncmp( currentNode->name(), "boolean", currentNode->name_size() ) == 0 )
            {
                currentValue->m_Type = EValueType::eBoolean;
                if ( strncmp( evaluatedValueString.data(), "false", evaluatedValueString.length() ) == 0 )
                {
                    currentValue->m_Boolean = false;
                }
                else if ( strncmp( evaluatedValueString.data(), "true", evaluatedValueString.length() ) == 0 )
                {
                    currentValue->m_Boolean = true;
                }
                else
                {
                    LOG_STRING( "Unrecognized boolean value.\n " );
                    return false;
                }
            }
            else if ( strncmp( currentNode->name(), "string", currentNode->name_size() ) == 0 )
            {
                currentValue->m_Type = EValueType::eString;
                currentValue->m_String = evaluatedValueString;
            }
            else if ( strncmp( currentNode->name(), "point", currentNode->name_size() ) == 0 ||
                strncmp( currentNode->name(), "vector", currentNode->name_size() ) == 0 )
            {
                currentValue->m_Type = EValueType::eVector;
                parameterSplit.clear();
                SplitByComma( evaluatedValueString, &parameterSplit );
                if ( parameterSplit.size() != 3 )
                {
                    LOG_STRING( "Unrecognized point/vector value.\n" );
                    return false;
                }
                currentValue->m_Vector.x = (float)atof( parameterSplit[ 0 ].data() );
                currentValue->m_Vector.y = (float)atof( parameterSplit[ 1 ].data() );
                currentValue->m_Vector.z = (float)atof( parameterSplit[ 2 ].data() );
            }
            else if ( strncmp( currentNode->name(), "rgb", currentNode->name_size() ) == 0 )
            {
                currentValue->m_Type = EValueType::eRGB;
                parameterSplit.clear();
                SplitByComma( evaluatedValueString, &parameterSplit );
                if ( parameterSplit.size() != 3 )
                {
                    LOG_STRING( "Unrecognized RGB value.\n" );
                    return false;
                }
                currentValue->m_RGB.x = (float)atof( parameterSplit[ 0 ].data() );
                currentValue->m_RGB.y = (float)atof( parameterSplit[ 1 ].data() );
                currentValue->m_RGB.z = (float)atof( parameterSplit[ 2 ].data() );
            }

            currentNode = currentNode->next_sibling();
        }
        else if ( strncmp( "default", currentNode->name(), currentNode->name_size() ) == 0 )
        {
            std::string_view name, value;
            if ( TryGetAttribute( currentNode, "name", &name ) )
            {
                if ( TryGetAttribute( currentNode, "value", &value ) )
                {
                    defaultParameterMap.insert( std::make_pair( name, value ) );
                }
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

    return true;
}

static float ClampValueToValidRange( const std::string_view valueName, float value, float min, float max )
{
    float result = value;
    if ( value < min || value > max )
    {
        result = std::clamp( result, min, max );
        LOG_STRING_FORMAT( "%.*s %f is out of valid range. Clamped to [%f, %f].\n", valueName.length(), valueName.data(), value, min, max );
    }
    return result;
}

struct SMaterialGatheringContext
{
    struct STexture
    {
        std::string m_Filename;
        std::string m_Id;
    };

    SMaterialGatheringContext( const std::filesystem::path& sceneFilename,
        std::unordered_map<std::string_view, EXMLMaterialType>& materialNameToEnumMap,
        std::vector<SMaterial>* materials,
        uint32_t textureIndexBase )
        : m_SceneFilename( sceneFilename )
        , m_MaterialNameToEnumMap( materialNameToEnumMap )
        , m_Materials( materials )
        , m_UnnamedTextureCount( 0 )
        , m_TextureIndexBase( textureIndexBase )
    {
    }

    bool CreateAndAddMaterial( const SValue& BSDF, uint32_t* materialId );

    bool TranslateMaterialFromBSDF( const SValue& BSDF, SMaterial* material, bool isTwoSided = false );

    bool LoadTexturesFromFiles( CScene* scene );

    std::unordered_map<const SValue*, uint32_t> m_BSDFToIdMap;
    std::unordered_map<const SValue*, uint32_t> m_TextureToIndexMap;
    std::vector<STexture> m_Textures;
    const std::filesystem::path& m_SceneFilename;
    std::unordered_map<std::string_view, EXMLMaterialType>& m_MaterialNameToEnumMap;
    std::vector<SMaterial>* m_Materials;
    uint32_t m_TextureIndexBase;
    uint32_t m_UnnamedTextureCount;
};

static bool ToNullTerminatedStringBuffer( std::string_view stringView, char* stringbuffer, uint32_t stringBufferSize )
{
    if ( stringView.length() + 1 <= stringBufferSize )
    {
        strncpy( stringbuffer, stringView.data(), stringView.length() );
        stringbuffer[ stringView.length() ] = '\0';
        return true;
    }
    return false;
}

static std::filesystem::path GetAbsoluteExternalFilename( char buffer[ MAX_PATH ], const std::filesystem::path& sceneFilename, std::string_view filename )
{
    bool hasString = ToNullTerminatedStringBuffer( filename, buffer, MAX_PATH );
    std::filesystem::path absoluteFilename( buffer );
    if ( absoluteFilename.is_relative() )
    {
        absoluteFilename = sceneFilename.parent_path() / absoluteFilename;
    }
    return absoluteFilename;
}

bool SMaterialGatheringContext::TranslateMaterialFromBSDF( const SValue& BSDF, SMaterial* material, bool isTwoSided )
{
    EXMLMaterialType materialType = EXMLMaterialType::eUnsupported;

    SValue* typeValue = BSDF.FindObjectField( "type" );
    if ( !typeValue )
    {
        LOG_STRING( "Cannot obtain bsdf type.\n" );
        return false;
    }

    {
        auto it = m_MaterialNameToEnumMap.find( typeValue->m_String );
        if ( it != m_MaterialNameToEnumMap.end() )
        {
            materialType = it->second;
        }
    }

    SValue* idValue = BSDF.FindObjectField( "id" );
    if ( idValue )
    {
        material->m_Name = idValue->m_String;
    }

    if ( materialType == EXMLMaterialType::eTwosided )
    {
        SValue* childBSDF = BSDF.FindFirstNestedObject( "bsdf" );
        if ( !childBSDF )
        {
            LOG_STRING( "Cannot find child BSDF inside a twosided BSDF.\n" );
            return false;
        }
        return TranslateMaterialFromBSDF( *childBSDF, material, true );
    }

    EMaterialType targetMaterialType = EMaterialType::Diffuse;
    bool hasDielectricIOR = false;
    bool hasConductorIOR = false;
    bool hasRoughness = false;
    bool hasDiffuseReflectance = false;
    switch ( materialType )
    {
    case EXMLMaterialType::eDiffuse:
    {
        hasDiffuseReflectance = true;
        targetMaterialType = EMaterialType::Diffuse;
        break;
    }
    case EXMLMaterialType::eRoughDiffuse:
    {
        hasDiffuseReflectance = true;
        hasRoughness = true;
        targetMaterialType = EMaterialType::Diffuse;
        break;
    }
    case EXMLMaterialType::eDielectric:
    {
        hasDielectricIOR = true;
        targetMaterialType = EMaterialType::Dielectric;
        break;
    }
    case EXMLMaterialType::eRoughDielectric:
    {
        hasDielectricIOR = true;
        hasRoughness = true;
        targetMaterialType = EMaterialType::Dielectric;
        break;
    }
    case EXMLMaterialType::eConductor:
    {
        hasConductorIOR = true;
        targetMaterialType = EMaterialType::Conductor;
        break;
    }
    case EXMLMaterialType::eRoughConductor:
    {
        hasConductorIOR = true;
        hasRoughness = true;
        targetMaterialType = EMaterialType::Conductor;
        break;
    }
    case EXMLMaterialType::ePlastic:
    {
        hasDielectricIOR = true;
        hasDiffuseReflectance = true;
        targetMaterialType = EMaterialType::Plastic;
        break;
    }
    case EXMLMaterialType::eRoughPlastic:
    {
        hasDielectricIOR = true;
        hasDiffuseReflectance = true;
        hasRoughness = true;
        targetMaterialType = EMaterialType::Plastic;
        break;
    }
    case EXMLMaterialType::eTwosided:
    {
        assert( false && "Should recursively translate child BSDF and never gets here." );
        return false;
    }
    default:
    {
        LOG_STRING_FORMAT( "Unsupported material type \'%.*s\', assigning default values.\n", typeValue->m_String.length(), typeValue->m_String.data() );
        break;
    }
    }

    assert( ( !hasDielectricIOR && !hasConductorIOR ) || ( hasDielectricIOR != hasConductorIOR ) );

    material->m_Albedo = { 0.0f, 0.0f, 0.0f };
    material->m_Roughness = 0.0f;
    material->m_IOR = { 1.0f, 1.0f, 1.0f };
    material->m_K = { 1.0f, 1.0f, 1.0f };
    material->m_Tiling = { 1.0f, 1.0f };
    material->m_MaterialType = targetMaterialType;
    material->m_AlbedoTextureIndex = INDEX_NONE;
    material->m_Multiscattering = false;
    material->m_IsTwoSided = isTwoSided;
    material->m_HasRoughnessTexture = false;

    if ( hasRoughness )
    {
        SValue* alphaValue = BSDF.FindObjectField( "alpha" );
        float alpha = alphaValue ? alphaValue->m_Float : 0.1f;
        material->m_Roughness = sqrt( alpha );
    }

    if ( hasDielectricIOR )
    {
        SValue* intIORValue = BSDF.FindObjectField( "int_ior" );
        SValue* extIORValue = BSDF.FindObjectField( "ext_ior" );
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
        SValue* etaValue = BSDF.FindObjectField( "eta" );
        SValue* extEtaValue = BSDF.FindObjectField( "ext_eta" );
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
                LOG_STRING( "Non-float ext_eta value is not supported.\n" );
            }
        }
        material->m_IOR.x = eta.x / extEta;
        material->m_IOR.y = eta.y / extEta;
        material->m_IOR.z = eta.z / extEta;

        SValue* kValue = BSDF.FindObjectField( "k" );
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
        int32_t albedoTextureIndex = INDEX_NONE;
        SValue* diffuseReflectanceValue = BSDF.FindObjectField( !hasDielectricIOR ? "reflectance" : "diffuse_reflectance" );
        if ( diffuseReflectanceValue )
        {
            if ( diffuseReflectanceValue->m_Type == EValueType::eRGB )
            {
                albedo = diffuseReflectanceValue->m_RGB;
            }
            else if ( diffuseReflectanceValue->m_Type == EValueType::eObject )
            {
                const std::string_view textureType = diffuseReflectanceValue->GetObjectField<std::string_view>( "type", "" );
                if ( strncmp( "bitmap", textureType.data(), textureType.length() ) == 0 )
                {
                    std::string_view textureFilename = diffuseReflectanceValue->GetObjectField<std::string_view>( "filename", "" );
                    char filenameBuffer[ MAX_PATH ];
                    std::filesystem::path textureFilepath = GetAbsoluteExternalFilename( filenameBuffer, m_SceneFilename, textureFilename );

                    auto textureToIndexPair = m_TextureToIndexMap.find( diffuseReflectanceValue );
                    if ( textureToIndexPair == m_TextureToIndexMap.end() )
                    {
                        uint32_t newTextureIndex = m_TextureIndexBase + (uint32_t)m_TextureToIndexMap.size();

                        m_TextureToIndexMap.insert( std::make_pair( diffuseReflectanceValue, newTextureIndex ) );

                        m_Textures.emplace_back();
                        STexture& newTexture = m_Textures.back();
                        newTexture.m_Filename = textureFilepath.u8string();

                        SValue* idValue = diffuseReflectanceValue->FindObjectField( "id" );
                        if ( idValue )
                        {
                            newTexture.m_Id = idValue->m_String;
                        }
                        else
                        {
                            // Generate an id if the texture does not have any.
                            sprintf_s( filenameBuffer, "Texture%03d", m_UnnamedTextureCount++ );
                            newTexture.m_Id = filenameBuffer;
                        }

                        albedoTextureIndex = (int32_t)newTextureIndex;
                    }
                    else
                    {
                        albedoTextureIndex = (int32_t)textureToIndexPair->second;
                    }
                }
                else
                {
                    LOG_STRING_FORMAT( "Unsupported texture type \'%.*s\'\n", textureType.length(), textureType.data() );
                }
            }
            else
            {
                LOG_STRING( "Unsupported diffuse reflectance type.\n" );
            }
        }
        material->m_Albedo = albedo;
        material->m_AlbedoTextureIndex = albedoTextureIndex;
    }

    bool materialIsConductor = material->m_MaterialType == EMaterialType::Conductor;
    float minIOR = materialIsConductor ? 0.0f : 1.0f;
    float maxIOR = materialIsConductor ? MAX_MATERIAL_ETA : MAX_MATERIAL_IOR;
    material->m_IOR.x = ClampValueToValidRange( "Material IOR.x", material->m_IOR.x, minIOR, maxIOR );
    material->m_IOR.y = ClampValueToValidRange( "Material IOR.y", material->m_IOR.y, minIOR, maxIOR );
    material->m_IOR.z = ClampValueToValidRange( "Material IOR.z", material->m_IOR.z, minIOR, maxIOR );
    material->m_K.x = ClampValueToValidRange( "Material K.x", material->m_K.x, 0.0f, MAX_MATERIAL_K );
    material->m_K.y = ClampValueToValidRange( "Material K.y", material->m_K.y, 0.0f, MAX_MATERIAL_K );
    material->m_K.z = ClampValueToValidRange( "Material K.z", material->m_K.z, 0.0f, MAX_MATERIAL_K );

    return true;
}

bool SMaterialGatheringContext::CreateAndAddMaterial( const SValue& BSDF, uint32_t* materialId )
{
    SMaterial newMaterial;
    std::string_view newMaterialName;
    if ( TranslateMaterialFromBSDF( BSDF, &newMaterial, &newMaterialName ) )
    {
        uint32_t newMaterialId = (uint32_t)m_Materials->size();
        m_Materials->emplace_back( newMaterial );
        m_BSDFToIdMap.insert( std::make_pair( &BSDF, newMaterialId ) );
        *materialId = newMaterialId;
        return true;
    }
    return false;
}

bool SMaterialGatheringContext::LoadTexturesFromFiles( CScene* scene )
{
    STextureCodec* codec = CTexture::CreateCodec();
    if ( !codec )
    {
        return false;
    }

    scene->m_Textures.reserve( scene->m_Textures.size() + m_Textures.size() );
    for ( const STexture& texture : m_Textures )
    {
        scene->m_Textures.emplace_back();
        CTexture& newTexture = scene->m_Textures.back();
        newTexture.Clear();
        newTexture.m_Name = std::move( texture.m_Id );
        if ( !newTexture.LoadFromFile( texture.m_Filename.c_str(), codec ) )
        {
            LOG_STRING_FORMAT( "Loading texture from file \"%s\" failed.\n", texture.m_Filename.c_str() );
        }
    }

    CTexture::DestroyCodec( codec );
    return true;
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
    std::vector<SValue*> sceneValues;
    if ( !BuildValueGraph( &valueList, &doc, &sceneValues ) )
    {
        LOG_STRING( "Failed to build value graph.\n" );
        return false;
    }

    if ( sceneValues.size() > 1 )
    {
        LOG_STRING( "The file contains more than 1 scenes, only the first one is supported.\n" );
    }

    std::unordered_map<std::string_view, EShapeType> shapeNameToEnumMap = { { "obj", EShapeType::eObj }, { "rectangle", EShapeType::eRectangle } };
    std::unordered_map<std::string_view, EXMLMaterialType> materialNameToEnumMap =
    {
          { "diffuse", EXMLMaterialType::eDiffuse }
        , { "roughdiffuse", EXMLMaterialType::eRoughDiffuse }
        , { "dielectric", EXMLMaterialType::eDielectric }
        , { "roughdielectric", EXMLMaterialType::eRoughDielectric }
        , { "conductor", EXMLMaterialType::eConductor }
        , { "roughconductor", EXMLMaterialType::eRoughConductor }
        , { "plastic", EXMLMaterialType::ePlastic }
        , { "roughplastic", EXMLMaterialType::eRoughPlastic }
        , { "twosided", EXMLMaterialType::eTwosided }
    };

    SMaterialGatheringContext materialGatheringContext( filepath, materialNameToEnumMap, &m_Materials, (uint32_t)m_Textures.size() );

    std::vector<std::pair<std::string_view, SValue*>>& rootObjectValues = *sceneValues[ 0 ]->m_NestedObjects;
    for ( auto& rootObjectValue : rootObjectValues )
    {
        if ( strncmp( "integrator", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            SValue* typeValue = rootObjectValue.second->FindObjectField( "type" );
            if ( strncmp( "path", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
            {
                m_MaxBounceCount = rootObjectValue.second->GetObjectField<int32_t>( "max_depth", 3 );
            }
            else
            {
                LOG_STRING_FORMAT( "Unsupported integrator type \'%.*s\'.\n", typeValue->m_String.length(), typeValue->m_String.data() );
            }
        }
        else if ( strncmp( "sensor", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            const std::string_view sensorTypeString = rootObjectValue.second->GetObjectField<std::string_view>( "type", "" );
            if ( strncmp( "perspective", sensorTypeString.data(), sensorTypeString.length() ) == 0 )
            {
                m_CameraType = ECameraType::PinHole;
            }
            else if ( strncmp( "thinlens", sensorTypeString.data(), sensorTypeString.length() ) == 0 )
            {
                m_CameraType = ECameraType::ThinLens;
            }
            else
            {
                LOG_STRING_FORMAT( "Unsupported sensor type \'%.*s\'.\n", sensorTypeString.length(), sensorTypeString.data() );
            }

            {
                SValue* transformValue = rootObjectValue.second->FindObjectField( "to_world" );
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
                SValue* filmValue = rootObjectValue.second->FindFirstNestedObject( "film" );
                if ( filmValue )
                {
                    {
                        m_ResolutionWidth = filmValue->GetObjectField<int32_t>( "width", 768 );
                        m_ResolutionHeight = filmValue->GetObjectField<int32_t>( "height", 576 );
                    }

                    SValue* filterValue = filmValue->FindFirstNestedObject( "rfilter" );
                    if ( filterValue )
                    {
                        SValue* typeValue = filterValue->FindObjectField( "type" );
                        if ( typeValue )
                        {
                            if ( strncmp( "box", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                m_Filter = EFilter::Box;
                                m_FilterRadius = filterValue->GetObjectField<float>( "radius", 0.5f );
                            }
                            else if ( strncmp( "tent", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                m_Filter = EFilter::Triangle;
                                m_FilterRadius = filterValue->GetObjectField<float>( "radius", 1.f );
                            }
                            else if ( strncmp( "gaussian", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                m_Filter = EFilter::Gaussian;
                                m_GaussianFilterAlpha = filterValue->GetObjectField<float>( "stddev", 0.5f );
                                m_FilterRadius = m_GaussianFilterAlpha * 4;
                            }
                            else if ( strncmp( "mitchell", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                m_Filter = EFilter::Mitchell;
                                m_MitchellB = filterValue->GetObjectField<float>( "B", 1.0f / 3.0f );
                                m_MitchellB = filterValue->GetObjectField<float>( "C", 1.0f / 3.0f );
                                m_FilterRadius = 2.0f;
                            }
                            else if ( strncmp( "lanczos", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                m_Filter = EFilter::LanczosSinc;
                                m_LanczosSincTau = filterValue->GetObjectField<int32_t>( "lobes", 3 );
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

            const float aspect = (float)m_ResolutionWidth / m_ResolutionHeight;

            // Update film size
            m_FilmSize.x = 0.035f;
            m_FilmSize.y = m_FilmSize.x / std::fmax( aspect, 0.0001f );

            SValue* focalLengthValue = rootObjectValue.second->FindObjectField( "focal_length" );
            if ( focalLengthValue )
            {
                float focalLengthMilliMeter = (float)atof( focalLengthValue->m_String.data() );
                m_FocalLength = focalLengthMilliMeter * 0.001f;

                if ( m_CameraType == ECameraType::PinHole )
                {
                    LOG_STRING( "Using focal length for PinHole camera is not supported.\n" );
                }
            }
            else
            {
                m_FocalLength = 0.05f;
            }

            float fovDeg = 50.f;
            SValue* fovValue = rootObjectValue.second->FindObjectField( "fov" );
            if ( fovValue && fovValue->m_Type == EValueType::eFloat )
            {
                fovDeg = std::clamp( fovValue->m_Float, 0.0001f, 179.99f );

                if ( m_CameraType == ECameraType::ThinLens )
                {
                    LOG_STRING( "Using fov for ThinLens camera is not supported.\n" );
                }
            }
            m_FoVX = XMConvertToRadians( fovDeg );

            if ( m_CameraType == ECameraType::PinHole )
            {
                std::string_view fovAxis = rootObjectValue.second->GetObjectField<std::string_view>( "fov_axis", "x" );
                if ( strncmp( "x", fovAxis.data(), fovAxis.length() ) == 0 )
                {
                }
                else if ( strncmp( "y", fovAxis.data(), fovAxis.length() ) == 0 )
                {
                    m_FoVX *= aspect;
                }
                else
                {
                    LOG_STRING_FORMAT( "Unsupported fov_axis \'%.*s\'.\n", fovAxis.length(), fovAxis.data() );
                }
            }
            else if ( m_CameraType == ECameraType::ThinLens )
            {
                SValue* apertureRadiusValue = rootObjectValue.second->FindObjectField( "aperture_radius" );
                m_RelativeAperture = apertureRadiusValue ? m_FocalLength / ( apertureRadiusValue->m_Float * 2 ) : 8.0f;
            
                SValue* focalDistanceValue = rootObjectValue.second->FindObjectField( "focus_distance" );
                m_FocalDistance = focalDistanceValue ? focalDistanceValue->m_Float : 2.0f;
            }
        }
        else if ( strncmp( "bsdf", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            uint32_t materialId = 0;
            materialGatheringContext.CreateAndAddMaterial( *rootObjectValue.second, &materialId );
        }
        else if ( strncmp( "shape", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            SValue* typeValue = rootObjectValue.second->FindObjectField( "type" );
            if ( !typeValue )
            {
                LOG_STRING( "Cannot determine the type of shape.\n" );
            }
            else
            {
                
                SValue* transformValue = rootObjectValue.second->FindObjectField( "to_world" );
                XMFLOAT4X4 transform = transformValue ? transformValue->m_Matrix : MathHelper::s_IdentityMatrix4x4;

                SValue* emitterValue = rootObjectValue.second->FindFirstNestedObject( "emitter" );
                bool isALight = emitterValue != nullptr;

                uint32_t materialId = INVALID_MATERIAL_ID;
                SValue* bsdfValue = rootObjectValue.second->FindFirstNestedObject( "bsdf" );
                if ( bsdfValue )
                {
                    auto iter = materialGatheringContext.m_BSDFToIdMap.find( bsdfValue );
                    if ( iter == materialGatheringContext.m_BSDFToIdMap.end() )
                    {
                        materialGatheringContext.CreateAndAddMaterial( *bsdfValue, &materialId );
                    }
                    else
                    {
                        materialId = iter->second;
                    }
                }
                else if ( isALight )
                {
                    // Assign a pitch black non-reflective material to light
                    materialId = (uint32_t)m_Materials.size();
                    SMaterial material;
                    material.m_Albedo = XMFLOAT3( 0.f, 0.f, 0.f );
                    material.m_Roughness = 0.f;
                    material.m_IOR = XMFLOAT3( 1.f, 1.f, 1.f );
                    material.m_MaterialType = EMaterialType::Diffuse;
                    material.m_AlbedoTextureIndex = INDEX_NONE;
                    material.m_Multiscattering = false;
                    material.m_IsTwoSided = false;
                    material.m_HasRoughnessTexture = false;
                    material.m_Name = "LightMaterial";
                    m_Materials.emplace_back( material );
                }

                EShapeType shapeType = EShapeType::Unsupported;
                auto itShapeType = shapeNameToEnumMap.find( typeValue->m_String );
                if ( itShapeType != shapeNameToEnumMap.end() )
                {
                    shapeType = itShapeType->second;
                }

                bool meshCreated = false;
                switch ( shapeType )
                {
                case EShapeType::eObj:
                {
                    SValue* filenameValue = rootObjectValue.second->FindObjectField( "filename" );
                    if ( !filenameValue )
                    {
                        LOG_STRING( "Cannot find filename of an obj shape.\n" );
                    }
                    else
                    {
                        char zeroTerminatedFilename[ MAX_PATH ];
                        const std::filesystem::path filenamePath = GetAbsoluteExternalFilename( zeroTerminatedFilename, filepath, filenameValue->m_String );

                        SMeshProcessingParams processingParams;
                        processingParams.m_ApplyTransform = false;
                        processingParams.m_ChangeWindingOrder = true;
                        processingParams.m_FlipTexcoordV = true;
                        processingParams.m_MaterialIndexOverride = materialId;

                        m_Meshes.emplace_back();
                        Mesh& newMesh = m_Meshes.back();
                        if ( newMesh.LoadFromWavefrontOBJFile( filenamePath, processingParams, nullptr, nullptr ) )
                        {
                            newMesh.SetName( zeroTerminatedFilename );
                            meshCreated = true;
                        }
                        else
                        {
                            LOG_STRING_FORMAT( "Failed to load wavefront obj file \'%s\'.\n", filenamePath.u8string().c_str() );
                        }
                    }
                    break;
                }
                case EShapeType::eRectangle:
                {
                    Mesh mesh;
                    if ( mesh.GenerateRectangle( materialId, true, MathHelper::s_IdentityMatrix4x4 ) )
                    {
                        mesh.SetName( "rectangle" );
                        m_Meshes.emplace_back( mesh );
                        meshCreated = true;
                    }
                    else
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

                if ( meshCreated )
                {
                    uint32_t instanceIndex = (uint32_t)m_InstanceTransforms.size();
                    m_InstanceTransforms.push_back( XMFLOAT4X3( transform._11, transform._12, transform._13, transform._21, transform._22, transform._23, transform._31, transform._32, transform._33, transform._41, transform._42, transform._43 ) );

                    if ( GetLightCount() >= s_MaxLightsCount )
                    {
                        LOG_STRING( "An emitter is discarded since maximum light count is hit." );
                        continue;
                    }

                    if ( isALight )
                    {
                        const SValue* typeValue = emitterValue->FindObjectField( "type" );
                        if ( typeValue && typeValue->m_Type == EValueType::eString )
                        {
                            if ( strncmp( "area", typeValue->m_String.data(), typeValue->m_String.length() ) == 0 )
                            {
                                SMeshLight light;
                                light.m_InstanceIndex = instanceIndex;

                                light.color = emitterValue->GetObjectField<XMFLOAT3>( "radiance", XMFLOAT3( 1.f, 1.f, 1.f ) );

                                m_MeshLights.emplace_back( light );
                            }
                            else
                            {
                                LOG_STRING_FORMAT( "Unsupported emitter type nested in a shape \'%.*s\'.\n", typeValue->m_String.length(), typeValue->m_String.data() );
                            }
                        }
                        else
                        {
                            LOG_STRING( "Cannot determine emitter type.\n" );
                        }
                    }
                }
            }
        }
        else if ( strncmp( "emitter", rootObjectValue.first.data(), rootObjectValue.first.length() ) == 0 )
        {
            SValue* emitterTypeValue = rootObjectValue.second->FindObjectField( "type" );
            if ( emitterTypeValue && emitterTypeValue->m_Type == EValueType::eString )
            {
                if ( GetLightCount() >= s_MaxLightsCount )
                {
                    LOG_STRING( "An emitter is discarded since maximum light count is hit." );
                    continue;
                }

                if ( strncmp( "constant", emitterTypeValue->m_String.data(), emitterTypeValue->m_String.length() ) == 0 )
                {
                    if ( !m_EnvironmentLight )
                    {
                        m_EnvironmentLight = std::make_shared<SEnvironmentLight>();
                    }
                    else
                    {
                        LOG_STRING( "Having more than 1 constant emitter is not supported.\n" );
                        continue;
                    }

                    m_EnvironmentLight->m_Color = rootObjectValue.second->GetObjectField<XMFLOAT3>( "radiance", XMFLOAT3( 1.f, 1.f, 1.f ) );
                }
                else if ( strncmp( "directional", emitterTypeValue->m_String.data(), emitterTypeValue->m_String.length() ) == 0 )
                {
                    SPunctualLight newLight;
                    newLight.m_IsDirectionalLight = true;
                    newLight.SetEulerAnglesFromDirection( XMFLOAT3( 0.f, -1.f, 0.f ) );

                    newLight.m_Color = rootObjectValue.second->GetObjectField<XMFLOAT3>( "irradiance", XMFLOAT3( 1.f, 1.f, 1.f ) );

                    SValue* directionValue = rootObjectValue.second->FindObjectField( "direction" );
                    if ( directionValue )
                    {
                        if ( directionValue->m_Type == EValueType::eVector )
                        {
                            newLight.SetEulerAnglesFromDirection( directionValue->m_Vector );
                        }
                        else
                        {
                            LOG_STRING_FORMAT( "Non-vector direction type is not supported.\n" );
                        }
                    }

                    m_PunctualLights.emplace_back( newLight );
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

    // Start loading texture
    if ( !materialGatheringContext.LoadTexturesFromFiles( this ) )
    {
        LOG_STRING( "Loading textures failed!\n" );
    }

    return true;
}
