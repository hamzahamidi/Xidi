/*****************************************************************************
 * Xidi
 *   DirectInput interface for XInput controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016-2020
 *************************************************************************//**
 * @file Mapper.cpp
 *   Implementation of functionality common to all mappers.
 *****************************************************************************/


#include "ApiDirectInput.h"
#include "Configuration.h"
#include "Globals.h"
#include "Mapper.h"
#include "Mapper/ExtendedGamepad.h"
#include "Mapper/XInputNative.h"
#include "Mapper/XInputSharedTriggers.h"
#include "Mapper/StandardGamepad.h"
#include "Message.h"
#include "Strings.h"

#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <Xinput.h>


// -------- MACROS --------------------------------------------------------- //
// Used for strings that need to be available in multiple formats.

#define XIDI_AXIS_NAME_X                    "X Axis"
#define XIDI_AXIS_NAME_Y                    "Y Axis"
#define XIDI_AXIS_NAME_Z                    "Z Axis"
#define XIDI_AXIS_NAME_RX                   "RotX Axis"
#define XIDI_AXIS_NAME_RY                   "RotY Axis"
#define XIDI_AXIS_NAME_RZ                   "RotZ Axis"
#define XIDI_AXIS_NAME_UNKNOWN              "Unknown Axis"
#define XIDI_BUTTON_NAME_FORMAT             "Button %u"
#define XIDI_POV_NAME_FORMAT                "POV %u"


namespace Xidi
{
    // -------- INTERNAL VARIABLES ----------------------------------------- //

    static std::unordered_map<std::wstring_view, EMapperType> mapperTypeStrings = {
        {L"ExtendedGamepad",                     EMapperType::ExtendedGamepad},
        {L"StandardGamepad",                     EMapperType::StandardGamepad},
        {L"XInputNative",                        EMapperType::XInputNative},
        {L"XInputSharedTriggers",                EMapperType::XInputSharedTriggers}
    };


    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Creates a new mapper of the specified type, using the `new` operator.
    /// @return Pointer to the newly-created mapper, or `nullptr` in the event of an error.
    static Mapper* CreateMapperOfType(const EMapperType type)
    {
        Mapper* newMapper = nullptr;

        switch (type)
        {
        case EMapperType::XInputNative:
            newMapper = new XInputNativeMapper();
            break;

        case EMapperType::XInputSharedTriggers:
            newMapper = new XInputSharedTriggersMapper();
            break;

        case EMapperType::StandardGamepad:
            newMapper = new StandardGamepadMapper();
            break;

        case EMapperType::ExtendedGamepad:
            newMapper = new ExtendedGamepadMapper();
            break;
        }

        return newMapper;
    }

    /// Compares the specified GUID with the known list of object unique identifiers.
    /// Returns a string that represents the specified GUID.
    /// @param [in] pguid GUID to check.
    /// @return String representation of the GUID's semantics, even if unknown.
    static const wchar_t* DataFormatStringFromObjectUniqueIdentifier(const GUID* pguid)
    {
        if (nullptr == pguid)
            return L"(any)";
        if (GUID_XAxis == *pguid)
            return _CRT_WIDE(XIDI_AXIS_NAME_X);
        if (GUID_YAxis == *pguid)
            return _CRT_WIDE(XIDI_AXIS_NAME_Y);
        if (GUID_ZAxis == *pguid)
            return _CRT_WIDE(XIDI_AXIS_NAME_Z);
        if (GUID_RxAxis == *pguid)
            return _CRT_WIDE(XIDI_AXIS_NAME_RX);
        if (GUID_RyAxis == *pguid)
            return _CRT_WIDE(XIDI_AXIS_NAME_RY);
        if (GUID_RzAxis == *pguid)
            return _CRT_WIDE(XIDI_AXIS_NAME_RZ);
        if (GUID_Slider == *pguid)
            return L"Slider";
        if (GUID_Button == *pguid)
            return L"Button";
        if (GUID_Key == *pguid)
            return L"Key";
        if (GUID_POV == *pguid)
            return L"POV";
        if (GUID_Unknown == *pguid)
            return L"Unknown from GUID";

        return L"(unknown)";
    }

    /// Dumps a data format definition to the log.
    /// Intended as a debugging aid.
    static void DumpDataFormatToLog(LPCDIDATAFORMAT lpdf)
    {
        Message::Output(Message::ESeverity::Debug, L"Begin dump of data format.");

        // First, dump the top-level structure members along with some preliminary validity checks.
        Message::Output(Message::ESeverity::Debug, L"  Metadata:");
        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwSize = %d (%s; expected %d)", lpdf->dwSize, (sizeof(DIDATAFORMAT) == lpdf->dwSize ? L"OK" : L"INCORRECT"), sizeof(DIDATAFORMAT));
        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwObjSize = %d (%s; expected %d)", lpdf->dwObjSize, (sizeof(DIOBJECTDATAFORMAT) == lpdf->dwObjSize ? L"OK" : L"INCORRECT"), sizeof(DIOBJECTDATAFORMAT));
        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwFlags = 0x%x (%s)", lpdf->dwFlags, (DIDF_ABSAXIS == lpdf->dwFlags ? L"DIDF_ABSAXIS" : (DIDF_RELAXIS == lpdf->dwFlags ? L"DIDF_RELAXIS" : L"UNKNOWN VALUE")));
        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwDataSize = %d (%s)", lpdf->dwDataSize, (0 == lpdf->dwDataSize % 4 ? L"POSSIBLY OK; is a multiple of 4" : L"INCORRECT; must be a multiple of 4"));
        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwNumObjs = %d", lpdf->dwNumObjs);

        // Second, dump the individual objects.
        Message::Output(Message::ESeverity::Debug, L"  Objects:");
        for (DWORD i = 0; i < lpdf->dwNumObjs; ++i)
        {
            Message::OutputFormatted(Message::ESeverity::Debug, L"    rgodf[%3d]: { pguid = %s, dwOfs = %d, dwType = 0x%x, dwFlags = 0x%x }", i, DataFormatStringFromObjectUniqueIdentifier(lpdf->rgodf[i].pguid), lpdf->rgodf[i].dwOfs, lpdf->rgodf[i].dwType, lpdf->rgodf[i].dwFlags);
        }

        Message::Output(Message::ESeverity::Debug, L"End dump of data format.");
    }

    /// Compares the specified value to the possible values for the dwHow member of a property header.
    /// Returns a string representation.
    /// @param [in] dwHow Value to check.
    /// @return String representation of the identification method, even if unknown.
    static const wchar_t* PropertyStringFromIdentificationMethod(DWORD dwHow)
    {
        if (DIPH_DEVICE == dwHow)
            return L"DIPH_DEVICE";
        if (DIPH_BYOFFSET == dwHow)
            return L"DIPH_BYOFFSET";
        if (DIPH_BYUSAGE == dwHow)
            return L"DIPH_BYUSAGE";
        if (DIPH_BYID == dwHow)
            return L"DIPH_BYID";

        return L"(unknown)";
    }

    /// Dumps the top-level members of a property request (either get or set).
    /// @param [in] pdiph Pointer to the property header.
    static void DumpPropertyHeaderToLog(LPCDIPROPHEADER pdiph)
    {
        Message::Output(Message::ESeverity::Debug, L"Begin dump of property request header.");

        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwSize = %d", pdiph->dwSize);
        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwHeaderSize = %d (%s; expected %d)", pdiph->dwHeaderSize, (sizeof(DIPROPHEADER) == pdiph->dwHeaderSize ? L"OK" : L"INCORRECT"), sizeof(DIPROPHEADER));
        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwObj = %d (%s)", pdiph->dwObj, (DIPH_DEVICE != pdiph->dwHow || 0 == pdiph->dwObj ? L"POSSIBLY OK" : L"INCORRECT; must be 0 in this case"));
        Message::OutputFormatted(Message::ESeverity::Debug, L"    dwHow = %d (%s)", pdiph->dwHow, PropertyStringFromIdentificationMethod(pdiph->dwHow));

        Message::Output(Message::ESeverity::Debug, L"End dump of property request header.");
    }

    /// Specifies the number of bytes consumed by an instance of an object of the specified type.
    /// @param [in] type Instance type.
    /// @return Number of bytes consumed by an instance of that type of object, or 0 if the type is not recognized.
    static DWORD SizeofInstance(const EInstanceType type)
    {
        DWORD szInstance = 0;

        switch (type)
        {
        case EInstanceType::InstanceTypeAxis:
        case EInstanceType::InstanceTypePov:
            szInstance = sizeof(LONG);
            break;

        case EInstanceType::InstanceTypeButton:
            szInstance = sizeof(BYTE);
            break;
        }

        return szInstance;
    }


    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "Mapper.h" for documentation.

    Mapper::Mapper(void) : axisProperties(nullptr), cachedValueXInputLT(XInputController::kTriggerNeutral), cachedValueXInputRT(XInputController::kTriggerNeutral), dataPacketSize(0), instanceToOffset(), mapsValid(FALSE), offsetToInstance(), axisOffsetsUnused(), buttonOffsetsUnused(), povOffsetsUnused()
    {
        // Nothing to do here.
    }

    // ---------

