/*****************************************************************************
 * XinputControllerDirectInput
 *      Hook and helper for older DirectInput games.
 *      Fixes issues associated with certain Xinput-based controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016
 *****************************************************************************
 * WrapperIDirectInputDevice8.cpp
 *      Implementation of the wrapper class for IDirectInputDevice8.
 *****************************************************************************/

#include "WrapperIDirectInputDevice8.h"

using namespace XinputControllerDirectInput;


// -------- TYPE DEFINITIONS ----------------------------------------------- //

// Contains all information required to intercept callbacks to EnumObjects.
struct SEnumObjectsCallbackInfo
{
    WrapperIDirectInputDevice8* instance;
    LPDIENUMDEVICEOBJECTSCALLBACK lpCallback;
    LPVOID pvRef;
};


// -------- CONSTRUCTION AND DESTRUCTION ----------------------------------- //
// See "WrapperIDirectInputDevice8.h" for documentation.

WrapperIDirectInputDevice8::WrapperIDirectInputDevice8(IDirectInputDevice8* underlyingDIObject) : underlyingDIObject(underlyingDIObject) {}


// -------- METHODS: IUnknown ---------------------------------------------- //
// See IUnknown documentation for more information.

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::QueryInterface(REFIID riid, LPVOID* ppvObj)
{
    HRESULT result = S_OK;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectInputDevice8))
    {
        AddRef();
        *ppvObj = this;
    }
    else
    {
        result = underlyingDIObject->QueryInterface(riid, ppvObj);
    }

    return result;
}

// ---------

ULONG STDMETHODCALLTYPE WrapperIDirectInputDevice8::AddRef(void)
{
    return underlyingDIObject->AddRef();
}

// ---------

ULONG STDMETHODCALLTYPE WrapperIDirectInputDevice8::Release(void)
{
    ULONG numRemainingRefs = underlyingDIObject->Release();

    if (0 == numRemainingRefs)
        delete this;

    return numRemainingRefs;
}


