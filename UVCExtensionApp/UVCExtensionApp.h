#pragma once

#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <vector>
#include <string>
#include <ks.h>
#include <ksproxy.h>
#include <vidcap.h>

//Macro to check the HR result
#define CHECK_HR_RESULT(hr, msg, ...) if (hr != S_OK) {printf("info: Function: %s, %s failed, Error code: 0x%.2x \n", __FUNCTION__, msg, hr, __VA_ARGS__); goto done; }

//Templates for the App
template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

//Function to get UVC video devices
extern HRESULT GetVideoDevices();

//Function to get device friendly name
extern HRESULT GetVideoDeviceFriendlyNames(int deviceIndex);

//Function to initialize video device
extern HRESULT InitVideoDevice(int deviceIndex);

//Function to set/get parameters of UVC extension unit
extern HRESULT SetGetExtensionUnit(GUID xuGuid, ULONG xuPropertyId, ULONG flags, void *data, int len, ULONG *readCount);