    Mapper::~Mapper(void)
    {
        if (nullptr != axisProperties)
            delete[] axisProperties;
    }


    // -------- CLASS METHODS -------------------------------------------------- //
    // See "Mapper.h" for documentation.

    Mapper* Mapper::Create(void)
    {
        static EMapperType configuredMapperType = kDefaultMapperType;

        // Mappers might be created multiple times, but always of the same type, so check the configuration once and cache the result.
        static std::once_flag getConfiguredTypeFlag;
        std::call_once(getConfiguredTypeFlag, []() {
            const Configuration::Configuration& config = Globals::GetConfiguration();

            if ((true == config.IsDataValid()) && (true == config.GetData().SectionNamePairExists(Strings::kStrConfigurationSectionMapper, Strings::kStrConfigurationSettingMapperType)))
            {
                const EMapperType requestedMapperType = TypeFromString(config.GetData()[Strings::kStrConfigurationSectionMapper][Strings::kStrConfigurationSettingMapperType].FirstValue().GetStringValue());

                if (EMapperType::Invalid != requestedMapperType)
                    configuredMapperType = requestedMapperType;
            }
        });

        return CreateMapperOfType(configuredMapperType);
    }

    // --------

    EMapperType Mapper::TypeFromString(std::wstring_view typeString)
    {
        auto it = mapperTypeStrings.find(typeString);

        if (mapperTypeStrings.end() == it)
            return EMapperType::Invalid;
        else
            return it->second;
    }


    // -------- HELPERS ---------------------------------------------------- //
    // See "Mapper.h" for documentation.

    LONG Mapper::ApplyAxisPropertiesToRawValue(const TInstance axisInstance, const LONG value)
    {
        // Calculate axis physical range of motion, center axis position and the value's displacement from it.
        TInstanceIdx axisIndex = ExtractIdentifierInstanceIndex(axisInstance);
        const double axisCenterPosition = (double)(axisProperties[axisIndex].rangeMax + axisProperties[axisIndex].rangeMin) / 2.0;
        const double axisPhysicalRange = (double)(axisProperties[axisIndex].rangeMax) - axisCenterPosition;
        const double axisValueDisp = (double)value - axisCenterPosition;
        const double axisValueDispAbs = abs(axisValueDisp);

        // Calculate the value's displacement as a percentage of the axis' physical range of motion, mapped to a saturation and deadzone range (0 to 10000).
        // Use this to figure out what its percentage should be, given the axis properties of deadzone and saturation.
        DWORD axisValuePctRange = (DWORD)(axisValueDispAbs / axisPhysicalRange * 10000.0);
        if (axisValuePctRange <= axisProperties[axisIndex].deadzone)
            axisValuePctRange = 0;
        else if (axisValuePctRange >= axisProperties[axisIndex].saturation)
            axisValuePctRange = 10000;
        else
            axisValuePctRange = (DWORD)MapValueInRangeToRange(axisValuePctRange, axisProperties[axisIndex].deadzone, axisProperties[axisIndex].saturation, 0, 10000);

        // Compute the final value for the axis, taking into consideration deadzone and saturation.
        LONG axisFinalValue;
        if (axisValueDisp > 0)
            axisFinalValue = (LONG)(axisCenterPosition + (axisPhysicalRange * (axisValuePctRange / 10000.0)));
        else
            axisFinalValue = (LONG)(axisCenterPosition - (axisPhysicalRange * (axisValuePctRange / 10000.0)));

        return axisFinalValue;
    }

    // ---------

    template <> void Mapper::AxisTypeToString<LPSTR>(REFGUID axisTypeGUID, LPSTR buf, const int bufcount)
    {
        if (axisTypeGUID == GUID_XAxis)
            strncpy_s(buf, bufcount, XIDI_AXIS_NAME_X, _countof(XIDI_AXIS_NAME_X));
        else if (axisTypeGUID == GUID_YAxis)
            strncpy_s(buf, bufcount, XIDI_AXIS_NAME_Y, _countof(XIDI_AXIS_NAME_Y));
        else if (axisTypeGUID == GUID_ZAxis)
            strncpy_s(buf, bufcount, XIDI_AXIS_NAME_Z, _countof(XIDI_AXIS_NAME_Z));
        else if (axisTypeGUID == GUID_RxAxis)
            strncpy_s(buf, bufcount, XIDI_AXIS_NAME_RX, _countof(XIDI_AXIS_NAME_RX));
        else if (axisTypeGUID == GUID_RyAxis)
            strncpy_s(buf, bufcount, XIDI_AXIS_NAME_RY, _countof(XIDI_AXIS_NAME_RY));
        else if (axisTypeGUID == GUID_RzAxis)
            strncpy_s(buf, bufcount, XIDI_AXIS_NAME_RZ, _countof(XIDI_AXIS_NAME_RZ));
        else
            strncpy_s(buf, bufcount, XIDI_AXIS_NAME_UNKNOWN, _countof(XIDI_AXIS_NAME_UNKNOWN));
    }

    template <> void Mapper::AxisTypeToString<LPWSTR>(REFGUID axisTypeGUID, LPWSTR buf, const int bufcount)
    {
        if (axisTypeGUID == GUID_XAxis)
            wcsncpy_s(buf, bufcount, _CRT_WIDE(XIDI_AXIS_NAME_X), _countof(_CRT_WIDE(XIDI_AXIS_NAME_X)));
        else if (axisTypeGUID == GUID_YAxis)
            wcsncpy_s(buf, bufcount, _CRT_WIDE(XIDI_AXIS_NAME_Y), _countof(_CRT_WIDE(XIDI_AXIS_NAME_Y)));
        else if (axisTypeGUID == GUID_ZAxis)
            wcsncpy_s(buf, bufcount, _CRT_WIDE(XIDI_AXIS_NAME_Z), _countof(_CRT_WIDE(XIDI_AXIS_NAME_Z)));
        else if (axisTypeGUID == GUID_RxAxis)
            wcsncpy_s(buf, bufcount, _CRT_WIDE(XIDI_AXIS_NAME_RX), _countof(_CRT_WIDE(XIDI_AXIS_NAME_RX)));
        else if (axisTypeGUID == GUID_RyAxis)
            wcsncpy_s(buf, bufcount, _CRT_WIDE(XIDI_AXIS_NAME_RY), _countof(_CRT_WIDE(XIDI_AXIS_NAME_RY)));
        else if (axisTypeGUID == GUID_RzAxis)
            wcsncpy_s(buf, bufcount, _CRT_WIDE(XIDI_AXIS_NAME_RZ), _countof(_CRT_WIDE(XIDI_AXIS_NAME_RZ)));
        else
            wcsncpy_s(buf, bufcount, _CRT_WIDE(XIDI_AXIS_NAME_UNKNOWN), _countof(_CRT_WIDE(XIDI_AXIS_NAME_UNKNOWN)));
    }

    // ---------

    template <> void Mapper::ButtonToString<LPSTR>(unsigned int buttonNumber, LPSTR buf, const int bufcount)
    {
        sprintf_s(buf, bufcount, XIDI_BUTTON_NAME_FORMAT, buttonNumber);
    }

    template <> void Mapper::ButtonToString<LPWSTR>(unsigned int buttonNumber, LPWSTR buf, const int bufcount)
    {
        swprintf_s(buf, bufcount, _CRT_WIDE(XIDI_BUTTON_NAME_FORMAT), buttonNumber);
    }

    // ---------

    BOOL Mapper::CheckAndSetOffsets(BOOL* base, const DWORD count)
    {
        for (DWORD i = 0; i < count; ++i)
            if (base[i] != FALSE) return FALSE;

        for (DWORD i = 0; i < count; ++i)
            base[i] = TRUE;

        return TRUE;
    }

    // ---------