// -------- METHODS: IDirectInputDevice8 ----------------------------------- //
// See DirectInput documentation for more information.

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::Acquire(void)
{
    return underlyingDIObject->Acquire();
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::BuildActionMap(LPDIACTIONFORMAT lpdiaf, LPCTSTR lpszUserName, DWORD dwFlags)
{
    return underlyingDIObject->BuildActionMap(lpdiaf, lpszUserName, dwFlags);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::CreateEffect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT* ppdeff, LPUNKNOWN punkOuter)
{
    return underlyingDIObject->CreateEffect(rguid, lpeff, ppdeff, punkOuter);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl)
{
    return underlyingDIObject->EnumCreatedEffectObjects(lpCallback, pvRef, fl);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::EnumEffects(LPDIENUMEFFECTSCALLBACK lpCallback, LPVOID pvRef, DWORD dwEffType)
{
    return underlyingDIObject->EnumEffects(lpCallback, pvRef, dwEffType);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::EnumEffectsInFile(LPCTSTR lptszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags)
{
    return underlyingDIObject->EnumEffectsInFile(lptszFileName, pec, pvRef, dwFlags);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD dwFlags)
{
    SEnumObjectsCallbackInfo callbackInfo;
    callbackInfo.instance = this;
    callbackInfo.lpCallback = lpCallback;
    callbackInfo.pvRef = pvRef;
    
    return underlyingDIObject->EnumObjects(&WrapperIDirectInputDevice8::CallbackEnumObjects, (LPVOID)&callbackInfo, dwFlags);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::Escape(LPDIEFFESCAPE pesc)
{
    return underlyingDIObject->Escape(pesc);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetCapabilities(LPDIDEVCAPS lpDIDevCaps)
{
    return underlyingDIObject->GetCapabilities(lpDIDevCaps);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags)
{
    return underlyingDIObject->GetDeviceData(cbObjectData, rgdod, pdwInOut, dwFlags);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetDeviceInfo(LPDIDEVICEINSTANCE pdidi)
{
    return underlyingDIObject->GetDeviceInfo(pdidi);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetDeviceState(DWORD cbData, LPVOID lpvData)
{
    return underlyingDIObject->GetDeviceState(cbData, lpvData);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetEffectInfo(LPDIEFFECTINFO pdei, REFGUID rguid)
{
    return underlyingDIObject->GetEffectInfo(pdei, rguid);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetForceFeedbackState(LPDWORD pdwOut)
{
    return underlyingDIObject->GetForceFeedbackState(pdwOut);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetImageInfo(LPDIDEVICEIMAGEINFOHEADER lpdiDevImageInfoHeader)
{
    return underlyingDIObject->GetImageInfo(lpdiDevImageInfoHeader);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetObjectInfo(LPDIDEVICEOBJECTINSTANCE pdidoi, DWORD dwObj, DWORD dwHow)
{
    return underlyingDIObject->GetObjectInfo(pdidoi, dwObj, dwHow);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::GetProperty(REFGUID rguidProp, LPDIPROPHEADER pdiph)
{
    return underlyingDIObject->GetProperty(rguidProp, pdiph);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::Initialize(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid)
{
    return underlyingDIObject->Initialize(hinst, dwVersion, rguid);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::Poll(void)
{
    return underlyingDIObject->Poll();
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::RunControlPanel(HWND hwndOwner, DWORD dwFlags)
{
    return underlyingDIObject->RunControlPanel(hwndOwner, dwFlags);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::SendDeviceData(DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl)
{
    return underlyingDIObject->SendDeviceData(cbObjectData, rgdod, pdwInOut, fl);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::SendForceFeedbackCommand(DWORD dwFlags)
{
    return underlyingDIObject->SendForceFeedbackCommand(dwFlags);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::SetActionMap(LPDIACTIONFORMAT lpdiActionFormat, LPCTSTR lptszUserName, DWORD dwFlags)
{
    return underlyingDIObject->SetActionMap(lpdiActionFormat, lptszUserName, dwFlags);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::SetCooperativeLevel(HWND hwnd, DWORD dwFlags)
{
    return underlyingDIObject->SetCooperativeLevel(hwnd, dwFlags);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::SetDataFormat(LPCDIDATAFORMAT lpdf)
{
    return underlyingDIObject->SetDataFormat(lpdf);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::SetEventNotification(HANDLE hEvent)
{
    return underlyingDIObject->SetEventNotification(hEvent);
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::SetProperty(REFGUID rguidProp, LPCDIPROPHEADER pdiph)
{
    // Demo: compare the address against the predefined property values
    const size_t propguid = (size_t)&rguidProp;

    // Demo: header is at the start of the type-specific property structure, so convert to the correct type depending on the property in question
    const DIPROPPOINTER* proppointer = (const DIPROPPOINTER*)pdiph;
    const DIPROPDWORD* propdword = (const DIPROPDWORD*)pdiph;
    const DIPROPSTRING* propstring = (const DIPROPSTRING*)pdiph;
    const DIPROPRANGE* proprange = (const DIPROPRANGE*)pdiph;
    
    // Demo: if the property is not of interest, or once the property is intercepted and mapped, forward to the underlying object
    HRESULT result = underlyingDIObject->SetProperty(rguidProp, pdiph);

    return result;
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::Unacquire(void)
{
    return underlyingDIObject->Unacquire();
}

// ---------

HRESULT STDMETHODCALLTYPE WrapperIDirectInputDevice8::WriteEffectToFile(LPCTSTR lptszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags)
{
    return underlyingDIObject->WriteEffectToFile(lptszFileName, dwEntries, rgDiFileEft, dwFlags);
}


// -------- CALLBACKS: IDirectInputDevice8 --------------------------------- //
// See "WrapperIDirectInputDevice8.h" for documentation.

BOOL STDMETHODCALLTYPE WrapperIDirectInputDevice8::CallbackEnumObjects(LPCDIDEVICEOBJECTINSTANCE lpddoi, LPVOID pvRef)
{
    SEnumObjectsCallbackInfo* callbackInfo = (SEnumObjectsCallbackInfo*)pvRef;
    
    return callbackInfo->lpCallback(lpddoi, callbackInfo->pvRef);
}