    template <typename DeviceObjectInfoType> void Mapper::FillObjectInstanceInfo(DeviceObjectInfoType* instanceInfo, EInstanceType instanceType, TInstanceIdx instanceNumber)
    {
        // Obtain the number of objects of each type.
        const TInstanceCount numAxes = NumInstancesOfType(EInstanceType::InstanceTypeAxis);
        const TInstanceCount numPov = NumInstancesOfType(EInstanceType::InstanceTypePov);
        const TInstanceCount numButtons = NumInstancesOfType(EInstanceType::InstanceTypeButton);

        // Initialize the structure and fill out common parts.
        ZeroMemory(instanceInfo, sizeof(*instanceInfo));
        instanceInfo->dwSize = sizeof(*instanceInfo);
        instanceInfo->dwType = DIDFT_MAKEINSTANCE(instanceNumber);
        instanceInfo->dwFlags = DIDOI_POLLED;

        // Fill in the rest of the structure based on the instance type.
        switch (instanceType)
        {
        case EInstanceType::InstanceTypeAxis:
            instanceInfo->dwOfs = (instanceNumber * SizeofInstance(instanceType));
            instanceInfo->guidType = AxisTypeFromInstanceNumber(instanceNumber);
            instanceInfo->dwType |= DIDFT_ABSAXIS;
            instanceInfo->dwFlags |= DIDOI_ASPECTPOSITION;
            AxisTypeToString(instanceInfo->guidType, instanceInfo->tszName, _countof(instanceInfo->tszName));
            break;

        case EInstanceType::InstanceTypePov:
            instanceInfo->dwOfs = (numAxes * SizeofInstance(EInstanceType::InstanceTypeAxis)) + (instanceNumber * SizeofInstance(instanceType));
            instanceInfo->guidType = GUID_POV;
            instanceInfo->dwType |= DIDFT_POV;
            PovToString((unsigned int)(1 + instanceNumber), instanceInfo->tszName, _countof(instanceInfo->tszName));
            break;

        case EInstanceType::InstanceTypeButton:
            instanceInfo->dwOfs = (numAxes * SizeofInstance(EInstanceType::InstanceTypeAxis)) + (numPov * SizeofInstance(EInstanceType::InstanceTypePov)) + (instanceNumber * SizeofInstance(instanceType));
            instanceInfo->guidType = GUID_Button;
            instanceInfo->dwType |= DIDFT_PSHBUTTON;
            ButtonToString((unsigned int)(1 + instanceNumber), instanceInfo->tszName, _countof(instanceInfo->tszName));
            break;
        }

        // This is undocumented, but correct, DirectInput behavior.
        // Documentation suggests that EnumObjects will always return a native offset that has nothing to do with application data format.
        // However, in practice, DirectInput will use application data format offsets if they have been set, and 0xffffffff (-1) for any objects that do not exist in the application data format.
        if (IsApplicationDataFormatSet())
            instanceInfo->dwOfs = OffsetForInstance(MakeInstanceIdentifier(instanceType, instanceNumber));
    }

    template void Mapper::FillObjectInstanceInfo(LPDIDEVICEOBJECTINSTANCEA instanceInfo, EInstanceType instanceType, TInstanceIdx instanceNumber);
    template void Mapper::FillObjectInstanceInfo(LPDIDEVICEOBJECTINSTANCEW instanceInfo, EInstanceType instanceType, TInstanceIdx instanceNumber);

    // ---------

    void Mapper::InitializeAxisProperties(void)
    {
        if (nullptr == axisProperties)
        {
            const TInstanceCount numAxes = NumInstancesOfType(EInstanceType::InstanceTypeAxis);

            axisProperties = new SAxisProperties[numAxes];

            for (TInstanceIdx i = 0; i < (TInstanceIdx)numAxes; ++i)
            {
                axisProperties[i].rangeMin = kDefaultAxisRangeMin;
                axisProperties[i].rangeMax = kDefaultAxisRangeMax;
                axisProperties[i].deadzone = kDefaultAxisDeadzone;
                axisProperties[i].saturation = kDefaultAxisSaturation;
            }
        }
    }

    // ---------

    TInstance Mapper::InstanceIdentifierFromDirectInputIdentifier(DWORD diIdentifier)
    {
        EInstanceType type = (EInstanceType)-1;
        TInstanceIdx idx = (TInstanceIdx)DIDFT_GETINSTANCE(diIdentifier);

        switch (DIDFT_GETTYPE(diIdentifier))
        {
        case DIDFT_ABSAXIS:
            type = EInstanceType::InstanceTypeAxis;
            break;

        case DIDFT_PSHBUTTON:
            type = EInstanceType::InstanceTypeButton;
            break;

        case DIDFT_POV:
            type = EInstanceType::InstanceTypePov;
            break;
        }

        if ((int)type < 0 || idx >= NumInstancesOfType(type))
            return (TInstance)-1;

        return MakeInstanceIdentifier(type, idx);
    }

    // ---------

    TInstance Mapper::InstanceIdentifierFromDirectInputSpec(DWORD dwObj, DWORD dwHow)
    {
        TInstance instance = (TInstance)-1;

        // Select an instance based on the specifics provided by the application.
        // The methods called to get this instance also check for validity of the input and the current object state.
        // Only these methods for "dwHow" are supported, others return an invalid result.
        switch (dwHow)
        {
        case DIPH_BYOFFSET:
            instance = InstanceForOffset(dwObj);
            break;

        case DIPH_BYID:
            instance = InstanceIdentifierFromDirectInputIdentifier(dwObj);
            break;
        }

        return instance;
    }

    // ---------

    LONG Mapper::InvertAxisValue(LONG originalValue, LONG rangeMin, LONG rangeMax)
    {
        const LONG rangeCenter = (rangeMax + rangeMin) / 2;
        return (rangeCenter + (rangeCenter - originalValue));
    }

    // ---------

    void Mapper::MapInstanceAndOffset(TInstance instance, DWORD offset)
    {
        Message::OutputFormatted(Message::ESeverity::Debug, L"Mapping instance (type=%lld, index=%lld) to data format offset %d.", (int64_t)ExtractIdentifierInstanceType(instance), (int64_t)ExtractIdentifierInstanceIndex(instance), offset);
        instanceToOffset.insert({ instance, offset });
        offsetToInstance.insert({ offset, instance });
    }

    // ---------

    LONG Mapper::MapValueInRangeToRange(const LONG originalValue, const LONG originalMin, const LONG originalMax, const LONG newMin, const LONG newMax)
    {
        // Calculate the original value's position within the original range spread.
        const double originalSpread = (double)(originalMax - originalMin);
        const double originalFraction = (double)(originalValue - originalMin) / originalSpread;

        // Calculate the new range spread.
        const double newSpread = (double)(newMax - newMin);

        // Calculate and return the new scaled value.
        return (LONG)(originalFraction * newSpread) + newMin;
    }

    // ---------

    template <> void Mapper::PovToString<LPSTR>(unsigned int povNumber, LPSTR buf, const int bufcount)
    {
        sprintf_s(buf, bufcount, XIDI_POV_NAME_FORMAT, povNumber);
    }

    template <> void Mapper::PovToString<LPWSTR>(unsigned int povNumber, LPWSTR buf, const int bufcount)
    {
        swprintf_s(buf, bufcount, _CRT_WIDE(XIDI_POV_NAME_FORMAT), povNumber);
    }

    // ---------

    TInstance Mapper::SelectInstance(const EInstanceType instanceType, BOOL* instanceUsed, const TInstanceCount instanceCount, const TInstanceIdx instanceToSelect)
    {
        TInstance selectedInstance = (TInstance)-1;

        if ((instanceToSelect >= 0) && (instanceToSelect < instanceCount) && (FALSE == instanceUsed[instanceToSelect]))
        {
            instanceUsed[instanceToSelect] = TRUE;
            selectedInstance = Mapper::MakeInstanceIdentifier(instanceType, instanceToSelect);
        }

        return selectedInstance;
    }

    // ---------

    void Mapper::WriteAxisValueToApplicationDataStructure(const TInstance axisInstance, const LONG value, LPVOID appData)
    {
        // Verify that the application cares about the axis in question.
        if (0 == instanceToOffset.count(axisInstance)) return;

        // Write the axis value to the specified offset.
        WriteValueToApplicationOffset(ApplyAxisPropertiesToRawValue(axisInstance, value), instanceToOffset.find(axisInstance)->second, appData);
    }

    // ---------

    void Mapper::WriteButtonValueToApplicationDataStructure(const TInstance buttonInstance, const BYTE value, LPVOID appData)
    {
        // Verify that the application cares about the button in question.
        if (0 == instanceToOffset.count(buttonInstance)) return;

        // Write the button value to the specified offset.
        const DWORD offset = instanceToOffset.find(buttonInstance)->second;
        WriteValueToApplicationOffset((value ? (BYTE)0x80 : (BYTE)0x00), offset, appData);
    }

    // ---------

    void Mapper::WritePovValueToApplicationDataStructure(const TInstance povInstance, const LONG value, LPVOID appData)
    {
        // Verify that the application cares about the button in question.
        if (0 == instanceToOffset.count(povInstance)) return;

        // Write the button value to the specified offset.
        const DWORD offset = instanceToOffset.find(povInstance)->second;
        WriteValueToApplicationOffset(value, offset, appData);
    }

    // ---------

    void Mapper::WriteValueToApplicationOffset(const LONG value, const DWORD offset, LPVOID appData)
    {
        *((LONG*)(&((BYTE*)appData)[offset])) = value;
    }

    // ---------

    void Mapper::WriteValueToApplicationOffset(const BYTE value, const DWORD offset, LPVOID appData)
    {
        *((BYTE*)(&((BYTE*)appData)[offset])) = value;
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "Mapper.h" for documentation.

    template <typename DeviceObjectInstanceType> HRESULT Mapper::EnumerateMappedObjects(BOOL(FAR PASCAL* appCallback)(const DeviceObjectInstanceType*, LPVOID), LPVOID appCbParam, DWORD enumerationFlags)
    {
        // Obtain the number of objects of each type.
        const TInstanceCount numAxes = NumInstancesOfType(EInstanceType::InstanceTypeAxis);
        const TInstanceCount numPov = NumInstancesOfType(EInstanceType::InstanceTypePov);
        const TInstanceCount numButtons = NumInstancesOfType(EInstanceType::InstanceTypeButton);

        // Allocate a structure for repeated submission to the application, using the heap for security purposes.
        DeviceObjectInstanceType* objectDescriptor = new DeviceObjectInstanceType;

        // If requested, enumerate axes.
        if (DIDFT_ALL == enumerationFlags || enumerationFlags & DIDFT_AXIS)
        {
            for (TInstanceCount i = 0; i < numAxes; ++i)
            {
                // Fill the information structure to submit to the application.
                FillObjectInstanceInfo(objectDescriptor, EInstanceType::InstanceTypeAxis, (TInstanceIdx)i);

                // Submit the button to the application.
                BOOL appResponse = appCallback(objectDescriptor, appCbParam);

                // See if the application requested that the enumeration stop and, if so, honor that request
                switch (appResponse)
                {
                case DIENUM_CONTINUE:
                    break;
                case DIENUM_STOP:
                    delete objectDescriptor;
                    return DI_OK;
                default:
                    delete objectDescriptor;
                    return DIERR_INVALIDPARAM;
                }
            }
        }

        // If requested, enumerate POVs.
        if (DIDFT_ALL == enumerationFlags || enumerationFlags & DIDFT_POV)
        {
            for (TInstanceCount i = 0; i < numPov; ++i)
            {
                // Fill the information structure to submit to the application.
                FillObjectInstanceInfo(objectDescriptor, EInstanceType::InstanceTypePov, (TInstanceIdx)i);

                // Submit the button to the application.
                BOOL appResponse = appCallback(objectDescriptor, appCbParam);

                // See if the application requested that the enumeration stop and, if so, honor that request
                switch (appResponse)
                {
                case DIENUM_CONTINUE:
                    break;
                case DIENUM_STOP:
                    delete objectDescriptor;
                    return DI_OK;
                default:
                    delete objectDescriptor;
                    return DIERR_INVALIDPARAM;
                }
            }
        }

        // If requested, enumerate buttons.
        if (DIDFT_ALL == enumerationFlags || enumerationFlags & DIDFT_BUTTON)
        {
            for (TInstanceCount i = 0; i < numButtons; ++i)
            {
                // Fill the information structure to submit to the application.
                FillObjectInstanceInfo(objectDescriptor, EInstanceType::InstanceTypeButton, (TInstanceIdx)i);

                // Submit the button to the application.
                BOOL appResponse = appCallback(objectDescriptor, appCbParam);

                // See if the application requested that the enumeration stop and, if so, honor that request
                switch (appResponse)
                {
                case DIENUM_CONTINUE:
                    break;
                case DIENUM_STOP:
                    delete objectDescriptor;
                    return DI_OK;
                default:
                    delete objectDescriptor;
                    return DIERR_INVALIDPARAM;
                }
            }
        }

        delete objectDescriptor;
        return DI_OK;
    }

    template HRESULT Mapper::EnumerateMappedObjects(LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD);
    template HRESULT Mapper::EnumerateMappedObjects(LPDIENUMDEVICEOBJECTSCALLBACKW, LPVOID, DWORD);

    // ---------

    void Mapper::FillDeviceCapabilities(LPDIDEVCAPS lpDIDevCaps)
    {
        lpDIDevCaps->dwAxes = (DWORD)NumInstancesOfType(EInstanceType::InstanceTypeAxis);
        lpDIDevCaps->dwButtons = (DWORD)NumInstancesOfType(EInstanceType::InstanceTypeButton);
        lpDIDevCaps->dwPOVs = (DWORD)NumInstancesOfType(EInstanceType::InstanceTypePov);
    }

    // ---------

    template <typename DeviceObjectInstanceType> HRESULT Mapper::GetMappedObjectInfo(DeviceObjectInstanceType* pdidoi, DWORD dwObj, DWORD dwHow)
    {
        TInstance instance = InstanceIdentifierFromDirectInputSpec(dwObj, dwHow);

        // Verify that the structure size is corect, as required by the DirectInput API.
        if (pdidoi->dwSize != sizeof(*pdidoi)) return DIERR_INVALIDPARAM;

        // Check if an instance was identifiable above, if not then the object could not be located
        if (instance < 0)
            return DIERR_OBJECTNOTFOUND;

        // Fill the specified structure with information about the specified object.
        FillObjectInstanceInfo(pdidoi, ExtractIdentifierInstanceType(instance), ExtractIdentifierInstanceIndex(instance));

        return DI_OK;
    }

    template HRESULT Mapper::GetMappedObjectInfo(LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD);
    template HRESULT Mapper::GetMappedObjectInfo(LPDIDEVICEOBJECTINSTANCEW, DWORD, DWORD);

    // ---------

    HRESULT Mapper::GetMappedProperty(REFGUID rguidProp, LPDIPROPHEADER pdiph)
    {
        if (Message::WillOutputMessageOfSeverity(Message::ESeverity::Debug))
        {
            Message::Output(Message::ESeverity::Debug, L"Attempting to get a property.");
            DumpPropertyHeaderToLog(pdiph);
        }

        // Lazily initialize the axis properties (this is idempotent).
        InitializeAxisProperties();

        // First verify that this property is handled by this mapper.
        if (!IsPropertyHandledByMapper(rguidProp))
            return DIERR_UNSUPPORTED;

        // Verify the correct header size.
        if (pdiph->dwHeaderSize != sizeof(DIPROPHEADER))
            return DIERR_INVALIDPARAM;

        // Verify whole-device properties have the correct value for object identification.
        if (DIPH_DEVICE == pdiph->dwHow && 0 != pdiph->dwObj)
            return DIERR_INVALIDPARAM;

        // Branch based on the property requested.
        if (&DIPROP_AXISMODE == &rguidProp)
        {
            // Axis mode is easy: there is only one mode supported by the mapper.

            // Verify correct size. This one needs to be DIPROPDWORD.
            if (pdiph->dwSize != sizeof(DIPROPDWORD))
                return DIERR_INVALIDPARAM;

            // Provide output that the axis mode is absolute.
            ((LPDIPROPDWORD)pdiph)->dwData = DIPROPAXISMODE_ABS;
        }
        else if (&DIPROP_DEADZONE == &rguidProp || &DIPROP_SATURATION == &rguidProp || &DIPROP_RANGE == &rguidProp)
        {
            // Either deadzone, saturation, or range, and the logic is substantially similar for all of them.

            // Expected size depends on the actual property, so get that here.
            DWORD expectedSize = 0;

            if (&DIPROP_DEADZONE == &rguidProp || &DIPROP_SATURATION == &rguidProp)
                expectedSize = sizeof(DIPROPDWORD);
            else
                expectedSize = sizeof(DIPROPRANGE);

            // Verify correct size.
            if (pdiph->dwSize != expectedSize)
                return DIERR_INVALIDPARAM;

            // Verify that the target is specific, not the whole device.
            if (DIPH_DEVICE == pdiph->dwHow)
                return DIERR_UNSUPPORTED;

            // Attempt to locate the instance.
            TInstance instance = InstanceIdentifierFromDirectInputSpec(pdiph->dwObj, pdiph->dwHow);
            if (instance < 0)
                return DIERR_OBJECTNOTFOUND;

            // Verify that the instance target is an axis.
            if (EInstanceType::InstanceTypeAxis != ExtractIdentifierInstanceType(instance))
                return DIERR_UNSUPPORTED;

            // Provide the requested data, branching by specific property.
            if (&DIPROP_DEADZONE == &rguidProp)
                ((LPDIPROPDWORD)pdiph)->dwData = axisProperties[ExtractIdentifierInstanceIndex(instance)].deadzone;
            else if (&DIPROP_SATURATION == &rguidProp)
                ((LPDIPROPDWORD)pdiph)->dwData = axisProperties[ExtractIdentifierInstanceIndex(instance)].saturation;
            else
            {
                ((LPDIPROPRANGE)pdiph)->lMin = axisProperties[ExtractIdentifierInstanceIndex(instance)].rangeMin;
                ((LPDIPROPRANGE)pdiph)->lMax = axisProperties[ExtractIdentifierInstanceIndex(instance)].rangeMax;
            }
        }
        else
        {
            // All other properties are unsupported, but they should have been filtered out by checking if the mapper handles the property.
            return DIERR_UNSUPPORTED;
        }

        return DI_OK;
    }

    // ---------

    TInstance Mapper::InstanceForOffset(DWORD offset)
    {
        TInstance result = (TInstance)-1;

        if (IsApplicationDataFormatSet())
        {
            auto it = offsetToInstance.find(offset);

            if (offsetToInstance.end() != it)
                result = it->second;
        }

        return result;
    }

    // ---------

    BOOL Mapper::IsApplicationDataFormatSet(void)
    {
        return mapsValid;
    }

    // ---------

    BOOL Mapper::IsPropertyHandledByMapper(REFGUID guidProperty)
    {
        BOOL propertyHandled = FALSE;

        if (&guidProperty == &DIPROP_AXISMODE || &guidProperty == &DIPROP_DEADZONE || &guidProperty == &DIPROP_RANGE || &guidProperty == &DIPROP_SATURATION)
            propertyHandled = TRUE;

        return propertyHandled;
    }

    // ---------

    LONG Mapper::OffsetForInstance(TInstance instance)
    {
        LONG result = -1;

        if (IsApplicationDataFormatSet())
        {
            auto it = instanceToOffset.find(instance);

            if (instanceToOffset.end() != it)
                result = it->second;
        }

        return result;
    }

    // ---------

    LONG Mapper::OffsetForXInputControllerElement(EXInputControllerElement xElement)
    {
        LONG result = -1;

        TInstance xInstance = MapXInputElementToDirectInputInstance(xElement);
        if (xInstance >= 0)
            result = OffsetForInstance(xInstance);

        return result;
    }

    // ---------

    HRESULT Mapper::SetApplicationDataFormat(LPCDIDATAFORMAT lpdf)
    {
        if (Message::WillOutputMessageOfSeverity(Message::ESeverity::Debug))
        {
            Message::Output(Message::ESeverity::Debug, L"Attempting to set application's requested data format.");
            DumpDataFormatToLog(lpdf);
        }

        // Initialize the maps.
        ResetApplicationDataFormat();

        // Ensure the data packet size is a multiple of 4, as required by DirectInput.
        if (0 != (lpdf->dwDataSize % 4))
            return DIERR_INVALIDPARAM;

        // Ensure the data packet size is within bounds.
        if (kMaxDataPacketSize < lpdf->dwDataSize)
            return DIERR_INVALIDPARAM;

        // Save the application's data packet size.
        dataPacketSize = lpdf->dwDataSize;

        // Obtain the number of instances of each type in the mapping by asking the subclass.
        const TInstanceCount numButtons = NumInstancesOfType(EInstanceType::InstanceTypeButton);
        const TInstanceCount numAxes = NumInstancesOfType(EInstanceType::InstanceTypeAxis);
        const TInstanceCount numPov = NumInstancesOfType(EInstanceType::InstanceTypePov);

        // Track the next unused instance of each, essentially allowing a "dequeue" operation when the application does not specify a specific instance.
        TInstanceIdx nextUnusedButton = 0;
        TInstanceIdx nextUnusedAxis = 0;
        TInstanceIdx nextUnusedPov = 0;

        // Keep track of which instances were added to the mapping of each type as well as each offset.
        // It is an error to specify an instance multiple times, specify a non-existant instance, or specify multiple pieces of information at the same offset.
        BOOL* buttonUsed = new BOOL[numButtons];
        BOOL* axisUsed = new BOOL[numAxes];
        BOOL* povUsed = new BOOL[numPov];
        BOOL* offsetUsed = new BOOL[lpdf->dwDataSize];
        for (TInstanceCount i = 0; i < numButtons; ++i) buttonUsed[i] = FALSE;
        for (TInstanceCount i = 0; i < numAxes; ++i) axisUsed[i] = FALSE;
        for (TInstanceCount i = 0; i < numPov; ++i) povUsed[i] = FALSE;
        for (DWORD i = 0; i < lpdf->dwDataSize; ++i) offsetUsed[i] = FALSE;

        // Iterate over each of the object specifications provided by the application.
        for (DWORD i = 0; i < lpdf->dwNumObjs; ++i)
        {
            LPDIOBJECTDATAFORMAT dataFormat = &lpdf->rgodf[i];
            BOOL invalidParamsDetected = FALSE;

            // Extract information about the instance specified by the application.
            // If any instance is allowed, the specific instance is irrelevant.
            const BOOL allowAnyInstance = ((dataFormat->dwType & DIDFT_INSTANCEMASK) == DIDFT_ANYINSTANCE);
            const TInstanceIdx specificInstance = (TInstanceIdx)DIDFT_GETINSTANCE(dataFormat->dwType);

            if (dataFormat->dwType & DIDFT_ABSAXIS)
            {
                // Pick an axis

                // First check the offsets for overlap with something previously selected and for sufficient space in the data packet.
                if (!(dataFormat->dwOfs + SizeofInstance(EInstanceType::InstanceTypeAxis) <= lpdf->dwDataSize) || FALSE == CheckAndSetOffsets(&offsetUsed[dataFormat->dwOfs], SizeofInstance(EInstanceType::InstanceTypeAxis)))
                    invalidParamsDetected = TRUE;
                else
                {
                    if (nullptr == dataFormat->pguid)
                    {
                        // Any axis type allowed.

                        const TInstanceIdx instanceToSelect = allowAnyInstance ? nextUnusedAxis : specificInstance;
                        const TInstance selectedInstance = SelectInstance(EInstanceType::InstanceTypeAxis, axisUsed, numAxes, instanceToSelect);

                        if (selectedInstance >= 0)
                        {
                            // Instance was selected successfully, add a mapping.
                            MapInstanceAndOffset(selectedInstance, dataFormat->dwOfs);
                        }
                        else if (!allowAnyInstance)
                        {
                            // Instance was unable to be selected, and a specific instance was specified, so this is an error.
                            invalidParamsDetected = TRUE;
                        }
                        else
                        {
                            // Instance was unable to be selected, and any instance was allowed.
                            // This is okay, it just means the application data format has field for an axis that does not actually exist on the controller.
                            Message::OutputFormatted(Message::ESeverity::Debug, L"Any axis type allowed; mapping unused axis at offset %d.", dataFormat->dwOfs);
                            axisOffsetsUnused.insert(dataFormat->dwOfs);
                        }
                    }
                    else
                    {
                        // Specific axis type required.

                        if (allowAnyInstance)
                        {
                            // Any instance allowed, so find the first of this type that is unused, if any.
                            TInstanceIdx instanceIndex = 0;
                            TInstanceIdx axisIndex = AxisInstanceIndex(*dataFormat->pguid, instanceIndex++);
                            TInstance selectedInstance = SelectInstance(EInstanceType::InstanceTypeAxis, axisUsed, numAxes, axisIndex);

                            while (selectedInstance < 0 && axisIndex >= 0)
                            {
                                axisIndex = AxisInstanceIndex(*dataFormat->pguid, instanceIndex++);
                                selectedInstance = SelectInstance(EInstanceType::InstanceTypeAxis, axisUsed, numAxes, axisIndex);
                            }

                            if (selectedInstance >= 0)
                            {
                                // Unused instance found, create a mapping.
                                MapInstanceAndOffset(MakeInstanceIdentifier(EInstanceType::InstanceTypeAxis, axisIndex), dataFormat->dwOfs);
                            }
                            else
                            {
                                // No more unused instances of the requested axis type exist.
                                // This is okay, it just means the application data format has a field for an axis that does not actually exist on the controller.
                                Message::OutputFormatted(Message::ESeverity::Debug, L"Specific axis type required; mapping unused axis at offset %d.", dataFormat->dwOfs);
                                axisOffsetsUnused.insert(dataFormat->dwOfs);
                            }
                        }
                        else
                        {
                            // Specific instance required, so check if it is available and select it if so.
                            TInstanceIdx axisIndex = AxisInstanceIndex(*dataFormat->pguid, specificInstance);
                            TInstance axisInstance = SelectInstance(EInstanceType::InstanceTypeAxis, axisUsed, numAxes, axisIndex);

                            if (axisInstance >= 0)
                            {
                                // Instance was selected successfully, add a mapping.
                                MapInstanceAndOffset(axisInstance, dataFormat->dwOfs);
                            }
                            else
                            {
                                // Axis unavailable, this is an error.
                                invalidParamsDetected = TRUE;
                            }
                        }
                    }
                }
            }
            else if (dataFormat->dwType & DIDFT_PSHBUTTON)
            {
                // Pick a button.

                // First check the offsets for overlap with something previously selected and for sufficient space in the data packet.
                if (!(dataFormat->dwOfs + SizeofInstance(EInstanceType::InstanceTypeButton) <= lpdf->dwDataSize) || FALSE == CheckAndSetOffsets(&offsetUsed[dataFormat->dwOfs], SizeofInstance(EInstanceType::InstanceTypeButton)))
                    invalidParamsDetected = TRUE;
                else
                {
                    if (nullptr == dataFormat->pguid || GUID_Button == *dataFormat->pguid)
                    {
                        // Type unspecified or specified as a button.

                        const TInstanceIdx instanceToSelect = allowAnyInstance ? nextUnusedButton : specificInstance;
                        const TInstance selectedInstance = SelectInstance(EInstanceType::InstanceTypeButton, buttonUsed, numButtons, instanceToSelect);

                        if (selectedInstance > 0)
                        {
                            // Instance was selected successfully, add a mapping.
                            MapInstanceAndOffset(selectedInstance, dataFormat->dwOfs);
                        }
                        else if (!allowAnyInstance)
                        {
                            // Instance was unable to be selected, and a specific instance was specified, so this is an error.
                            invalidParamsDetected = TRUE;
                        }
                        else
                        {
                            // Instance was unable to be selected, and any instance was allowed.
                            // This is okay, it just means the application data format has field for a button that does not actually exist on the controller.
                            Message::OutputFormatted(Message::ESeverity::Debug, L"Mapping unused button at offset %d.", dataFormat->dwOfs);
                            buttonOffsetsUnused.insert(dataFormat->dwOfs);
                        }
                    }
                    else
                    {
                        // Type specified as a non-button, this is an error.
                        invalidParamsDetected = TRUE;
                    }
                }
            }
            else if (dataFormat->dwType & DIDFT_POV)
            {
                // Pick a POV

                // First check the offsets for overlap with something previously selected and for sufficient space in the data packet.
                if (!(dataFormat->dwOfs + SizeofInstance(EInstanceType::InstanceTypePov) <= lpdf->dwDataSize) || FALSE == CheckAndSetOffsets(&offsetUsed[dataFormat->dwOfs], SizeofInstance(EInstanceType::InstanceTypePov)))
                    invalidParamsDetected = TRUE;
                else
                {
                    if (nullptr == dataFormat->pguid || GUID_POV == *dataFormat->pguid)
                    {
                        // Type unspecified or specified as a POV.

                        const TInstanceIdx instanceToSelect = allowAnyInstance ? nextUnusedPov : specificInstance;
                        const TInstance selectedInstance = SelectInstance(EInstanceType::InstanceTypePov, povUsed, numPov, instanceToSelect);

                        if (selectedInstance > 0)
                        {
                            // Instance was selected successfully, add a mapping.
                            MapInstanceAndOffset(selectedInstance, dataFormat->dwOfs);
                        }
                        else if (!allowAnyInstance)
                        {
                            // Instance was unable to be selected, and a specific instance was specified, so this is an error.
                            invalidParamsDetected = TRUE;
                        }
                        else
                        {
                            // Instance was unable to be selected, and any instance was allowed.
                            // This is okay, it just means the application data format has field for a POV that does not actually exist on the controller.
                            Message::OutputFormatted(Message::ESeverity::Debug, L"Mapping unused POV at offset %d.", dataFormat->dwOfs);
                            povOffsetsUnused.insert(dataFormat->dwOfs);
                        }
                    }
                    else
                    {
                        // Type specified as a non-POV, this is an error.
                        invalidParamsDetected = TRUE;
                    }
                }
            }
            else
            {
                // An unknown data format element was specified. This is an error.
                invalidParamsDetected = TRUE;
            }

            // Bail in the event of an error.
            if (invalidParamsDetected)
            {
                delete[] buttonUsed;
                delete[] axisUsed;
                delete[] povUsed;
                delete[] offsetUsed;

                return DIERR_INVALIDPARAM;
            }

            // Increment all next-unused indices.
            while (TRUE == axisUsed[nextUnusedAxis] && nextUnusedAxis < numAxes) nextUnusedAxis += 1;
            while (TRUE == buttonUsed[nextUnusedButton] && nextUnusedButton < numButtons) nextUnusedButton += 1;
            while (TRUE == povUsed[nextUnusedPov] && nextUnusedPov < numPov) nextUnusedPov += 1;
        }

        delete[] buttonUsed;
        delete[] axisUsed;
        delete[] povUsed;
        delete[] offsetUsed;

        mapsValid = TRUE;

        return S_OK;
    }

    // ---------

    HRESULT Mapper::SetMappedProperty(REFGUID rguidProp, LPCDIPROPHEADER pdiph)
    {
        if (Message::WillOutputMessageOfSeverity(Message::ESeverity::Debug))
        {
            Message::Output(Message::ESeverity::Debug, L"Attempting to set a property.");
            DumpPropertyHeaderToLog(pdiph);
        }

        // Lazily initialize the axis properties (this is idempotent).
        InitializeAxisProperties();

        // First verify that this property is handled by this mapper.
        if (!IsPropertyHandledByMapper(rguidProp))
            return DIERR_UNSUPPORTED;

        // Verify the correct header size.
        if (pdiph->dwHeaderSize != sizeof(DIPROPHEADER))
            return DIERR_INVALIDPARAM;

        // Verify whole-device properties have the correct value for object identification.
        if (DIPH_DEVICE == pdiph->dwHow && 0 != pdiph->dwObj)
            return DIERR_INVALIDPARAM;

        // Branch based on the property requested.
        if (&DIPROP_AXISMODE == &rguidProp)
        {
            // Axis mode is easy: only absolute is supported.
            switch (((LPDIPROPDWORD)pdiph)->dwData)
            {
            case DIPROPAXISMODE_ABS:
                return DI_PROPNOEFFECT;
            default:
                return DIERR_UNSUPPORTED;
            }
        }
        else if (&DIPROP_DEADZONE == &rguidProp || &DIPROP_SATURATION == &rguidProp || &DIPROP_RANGE == &rguidProp)
        {
            // Either deadzone, saturation, or range, and the logic is substantially similar for all of them.

            // Expected size depends on the actual property, so get that here.
            DWORD expectedSize = 0;

            if (&DIPROP_DEADZONE == &rguidProp || &DIPROP_SATURATION == &rguidProp)
                expectedSize = sizeof(DIPROPDWORD);
            else
                expectedSize = sizeof(DIPROPRANGE);

            // Verify correct size.
            if (pdiph->dwSize != expectedSize)
                return DIERR_INVALIDPARAM;

            // Locate a range of instances to set based on the input specification.
            TInstanceIdx startInstance = 0;
            TInstanceIdx endInstance = 0;

            if (DIPH_DEVICE == pdiph->dwHow)
            {
                // Targetting the whole device, so start at index 0 and end at the highest axis index that exists.
                startInstance = 0;
                endInstance = NumInstancesOfType(EInstanceType::InstanceTypeAxis) - 1;

                // There should be axes on the device, but in case there are none return an error.
                if (endInstance < startInstance)
                    return DIERR_OBJECTNOTFOUND;
            }
            else
            {
                // Targetting a specific instance, so locate that instance.
                TInstance instance = InstanceIdentifierFromDirectInputSpec(pdiph->dwObj, pdiph->dwHow);
                if (instance < 0)
                    return DIERR_OBJECTNOTFOUND;

                // Verify that the instance target is an axis.
                if (EInstanceType::InstanceTypeAxis != ExtractIdentifierInstanceType(instance))
                    return DIERR_UNSUPPORTED;

                // Start and end at the specified instance.
                startInstance = ExtractIdentifierInstanceIndex(instance);
                endInstance = startInstance;
            }

            // Verify the provided data and, if valid, write it.
            if (&DIPROP_DEADZONE == &rguidProp)
            {
                DWORD newDeadzone = ((LPDIPROPDWORD)pdiph)->dwData;

                if (newDeadzone < kMinAxisDeadzoneSaturation || newDeadzone > kMaxAxisDeadzoneSaturation)
                    return DIERR_INVALIDPARAM;

                for (TInstanceIdx instance = startInstance; instance <= endInstance; ++instance)
                {
                    axisProperties[ExtractIdentifierInstanceIndex(instance)].deadzone = newDeadzone;
                }
            }
            else if (&DIPROP_SATURATION == &rguidProp)
            {
                DWORD newSaturation = ((LPDIPROPDWORD)pdiph)->dwData;

                if (newSaturation < kMinAxisDeadzoneSaturation || newSaturation > kMaxAxisDeadzoneSaturation)
                    return DIERR_INVALIDPARAM;

                for (TInstanceIdx instance = startInstance; instance <= endInstance; ++instance)
                {
                    axisProperties[ExtractIdentifierInstanceIndex(instance)].saturation = newSaturation;
                }
            }
            else
            {
                LONG newRangeMin = ((LPDIPROPRANGE)pdiph)->lMin;
                LONG newRangeMax = ((LPDIPROPRANGE)pdiph)->lMax;

                if (!(newRangeMin < newRangeMax))
                    return DIERR_INVALIDPARAM;

                for (TInstanceIdx instance = startInstance; instance <= endInstance; ++instance)
                {
                    axisProperties[ExtractIdentifierInstanceIndex(instance)].rangeMin = newRangeMin;
                    axisProperties[ExtractIdentifierInstanceIndex(instance)].rangeMax = newRangeMax;
                }
            }
        }
        else
        {
            // All other properties are unsupported, but they should have been filtered out by checking if the mapper handles the property.
            return DIERR_UNSUPPORTED;
        }

        return DI_OK;
    }

    // ---------

    void Mapper::ResetApplicationDataFormat(void)
    {
        instanceToOffset.clear();
        offsetToInstance.clear();
        axisOffsetsUnused.clear();
        buttonOffsetsUnused.clear();
        povOffsetsUnused.clear();

        mapsValid = FALSE;
    }

    // ---------

    HRESULT Mapper::WriteApplicationBufferedEvents(XInputController* xController, LPDIDEVICEOBJECTDATA appEventBuf, DWORD& eventCount, const BOOL peek)
    {
        xController->LockEventBuffer();

        // Initialize before writing application events.
        const DWORD maxAppEvents = eventCount;
        const DWORD numControllerEvents = xController->BufferedEventsCount();
        const BOOL eventBufferOverflowed = xController->IsEventBufferOverflowed();

        // Iterate over the controller events. Will break early if application event capacity is reached.
        eventCount = 0;

        for (DWORD i = 0; i < numControllerEvents && eventCount < maxAppEvents; ++i)
        {
            // Retrieve the next controller event.
            SControllerEvent xEvent;
            if (peek)
                xController->PeekBufferedEvent(&xEvent, i);
            else
                xController->PopBufferedEvent(&xEvent);

            // If not writing the event data anywhere, skip over the rest.
            if (nullptr != appEventBuf)
            {
                // Figure out the application instance that corresponds to the XInput controller element.
                const TInstance appEventInstance = MapXInputElementToDirectInputInstance(xEvent.controllerElement);
                const LONG appEventOffset = OffsetForXInputControllerElement(xEvent.controllerElement);

                // If the element is not part of the mapping, skip over the rest.
                if (appEventInstance >= 0 && appEventOffset >= 0)
                {
                    // Initialize and fill in known application event information.
                    ZeroMemory(&appEventBuf[eventCount], sizeof(appEventBuf[eventCount]));
                    appEventBuf[eventCount].dwSequence = xEvent.sequenceNumber;
                    appEventBuf[eventCount].dwTimeStamp = xEvent.timestamp;
                    appEventBuf[eventCount].dwOfs = appEventOffset;

                    // If triggers are treated as buttons, then the value of the trigger needs to be correctly formatted to be a button value.
                    if (EInstanceType::InstanceTypeButton == ExtractIdentifierInstanceType(appEventInstance) && (EXInputControllerElement::TriggerLT == xEvent.controllerElement || EXInputControllerElement::TriggerRT == xEvent.controllerElement))
                        xEvent.value = (xEvent.value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? (LONG)0x0080 : (LONG)0x0000);

                    // Value depends on the instance type.
                    if (EInstanceType::InstanceTypeAxis == ExtractIdentifierInstanceType(appEventInstance))
                    {
                        // If axis, value needs to be converted and range-adjusted, with proper consideration for deadzone and saturation.
                        // Functionality depends on the XInput controller element, since each has different input range.
                        switch (xEvent.controllerElement)
                        {
                        case EXInputControllerElement::StickLeftHorizontal:
                        case EXInputControllerElement::StickRightHorizontal:
                            // Horizontal sticks require no special treatment.
                            appEventBuf[eventCount].dwData = (DWORD)MapValueInRangeToRange(xEvent.value, XInputController::kStickRangeMin, XInputController::kStickRangeMax, axisProperties[ExtractIdentifierInstanceIndex(appEventInstance)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(appEventInstance)].rangeMax);
                            break;

                        case EXInputControllerElement::StickLeftVertical:
                        case EXInputControllerElement::StickRightVertical:
                            // Vertical sticks require inversion.
                            appEventBuf[eventCount].dwData = (DWORD)MapValueInRangeToRange(InvertAxisValue(xEvent.value, XInputController::kStickRangeMin, XInputController::kStickRangeMax), XInputController::kStickRangeMin, XInputController::kStickRangeMax, axisProperties[ExtractIdentifierInstanceIndex(appEventInstance)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(appEventInstance)].rangeMax);
                            break;

                        case EXInputControllerElement::TriggerLT:
                        case EXInputControllerElement::TriggerRT:
                            // Triggers require special handling because they might share an axis.
                            if (MapXInputElementToDirectInputInstance(EXInputControllerElement::TriggerLT) >= 0 && MapXInputElementToDirectInputInstance(EXInputControllerElement::TriggerLT) == MapXInputElementToDirectInputInstance(EXInputControllerElement::TriggerRT))
                            {
                                // Shared axis, so figure out the separate contributions from each trigger and compute a new value.
                                if (EXInputControllerElement::TriggerLT == xEvent.controllerElement)
                                    cachedValueXInputLT = xEvent.value;
                                else
                                    cachedValueXInputRT = xEvent.value;

                                // Figure out the direction of each trigger on the shared axis.
                                LONG leftTriggerMultiplier = XInputTriggerSharedAxisDirection(EXInputControllerElement::TriggerLT);
                                if (0 > leftTriggerMultiplier)
                                    leftTriggerMultiplier = -1;
                                else if (0 < leftTriggerMultiplier)
                                    leftTriggerMultiplier = 1;
                                else
                                {
                                    // It is an error for the direction of a trigger on the shared axis to be 0.
                                    xController->UnlockEventBuffer();
                                    return DIERR_GENERIC;
                                }

                                // Compute the new value of the shared axis.
                                appEventBuf[eventCount].dwData = (DWORD)((leftTriggerMultiplier * cachedValueXInputLT) + (leftTriggerMultiplier * -1 * cachedValueXInputRT));
                                appEventBuf[eventCount].dwData = MapValueInRangeToRange((LONG)appEventBuf[eventCount].dwData, XInputController::kTriggerRangeMax * -1, XInputController::kTriggerRangeMax, axisProperties[ExtractIdentifierInstanceIndex(appEventInstance)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(appEventInstance)].rangeMax);
                            }
                            else
                            {
                                // Separate axes, so just map as usual.
                                appEventBuf[eventCount].dwData = (DWORD)MapValueInRangeToRange(xEvent.value, XInputController::kTriggerRangeMin, XInputController::kTriggerRangeMax, axisProperties[ExtractIdentifierInstanceIndex(appEventInstance)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(appEventInstance)].rangeMax);
                            }
                            break;

                        default:
                            // It is an error for an axis to be mapped to any other XInput controller element.
                            xController->UnlockEventBuffer();
                            return DIERR_GENERIC;
                        }

                        // Apply range and deadzone.
                        appEventBuf[eventCount].dwData = (DWORD)ApplyAxisPropertiesToRawValue(appEventInstance, (LONG)appEventBuf[eventCount].dwData);
                    }
                    else
                    {
                        // If button or POV, value is already in DirectInput format so just copy directly.
                        appEventBuf[eventCount].dwData = (DWORD)xEvent.value;
                    }

                    // Increment the number of events written to the application buffer.
                    eventCount += 1;
                }
            }
        }

        xController->UnlockEventBuffer();

        return (eventBufferOverflowed ? DI_BUFFEROVERFLOW : DI_OK);
    }

    // ---------

    HRESULT Mapper::WriteApplicationControllerState(const XINPUT_GAMEPAD& xState, LPVOID appDataBuf, DWORD appDataSize)
    {
        // Lazily initialize the axis properties (this is idempotent).
        InitializeAxisProperties();

        // First verify sufficient buffer space.
        if (appDataSize < dataPacketSize)
            return DIERR_INVALIDPARAM;

        // Keep track of instances already mapped, for error checking.
        std::unordered_set<TInstance> mappedInstances;

        // Initialize the application structure. Everything not explicitly written will return 0.
        ZeroMemory(appDataBuf, appDataSize);

        // Triggers are handled differently, so handle them first as a special case.
        {
            TInstance instanceLT = MapXInputElementToDirectInputInstance(EXInputControllerElement::TriggerLT);
            TInstance instanceRT = MapXInputElementToDirectInputInstance(EXInputControllerElement::TriggerRT);

            // Set aside the current state of the triggers into the cache.
            cachedValueXInputLT = (LONG)xState.bLeftTrigger;
            cachedValueXInputRT = (LONG)xState.bRightTrigger;

            if (instanceLT >= 0 && instanceRT >= 0 && instanceLT == instanceRT)
            {
                // LT and RT are part of the mapping and share an instance.

                // It is an error for the triggers to share a non-axis controller element.
                if (EInstanceType::InstanceTypeAxis != ExtractIdentifierInstanceType(instanceLT))
                    return DIERR_GENERIC;

                // Verify the axis is in bounds.
                if (ExtractIdentifierInstanceIndex(instanceLT) >= NumInstancesOfType(EInstanceType::InstanceTypeAxis))
                    return DIERR_GENERIC;

                // For sharing an axis, figure out which trigger contributes to which direction.
                // This function must not return 0, otherwise there is an error.
                LONG leftTriggerMultiplier = XInputTriggerSharedAxisDirection(EXInputControllerElement::TriggerLT);
                if (0 > leftTriggerMultiplier)
                    leftTriggerMultiplier = -1;
                else if (0 < leftTriggerMultiplier)
                    leftTriggerMultiplier = 1;
                else
                    return DIERR_GENERIC;

                // Compute the axis value for the shared axis.
                LONG triggerSharedAxisValue = (leftTriggerMultiplier * (LONG)xState.bLeftTrigger) + (leftTriggerMultiplier * -1 * (LONG)xState.bRightTrigger);
                triggerSharedAxisValue = MapValueInRangeToRange(triggerSharedAxisValue, XInputController::kTriggerRangeMax * -1, XInputController::kTriggerRangeMax, axisProperties[ExtractIdentifierInstanceIndex(instanceLT)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(instanceLT)].rangeMax);

                // Add the shared axis to the set.
                mappedInstances.insert(instanceLT);

                // Write the shared axis value to the application data structure.
                WriteAxisValueToApplicationDataStructure(instanceLT, triggerSharedAxisValue, appDataBuf);
            }
            else
            {
                // LT and RT need to be handled separately, so handle them like any other controller elements.
                // Unlike other elements these can be mapped to buttons or axes.

                if (instanceLT >= 0)
                {
                    if (EInstanceType::InstanceTypeAxis == ExtractIdentifierInstanceType(instanceLT))
                    {
                        // Verify the axis is in bounds.
                        if (ExtractIdentifierInstanceIndex(instanceLT) >= NumInstancesOfType(EInstanceType::InstanceTypeAxis))
                            return DIERR_GENERIC;

                        // Compute the axis value.
                        LONG triggerAxisValue = MapValueInRangeToRange((LONG)xState.bLeftTrigger, XInputController::kTriggerRangeMin, XInputController::kTriggerRangeMax, axisProperties[ExtractIdentifierInstanceIndex(instanceLT)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(instanceLT)].rangeMax);

                        // Add the axis to the set.
                        mappedInstances.insert(instanceLT);

                        // Write the axis value to the application data structure.
                        WriteAxisValueToApplicationDataStructure(instanceLT, triggerAxisValue, appDataBuf);
                    }
                    else if (EInstanceType::InstanceTypeButton == ExtractIdentifierInstanceType(instanceLT))
                    {
                        // Verify the button is in bounds.
                        if (ExtractIdentifierInstanceIndex(instanceLT) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                            return DIERR_GENERIC;

                        // Compute the button value.
                        BYTE triggerButtonValue = (xState.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD);

                        // Add the button to the set.
                        mappedInstances.insert(instanceLT);

                        // Write the button value to the application data structure.
                        WriteButtonValueToApplicationDataStructure(instanceLT, triggerButtonValue, appDataBuf);
                    }
                    else
                        return DIERR_GENERIC;
                }

                if (instanceRT >= 0)
                {
                    if (EInstanceType::InstanceTypeAxis == ExtractIdentifierInstanceType(instanceRT))
                    {
                        // Verify the axis is in bounds.
                        if (ExtractIdentifierInstanceIndex(instanceRT) >= NumInstancesOfType(EInstanceType::InstanceTypeAxis))
                            return DIERR_GENERIC;

                        // Compute the axis value.
                        LONG triggerAxisValue = MapValueInRangeToRange((LONG)xState.bRightTrigger, XInputController::kTriggerRangeMin, XInputController::kTriggerRangeMax, axisProperties[ExtractIdentifierInstanceIndex(instanceRT)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(instanceRT)].rangeMax);

                        // Add the axis to the set.
                        mappedInstances.insert(instanceRT);

                        // Write the axis value to the application data structure.
                        WriteAxisValueToApplicationDataStructure(instanceRT, triggerAxisValue, appDataBuf);
                    }
                    else if (EInstanceType::InstanceTypeButton == ExtractIdentifierInstanceType(instanceRT))
                    {
                        // Verify the button is in bounds.
                        if (ExtractIdentifierInstanceIndex(instanceRT) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                            return DIERR_GENERIC;

                        // Compute the button value.
                        BYTE triggerButtonValue = (xState.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD);

                        // Add the button to the set.
                        mappedInstances.insert(instanceRT);

                        // Write the button value to the application data structure.
                        WriteButtonValueToApplicationDataStructure(instanceRT, triggerButtonValue, appDataBuf);
                    }
                    else
                        return DIERR_GENERIC;
                }
            }
        }

        // Left and right analog sticks
        {
            TInstance instanceAxis;

            // Left stick horizontal
            instanceAxis = MapXInputElementToDirectInputInstance(EXInputControllerElement::StickLeftHorizontal);
            if (instanceAxis >= 0)
            {
                if (EInstanceType::InstanceTypeAxis != ExtractIdentifierInstanceType(instanceAxis))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceAxis) >= NumInstancesOfType(EInstanceType::InstanceTypeAxis))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceAxis))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceAxis);

                LONG axisValue = MapValueInRangeToRange((LONG)xState.sThumbLX, XInputController::kStickRangeMin, XInputController::kStickRangeMax, axisProperties[ExtractIdentifierInstanceIndex(instanceAxis)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(instanceAxis)].rangeMax);
                WriteAxisValueToApplicationDataStructure(instanceAxis, axisValue, appDataBuf);
            }

            // Left stick vertical
            instanceAxis = MapXInputElementToDirectInputInstance(EXInputControllerElement::StickLeftVertical);
            if (instanceAxis >= 0)
            {
                if (EInstanceType::InstanceTypeAxis != ExtractIdentifierInstanceType(instanceAxis))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceAxis) >= NumInstancesOfType(EInstanceType::InstanceTypeAxis))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceAxis))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceAxis);

                LONG axisValue = MapValueInRangeToRange(InvertAxisValue((LONG)xState.sThumbLY, XInputController::kStickRangeMin, XInputController::kStickRangeMax), XInputController::kStickRangeMin, XInputController::kStickRangeMax, axisProperties[ExtractIdentifierInstanceIndex(instanceAxis)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(instanceAxis)].rangeMax);
                WriteAxisValueToApplicationDataStructure(instanceAxis, axisValue, appDataBuf);
            }

            // Right stick horizontal
            instanceAxis = MapXInputElementToDirectInputInstance(EXInputControllerElement::StickRightHorizontal);
            if (instanceAxis >= 0)
            {
                if (EInstanceType::InstanceTypeAxis != ExtractIdentifierInstanceType(instanceAxis))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceAxis) >= NumInstancesOfType(EInstanceType::InstanceTypeAxis))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceAxis))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceAxis);

                LONG axisValue = MapValueInRangeToRange((LONG)xState.sThumbRX, XInputController::kStickRangeMin, XInputController::kStickRangeMax, axisProperties[ExtractIdentifierInstanceIndex(instanceAxis)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(instanceAxis)].rangeMax);
                WriteAxisValueToApplicationDataStructure(instanceAxis, axisValue, appDataBuf);
            }

            // Right stick vertical
            instanceAxis = MapXInputElementToDirectInputInstance(EXInputControllerElement::StickRightVertical);
            if (instanceAxis >= 0)
            {
                if (EInstanceType::InstanceTypeAxis != ExtractIdentifierInstanceType(instanceAxis))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceAxis) >= NumInstancesOfType(EInstanceType::InstanceTypeAxis))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceAxis))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceAxis);

                LONG axisValue = MapValueInRangeToRange(InvertAxisValue((LONG)xState.sThumbRY, XInputController::kStickRangeMin, XInputController::kStickRangeMax), XInputController::kStickRangeMin, XInputController::kStickRangeMax, axisProperties[ExtractIdentifierInstanceIndex(instanceAxis)].rangeMin, axisProperties[ExtractIdentifierInstanceIndex(instanceAxis)].rangeMax);
                WriteAxisValueToApplicationDataStructure(instanceAxis, axisValue, appDataBuf);
            }
        }

        // Dpad
        {
            TInstance instanceDpad = MapXInputElementToDirectInputInstance(EXInputControllerElement::Dpad);
            if (instanceDpad >= 0)
            {
                if (EInstanceType::InstanceTypePov != ExtractIdentifierInstanceType(instanceDpad))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceDpad) >= NumInstancesOfType(EInstanceType::InstanceTypePov))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceDpad))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceDpad);
                WritePovValueToApplicationDataStructure(instanceDpad, XInputController::DirectInputPovStateFromXInputButtonState(xState.wButtons), appDataBuf);
            }
        }

        // Buttons A, B, X, Y, LB, RB, Back, Start, Left stick, and Right stick
        {
            TInstance instanceButton;

            // A
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonA);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_A ? 1 : 0), appDataBuf);
            }

            // B
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonB);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_B ? 1 : 0), appDataBuf);
            }

            // X
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonX);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_X ? 1 : 0), appDataBuf);
            }

            // Y
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonY);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_Y ? 1 : 0), appDataBuf);
            }

            // LB
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonLB);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER ? 1 : 0), appDataBuf);
            }

            // RB
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonRB);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER ? 1 : 0), appDataBuf);
            }

            // Back
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonBack);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_BACK ? 1 : 0), appDataBuf);
            }

            // Start
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonStart);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_START ? 1 : 0), appDataBuf);
            }

            // Left stick
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonLeftStick);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_LEFT_THUMB ? 1 : 0), appDataBuf);
            }

            // Right stick
            instanceButton = MapXInputElementToDirectInputInstance(EXInputControllerElement::ButtonRightStick);
            if (instanceButton >= 0)
            {
                if (EInstanceType::InstanceTypeButton != ExtractIdentifierInstanceType(instanceButton))
                    return DIERR_GENERIC;
                if (ExtractIdentifierInstanceIndex(instanceButton) >= NumInstancesOfType(EInstanceType::InstanceTypeButton))
                    return DIERR_GENERIC;
                if (0 != mappedInstances.count(instanceButton))
                    return DIERR_GENERIC;

                mappedInstances.insert(instanceButton);
                WriteButtonValueToApplicationDataStructure(instanceButton, (xState.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB ? 1 : 0), appDataBuf);
            }
        }

        // Set to "centered" any other POVs in the application's data format.
        if (povOffsetsUnused.size() > 0)
        {
            const LONG povCenteredValue = XInputController::DirectInputPovStateFromXInputButtonState(0);

            for (auto it = povOffsetsUnused.begin(); it != povOffsetsUnused.end(); ++it)
                WriteValueToApplicationOffset(povCenteredValue, *it, appDataBuf);
        }

        return DI_OK;
    }


    // -------- CONCRETE INSTANCE METHODS ---------------------------------- //
    // See "Mapper.h" for documentation.

    LONG Mapper::XInputTriggerSharedAxisDirection(EXInputControllerElement trigger)
    {
        if (EXInputControllerElement::TriggerLT == trigger)
            return 1;

        return -1;
    }
}