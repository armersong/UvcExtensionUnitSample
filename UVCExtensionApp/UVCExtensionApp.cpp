// UVCExtensionApp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <vector>
#include <string>
#include <ks.h>
#include <ksproxy.h>
#include <vidcap.h>
#include <ksmedia.h>
#include "UVCExtensionApp.h"

static const GUID smartCamera =
{ 0xA29E7641, 0xDE04, 0x47E3,{ 0x8B, 0x2B, 0xF4, 0x34, 0x1A, 0xFF, 0x00, 0x3B } };	//unitId = 10, noteId = 2
//{ 0x9A1E7291, 0x6843, 0x4683, { 0x6D, 0x92, 0x39, 0xBC, 0x79, 0x06, 0xEE, 0x49 } };	//unitId = 17, noteId = 3


//Media foundation and DSHOW specific structures, class and variables
IMFMediaSource *pVideoSource = NULL;
IMFAttributes *pVideoConfig = NULL;
IMFActivate **ppVideoDevices = NULL;
IMFSourceReader *pVideoReader = NULL;

//Other variables
UINT32 noOfVideoDevices = 0;
WCHAR *szFriendlyName = NULL;

int main()
{
	HRESULT hr = S_OK;
	CHAR enteredStr[MAX_PATH], videoDevName[20][MAX_PATH];
	UINT32 selectedVal = 0xFFFFFFFF;
	ULONG flags, readCount;
	size_t returnValue;
	BYTE an75779FwVer[5] = { 2, 2, 12, 20, 18 }; //Write some random value

	//Get all video devices
	GetVideoDevices();

	printf("Video Devices connected:\n");
//	const char* selectedCameraName = "Integrated Camera";
	const char* selectedCameraName = "Smart Camera";
	for (UINT32 i = 0; i < noOfVideoDevices; i++)
	{
		//Get the device names
		GetVideoDeviceFriendlyNames(i);
		wcstombs_s(&returnValue, videoDevName[i], MAX_PATH, szFriendlyName, MAX_PATH);
		printf("%d: %s\n", i, videoDevName[i]);

		//Store the App note firmware (Whose name is *FX3*) device index  
		if (!(strcmp(videoDevName[i], selectedCameraName)))
			selectedVal = i;
	}

	//Specific to UVC extension unit in AN75779 appnote firmware
	if (selectedVal != 0xFFFFFFFF)
	{
		printf("\nFound \"%s\" device\n", selectedCameraName);

		//Initialize the selected device
		InitVideoDevice(selectedVal);

		printf("\nSelect\n1. To Set Firmware Version \n2. To Get Firmware version\nAnd press Enter\n");
		fgets(enteredStr, MAX_PATH, stdin);
		fflush(stdin);
		selectedVal = atoi(enteredStr);

		if (selectedVal == 1)
			flags = KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY;
		else
			flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;

		printf("\nTrying to invoke UVC extension unit...\n");

		if (!SetGetExtensionUnit(smartCamera, 0x1, flags, (void*)an75779FwVer, 5, &readCount))
		{
			printf("Found UVC extension unit\n");

			if (flags == (KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY))
			{
				printf("\nAN75779 Firmware Version Set to %d.%d (Major.Minor), Build Date: %d/%d/%d (MM/DD/YY)\n\n", an75779FwVer[0], an75779FwVer[1],
					an75779FwVer[2], an75779FwVer[3], an75779FwVer[4]);
			}
			else
			{
				printf("\nCurrent AN75779 Firmware Version is %d.%d (Major.Minor), Build Date: %d/%d/%d (MM/DD/YY)\n\n", an75779FwVer[0], an75779FwVer[1],
					an75779FwVer[2], an75779FwVer[3], an75779FwVer[4]);
			}
		}
	}
	else
	{
		printf("\nDid not find \"FX3\" device (AN75779 firmware)\n\n");
	}

	//Release all the video device resources
	for (UINT32 i = 0; i < noOfVideoDevices; i++)
	{
		SafeRelease(&ppVideoDevices[i]);
	}
	CoTaskMemFree(ppVideoDevices);
	SafeRelease(&pVideoConfig);
	SafeRelease(&pVideoSource);
	CoTaskMemFree(szFriendlyName);

	printf("\nExiting App in 5 sec...");
	Sleep(5000);

    return 0;
}

//Function to get UVC video devices
HRESULT GetVideoDevices()
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	MFStartup(MF_VERSION);

	// Create an attribute store to specify the enumeration parameters.
	HRESULT hr = MFCreateAttributes(&pVideoConfig, 1);
	CHECK_HR_RESULT(hr, "Create attribute store");

	// Source type: video capture devices
	hr = pVideoConfig->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	);
	CHECK_HR_RESULT(hr, "Video capture device SetGUID");

	// Enumerate devices.
	hr = MFEnumDeviceSources(pVideoConfig, &ppVideoDevices, &noOfVideoDevices);
	CHECK_HR_RESULT(hr, "Device enumeration");

done:
	return hr;
}

//Function to get device friendly name
HRESULT GetVideoDeviceFriendlyNames(int deviceIndex)
{
	// Get the the device friendly name.
	UINT32 cchName;

	HRESULT hr = ppVideoDevices[deviceIndex]->GetAllocatedString(
		MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
		&szFriendlyName, &cchName);
	CHECK_HR_RESULT(hr, "Get video device friendly name");

done:
	return hr;
}

//Function to initialize video device
HRESULT InitVideoDevice(int deviceIndex)
{
	HRESULT hr = ppVideoDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&pVideoSource));
	CHECK_HR_RESULT(hr, "Activating video device");

	// Create a source reader.
	hr = MFCreateSourceReaderFromMediaSource(pVideoSource, pVideoConfig, &pVideoReader);
	CHECK_HR_RESULT(hr, "Creating video source reader");

done:
	return hr;
}

HRESULT FindExtensionNode(IKsTopologyInfo* pKsTopologyInfo, DWORD* node)
{
	HRESULT hr = E_FAIL;
	DWORD dwNumNodes = 0;
	GUID guidNodeType;
	IKsControl* pKsControl = NULL;
	ULONG ulBytesReturned = 0;
	KSP_NODE ExtensionProp;

	if (!pKsTopologyInfo || !node)
		return E_POINTER;

	// Retrieve the number of nodes in the filter
	hr = pKsTopologyInfo->get_NumNodes(&dwNumNodes);
	if (!SUCCEEDED(hr))
		return hr;
	if (dwNumNodes == 0)
		return E_FAIL;


	// Find the extension unit node that corresponds to the given GUID
	for (unsigned int i = 0; i < dwNumNodes; i++)
	{
		hr = E_FAIL;
		pKsTopologyInfo->get_NodeType(i, &guidNodeType);
		if (IsEqualGUID(guidNodeType, KSNODETYPE_DEV_SPECIFIC))
		{
			printf("found one xu node\n");
			*node = i;
			return S_OK;
		}
	}

	return E_FAIL;
}

//Function to set/get parameters of UVC extension unit
HRESULT SetGetExtensionUnit(GUID xuGuid, ULONG xuPropertyId, ULONG flags, void *data, int len, ULONG *readCount)
{
	GUID pNodeType;
	IUnknown *unKnown;
	IKsControl * ks_control = NULL;
	IKsTopologyInfo * pKsTopologyInfo = NULL;
	KSP_NODE kspNode;

	HRESULT hr = pVideoSource->QueryInterface(__uuidof(IKsTopologyInfo), (void **)&pKsTopologyInfo);
	CHECK_HR_RESULT(hr, "IMFMediaSource::QueryInterface(IKsTopologyInfo)");

	DWORD dwExtensionNode = 0;
	hr = FindExtensionNode(pKsTopologyInfo, &dwExtensionNode);
	CHECK_HR_RESULT(hr, "FindExtensionNode");

	hr = pKsTopologyInfo->get_NodeType(dwExtensionNode, &pNodeType);
	CHECK_HR_RESULT(hr, "IKsTopologyInfo->get_NodeType(...)");

	hr = pKsTopologyInfo->CreateNodeInstance(dwExtensionNode, IID_IUnknown, (LPVOID *)&unKnown);
	CHECK_HR_RESULT(hr, "ks_topology_info->CreateNodeInstance(...)");

	hr = unKnown->QueryInterface(__uuidof(IKsControl), (void **)&ks_control);
	CHECK_HR_RESULT(hr, "ks_topology_info->QueryInterface(...)");

	kspNode.Property.Set = xuGuid;              // XU GUID
	kspNode.NodeId = (ULONG)dwExtensionNode;   // XU Node ID
	kspNode.Property.Id = xuPropertyId;         // XU control ID
	kspNode.Property.Flags = flags;             // Set/Get request

	hr = ks_control->KsProperty((PKSPROPERTY)&kspNode, sizeof(kspNode), (PVOID)data, len, readCount);
	CHECK_HR_RESULT(hr, "ks_control->KsProperty(...)");

done:
	SafeRelease(&ks_control);
	return hr;
}


/*
Smart Camera USB Descriptor
Device Description       : USB Composite Device
Device Path              : \\?\usb#vid_1d6b&pid_0102#5&665da22&0&4#{a5dcbf10-6530-11d2-901f-00c04fb951ed}
Device ID                : USB\VID_1D6B&PID_0102\5&665DA22&0&4
Hardware IDs             : USB\VID_1D6B&PID_0102&REV_0409 USB\VID_1D6B&PID_0102
Driver KeyName           : {36fc9e60-c465-11cf-8056-444553540000}\0005 (GUID_DEVCLASS_USB)
Driver                   : \SystemRoot\System32\drivers\usbccgp.sys (Version: 10.0.18362.693  Date: 2020-04-08)
Driver Inf               : C:\WINDOWS\inf\usb.inf
Legacy BusType           : PNPBus
Class                    : USB
Class GUID               : {36fc9e60-c465-11cf-8056-444553540000} (GUID_DEVCLASS_USB)
Interface GUID           : {a5dcbf10-6530-11d2-901f-00c04fb951ed} (GUID_DEVINTERFACE_USB_DEVICE)
Service                  : usbccgp
Enumerator               : USB
Location Info            : Port_#0004.Hub_#0001
Location IDs             : PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4), ACPI(_SB_)#ACPI(PCI0)#ACPI(XHC_)#ACPI(RHUB)#ACPI(HS04)
Container ID             : {aa611e39-8002-11ea-8c87-54e1ad3167c1}
Manufacturer Info        : (标准 USB 主控制器)
Capabilities             : 0x84 (Removable, SurpriseRemovalOK)
Status                   : 0x0180600A (DN_DRIVER_LOADED, DN_STARTED, DN_DISABLEABLE, DN_REMOVABLE, DN_NT_ENUMERATOR, DN_NT_DRIVER)
Problem Code             : 0
Address                  : 4
Power State              : D0 (supported: D0, D3, wake from D0)
 Child Device 1          : Smart Camera (USB 视频设备)
  KernelName             : \Device\00000198\GLOBAL
  Device ID              : USB\VID_1D6B&PID_0102&MI_00\6&C7FE2D&0&0000
  Class                  : Camera
 Child Device 2          : USB 输入设备
  Device ID              : USB\VID_1D6B&PID_0102&MI_02\6&C7FE2D&0&0002
  Class                  : HIDClass
   Child Device 1        : 符合 HID 标准的供应商定义设备
    DevicePath           : \\?\hid#vid_1d6b&pid_0102&mi_02&col02#7&8793ffa&0&0001#{4d1e55b2-f16f-11cf-88cb-001111000030}
    KernelName           : \Device\0000019c
    Device ID            : HID\VID_1D6B&PID_0102&MI_02&COL02\7&8793FFA&0&0001
    Class                : HIDClass
   Child Device 2        : 符合 HID 标准的供应商定义设备
    DevicePath           : \\?\hid#vid_1d6b&pid_0102&mi_02&col01#7&8793ffa&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}
    KernelName           : \Device\0000019b
    Device ID            : HID\VID_1D6B&PID_0102&MI_02&COL01\7&8793FFA&0&0000
    Class                : HIDClass

        +++++++++++++++++ Registry USB Flags +++++++++++++++++
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\UsbFlags\1D6B01020409
 osvc                    : REG_BINARY 00 00
 NewInterfaceUsage       : REG_DWORD 00000000 (0)

        ---------------- Connection Information ---------------
Connection Index         : 0x04 (4)
Connection Status        : 0x01 (DeviceConnected)
Current Config Value     : 0x01
Device Address           : 0x34 (52)
Is Hub                   : 0x00 (no)
Number Of Open Pipes     : 0x03 (3)
Device Bus Speed         : 0x02 (High-Speed)
Pipe0ScheduleOffset      : 0x00 (0)
Pipe1ScheduleOffset      : 0x00 (0)
Pipe2ScheduleOffset      : 0x00 (0)
Data (HexDump)           : 04 00 00 00 12 01 00 02 EF 02 01 40 6B 1D 02 01   ...........@k...
                           09 04 01 02 00 01 01 02 00 34 00 03 00 00 00 01   .........4......
                           00 00 00 07 05 84 03 10 00 08 00 00 00 00 07 05   ................
                           82 03 00 02 01 00 00 00 00 07 05 01 03 00 02 01   ................
                           00 00 00 00                                       ....

        --------------- Connection Information V2 -------------
Connection Index         : 0x04 (4)
Length                   : 0x10 (16 bytes)
SupportedUsbProtocols    : 0x03
 Usb110                  : 1 (yes)
 Usb200                  : 1 (yes)
 Usb300                  : 0 (no)
 ReservedMBZ             : 0x00
Flags                    : 0x00
 DevIsOpAtSsOrHigher     : 0 (Is not operating at SuperSpeed or higher)
 DevIsSsCapOrHigher      : 0 (Is not SuperSpeed capable or higher)
 DevIsOpAtSsPlusOrHigher : 0 (Is not operating at SuperSpeedPlus or higher)
 DevIsSsPlusCapOrHigher  : 0 (Is not SuperSpeedPlus capable or higher)
 ReservedMBZ             : 0x00
Data (HexDump)           : 04 00 00 00 10 00 00 00 03 00 00 00 00 00 00 00   ................

    ---------------------- Device Descriptor ----------------------
bLength                  : 0x12 (18 bytes)
bDescriptorType          : 0x01 (Device Descriptor)
bcdUSB                   : 0x200 (USB Version 2.00)
bDeviceClass             : 0xEF (Miscellaneous)
bDeviceSubClass          : 0x02
bDeviceProtocol          : 0x01 (IAD - Interface Association Descriptor)
bMaxPacketSize0          : 0x40 (64 bytes)
idVendor                 : 0x1D6B
idProduct                : 0x0102
bcdDevice                : 0x0409
iManufacturer            : 0x01 (String Descriptor 1)
 Language 0x0409         : "Linux Foundation"
iProduct                 : 0x02 (String Descriptor 2)
 Language 0x0409         : "Webcam gadget"
iSerialNumber            : 0x00 (No String Descriptor)
bNumConfigurations       : 0x01 (1 Configuration)
Data (HexDump)           : 12 01 00 02 EF 02 01 40 6B 1D 02 01 09 04 01 02   .......@k.......
                           00 01                                             ..

    ------------------ Configuration Descriptor -------------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x02 (Configuration Descriptor)
wTotalLength             : 0x05EE (1518 bytes)
bNumInterfaces           : 0x03 (3 Interfaces)
bConfigurationValue      : 0x01 (Configuration 1)
iConfiguration           : 0x04 (String Descriptor 4)
 Language 0x0409         : "Video"
bmAttributes             : 0xC0
 D7: Reserved, set 1     : 0x01
 D6: Self Powered        : 0x01 (yes)
 D5: Remote Wakeup       : 0x00 (no)
 D4..0: Reserved, set 0  : 0x00
MaxPower                 : 0x01 (2 mA)
Data (HexDump)           : 09 02 EE 05 03 01 04 C0 01 08 0B 00 02 0E 03 00   ................
                           08 09 04 00 00 01 0E 01 00 08 0D 24 01 10 01 6A   ...........$...j
                           00 00 6C DC 02 01 01 12 24 02 02 01 02 00 00 00   ..l.....$.......
                           00 00 00 00 00 03 1A 00 00 0C 24 05 05 01 00 40   ..........$....@
                           02 FF FF 00 12 1B 24 06 0A 41 76 9E A2 04 DE E3   ......$..Av.....
                           47 8B 2B F4 34 1A FF 00 3B 0F 01 02 02 FF FF 00   G.+.4...;.......
                           1B 24 06 11 91 72 1E 9A 43 68 83 46 6D 92 39 BC   .$...r..Ch.Fm.9.
                           79 06 EE 49 0F 01 0A 02 FF FF 00 09 24 03 03 01   y..I........$...
                           01 00 11 00 07 05 84 03 10 00 08 05 25 03 10 00   ............%...
                           09 04 01 00 00 0E 02 00 06 0F 24 01 02 25 05 81   ..........$..%..
                           00 03 00 00 00 01 00 04 1B 24 04 01 09 4E 56 31   .........$...NV1
                           32 00 00 10 00 80 00 00 AA 00 38 9B 71 0C 01 00   2.........8.q...
                           00 00 00 1E 24 05 01 00 A0 00 5A 00 40 78 7D 01   ....$.....Z.@x}.
                           40 78 7D 01 60 54 00 00 15 16 05 00 01 15 16 05   @x}.`T..........
                           00 1E 24 05 02 00 A0 00 78 00 00 C0 4B 03 00 C0   ..$.....x...K...
                           4B 03 00 46 05 00 15 16 05 00 01 15 16 05 00 1E   K..F............
                           24 05 03 00 B0 00 90 00 00 C0 4B 03 00 C0 4B 03   $.........K...K.
                           00 46 05 00 15 16 05 00 01 15 16 05 00 1E 24 05   .F............$.
                           04 00 40 01 B4 00 00 C0 4B 03 00 C0 4B 03 00 46   ..@.....K...K..F
                           05 00 15 16 05 00 01 15 16 05 00 1E 24 05 05 00   ............$...
                           40 01 F0 00 00 C0 4B 03 00 C0 4B 03 00 46 05 00   @.....K...K..F..
                           15 16 05 00 01 15 16 05 00 1E 24 05 06 00 60 01   ..........$...`.
                           20 01 00 C0 4B 03 00 C0 4B 03 00 46 05 00 15 16    ...K...K..F....
                           05 00 01 15 16 05 00 1E 24 05 07 00 E0 01 0E 01   ........$.......
                           00 C0 4B 03 00 C0 4B 03 00 46 05 00 15 16 05 00   ..K...K..F......
                           01 15 16 05 00 1E 24 05 08 00 80 02 68 01 00 C0   ......$.....h...
                           4B 03 00 C0 4B 03 00 46 05 00 15 16 05 00 01 15   K...K..F........
                           16 05 00 1E 24 05 09 00 80 02 E0 01 00 C0 4B 03   ....$.........K.
                           00 C0 4B 03 00 08 07 00 15 16 05 00 01 15 16 05   ..K.............
                           00 0B 24 06 02 10 00 0E 00 00 00 00 1E 24 07 01   ..$..........$..
                           00 A0 00 5A 00 00 40 9C 00 00 40 9C 00 00 08 07   ...Z..@...@.....
                           00 15 16 05 00 01 15 16 05 00 1E 24 07 02 00 A0   ...........$....
                           00 78 00 00 40 9C 00 00 40 9C 00 00 08 07 00 15   .x..@...@.......
                           16 05 00 01 15 16 05 00 1E 24 07 03 00 B0 00 90   .........$......
                           00 00 40 9C 00 00 40 9C 00 00 08 07 00 15 16 05   ..@...@.........
                           00 01 15 16 05 00 1E 24 07 04 00 40 01 B4 00 00   .......$...@....
                           40 9C 00 00 40 9C 00 00 08 07 00 15 16 05 00 01   @...@...........
                           15 16 05 00 1E 24 07 05 00 40 01 F0 00 00 40 9C   .....$...@....@.
                           00 00 40 9C 00 00 08 07 00 15 16 05 00 01 15 16   ..@.............
                           05 00 1E 24 07 06 00 60 01 20 01 00 40 9C 00 00   ...$...`. ..@...
                           40 9C 00 00 08 07 00 15 16 05 00 01 15 16 05 00   @...............
                           1E 24 07 07 00 E0 01 0E 01 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00 1E 24   ...............$
                           07 08 00 80 02 68 01 00 40 9C 00 00 40 9C 00 00   .....h..@...@...
                           08 07 00 15 16 05 00 01 15 16 05 00 1E 24 07 09   .............$..
                           00 80 02 E0 01 00 40 9C 00 00 40 9C 00 00 08 07   ......@...@.....
                           00 15 16 05 00 01 15 16 05 00 1E 24 07 0A 00 20   ...........$...
                           03 58 02 00 40 9C 00 00 40 9C 00 00 08 07 00 15   .X..@...@.......
                           16 05 00 01 15 16 05 00 1E 24 07 0B 00 C0 03 1C   .........$......
                           02 00 40 9C 00 00 40 9C 00 00 08 07 00 15 16 05   ..@...@.........
                           00 01 15 16 05 00 1E 24 07 0C 00 00 04 40 02 00   .......$.....@..
                           40 9C 00 00 40 9C 00 00 08 07 00 15 16 05 00 01   @...@...........
                           15 16 05 00 1E 24 07 0D 00 00 05 D0 02 00 80 38   .....$.........8
                           01 00 80 38 01 00 20 1C 00 15 16 05 00 01 15 16   ...8.. .........
                           05 00 1E 24 07 0E 00 80 07 38 04 00 00 71 02 00   ...$.....8...q..
                           00 71 02 00 48 3F 00 15 16 05 00 01 15 16 05 00   .q..H?..........
                           1E 24 07 0F 00 00 0A A0 05 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00 1E 24   ...............$
                           07 10 00 00 0F 70 08 00 80 A9 03 00 80 A9 03 00   .....p..........
                           20 FD 00 15 16 05 00 01 15 16 05 00 1C 24 10 03    ............$..
                           10 48 32 36 34 00 00 10 00 80 00 00 AA 00 38 9B   .H264.........8.
                           71 10 0D 00 00 00 00 01 1E 24 11 01 00 A0 00 5A   q........$.....Z
                           00 00 00 7D 00 00 00 7D 00 15 16 05 00 01 00 00   ...}...}........
                           00 00 15 16 05 00 1E 24 11 02 00 A0 00 78 00 00   .......$.....x..
                           00 7D 00 00 00 7D 00 15 16 05 00 01 00 00 00 00   .}...}..........
                           15 16 05 00 1E 24 11 03 00 B0 00 90 00 00 00 7D   .....$.........}
                           00 00 00 7D 00 15 16 05 00 01 00 00 00 00 15 16   ...}............
                           05 00 1E 24 11 04 00 40 01 B4 00 00 00 7D 00 00   ...$...@.....}..
                           00 7D 00 15 16 05 00 01 00 00 00 00 15 16 05 00   .}..............
                           1E 24 11 05 00 40 01 F0 00 00 00 7D 00 00 00 7D   .$...@.....}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00 1E 24   ...............$
                           11 06 00 60 01 20 01 00 00 7D 00 00 00 7D 00 15   ...`. ...}...}..
                           16 05 00 01 00 00 00 00 15 16 05 00 1E 24 11 07   .............$..
                           00 E0 01 0E 01 00 00 7D 00 00 00 7D 00 15 16 05   .......}...}....
                           00 01 00 00 00 00 15 16 05 00 1E 24 11 08 00 80   ...........$....
                           02 68 01 00 00 7D 00 00 00 7D 00 15 16 05 00 01   .h...}...}......
                           00 00 00 00 15 16 05 00 1E 24 11 09 00 80 02 E0   .........$......
                           01 00 00 7D 00 00 00 7D 00 15 16 05 00 01 00 00   ...}...}........
                           00 00 15 16 05 00 1E 24 11 0A 00 20 03 58 02 00   .......$... .X..
                           00 7D 00 00 00 7D 00 15 16 05 00 01 00 00 00 00   .}...}..........
                           15 16 05 00 1E 24 11 0B 00 C0 03 1C 02 00 00 7D   .....$.........}
                           00 00 00 7D 00 15 16 05 00 01 00 00 00 00 15 16   ...}............
                           05 00 1E 24 11 0C 00 00 04 40 02 00 00 7D 00 00   ...$.....@...}..
                           00 7D 00 15 16 05 00 01 00 00 00 00 15 16 05 00   .}..............
                           1E 24 11 0D 00 00 05 D0 02 00 40 9C 00 00 40 9C   .$........@...@.
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00 1E 24   ...............$
                           11 0E 00 80 07 38 04 00 60 EA 00 00 60 EA 00 15   .....8..`...`...
                           16 05 00 01 00 00 00 00 15 16 05 00 1E 24 11 0F   .............$..
                           00 00 0A A0 05 00 2D 31 01 00 2D 31 01 15 16 05   ......-1..-1....
                           00 01 00 00 00 00 15 16 05 00 1E 24 11 10 00 00   ...........$....
                           0F 70 08 00 C0 D4 01 00 C0 D4 01 15 16 05 00 01   .p..............
                           00 00 00 00 15 16 05 00 06 24 0D 01 01 04 09 04   .........$......
                           01 01 01 0E 02 00 06 07 05 81 05 00 14 01 09 04   ................
                           02 00 02 03 00 00 0A 09 21 01 01 00 01 22 3E 00   ........!....">.
                           07 05 82 03 00 02 01 07 05 01 03 00 02 01         ..............

        ------------------- IAD Descriptor --------------------
bLength                  : 0x08 (8 bytes)
bDescriptorType          : 0x0B
bFirstInterface          : 0x00
bInterfaceCount          : 0x02
bFunctionClass           : 0x0E (Video)
bFunctionSubClass        : 0x03 (Video Interface Collection)
bFunctionProtocol        : 0x00 (PC_PROTOCOL_UNDEFINED protocol)
iFunction                : 0x08 (String Descriptor 8)
 Language 0x0409         : "Smart Camera"
Data (HexDump)           : 08 0B 00 02 0E 03 00 08                           ........

        ---------------- Interface Descriptor -----------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x04 (Interface Descriptor)
bInterfaceNumber         : 0x00
bAlternateSetting        : 0x00
bNumEndpoints            : 0x01 (1 Endpoint)
bInterfaceClass          : 0x0E (Video)
bInterfaceSubClass       : 0x01 (Video Control)
bInterfaceProtocol       : 0x00
iInterface               : 0x08 (String Descriptor 8)
 Language 0x0409         : "Smart Camera"
Data (HexDump)           : 09 04 00 00 01 0E 01 00 08                        .........

        ------- Video Control Interface Header Descriptor -----
bLength                  : 0x0D (13 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x01 (Video Control Header)
bcdUVC                   : 0x0110 (UVC Version 1.10)
wTotalLength             : 0x006A (106 bytes)
dwClockFreq              : 0x02DC6C00 (48 MHz)
bInCollection            : 0x01 (1 VideoStreaming interface)
baInterfaceNr[1]         : 0x01
Data (HexDump)           : 0D 24 01 10 01 6A 00 00 6C DC 02 01 01            .$...j..l....

        -------- Video Control Input Terminal Descriptor ------
bLength                  : 0x12 (18 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x02 (Input Terminal)
bTerminalID              : 0x02
wTerminalType            : 0x0201 (ITT_CAMERA)
bAssocTerminal           : 0x00 (Not associated with an Output Terminal)
iTerminal                : 0x00
Camera Input Terminal Data:
wObjectiveFocalLengthMin : 0x0000
wObjectiveFocalLengthMax : 0x0000
wOcularFocalLength       : 0x0000
bControlSize             : 0x03
bmControls               : 0x1A, 0x00, 0x00
 D00                     : 0   no -  Scanning Mode
 D01                     : 1  yes -  Auto-Exposure Mode
 D02                     : 0   no -  Auto-Exposure Priority
 D03                     : 1  yes -  Exposure Time (Absolute)
 D04                     : 1  yes -  Exposure Time (Relative)
 D05                     : 0   no -  Focus (Absolute)
 D06                     : 0   no -  Focus (Relative)
 D07                     : 0   no -  Iris (Absolute)
 D08                     : 0   no -  Iris (Relative)
 D09                     : 0   no -  Zoom (Absolute)
 D10                     : 0   no -  Zoom (Relative)
 D11                     : 0   no -  Pan (Absolute)
 D12                     : 0   no -  Pan (Relative)
 D13                     : 0   no -  Roll (Absolute)
 D14                     : 0   no -  Roll (Relative)
 D15                     : 0   no -  Tilt (Absolute)
 D16                     : 0   no -  Tilt (Relative)
 D17                     : 0   no -  Focus Auto
 D18                     : 0   no -  Reserved
 D19                     : 0   no -  Reserved
 D20                     : 0   no -  Reserved
 D21                     : 0   no -  Reserved
 D22                     : 0   no -  Reserved
 D23                     : 0   no -  Reserved
Data (HexDump)           : 12 24 02 02 01 02 00 00 00 00 00 00 00 00 03 1A   .$..............
                           00 00                                             ..
Data (HexDump)           : 12 24 02 02 01 02 00 00 00 00 00 00 00 00 03 1A   .$..............
                           00 00                                             ..

        -------- Video Control Processing Unit Descriptor -----
bLength                  : 0x0C (12 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x05 (Processing Unit)
bUnitID                  : 0x05
bSourceID                : 0x01
wMaxMultiplier           : 0x4000 (163.84x Zoom)
bControlSize             : 0x02
bmControls               : 0xFF, 0xFF
 D00                     : 1  yes -  Brightness
 D01                     : 1  yes -  Contrast
 D02                     : 1  yes -  Hue
 D03                     : 1  yes -  Saturation
 D04                     : 1  yes -  Sharpness
 D05                     : 1  yes -  Gamma
 D06                     : 1  yes -  White Balance Temperature
 D07                     : 1  yes -  White Balance Component
 D08                     : 1  yes -  Backlight Compensation
 D09                     : 1  yes -  Gain
 D10                     : 1  yes -  Power Line Frequency
 D11                     : 1  yes -  Hue, Auto
 D12                     : 1  yes -  White Balance Temperature, Auto
 D13                     : 1  yes -  White Balance Component, Auto
 D14                     : 1  yes -  Digital Multiplier
 D15                     : 1  yes -  Digital Multiplier Limit
iProcessing              : 0x00
bmVideoStandards         : 0x12
 D00                   : 0   no -  None
 D01                   : 1  yes -  NTSC  - 525/60
 D02                   : 0   no -  PAL   - 625/50
 D03                   : 0   no -  SECAM - 625/50
 D04                   : 1  yes -  NTSC  - 625/50
 D05                   : 0   no -  PAL   - 525/60
 D06                   : 0   no -  Reserved
 D07                   : 0   no -  Reserved
Data (HexDump)           : 0C 24 05 05 01 00 40 02 FF FF 00 12               .$....@.....

        --------- Video Control Extension Unit Descriptor -----
bLength                  : 0x1B (27 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x06 (Extension Unit)
bUnitID                  : 0x0A
guidExtensionCode        : {A29E7641-DE04-47E3-8B2B-F4341AFF003B}
bNumControls             : 0x0F
bNrInPins                : 0x01 (1 pins)
baSourceID[1]            : 0x02
bControlSize             : 0x02
bmControls               : 0xFF, 0xFF
 D00                     : 1  yes -  Vendor-Specific (Optional)
 D01                     : 1  yes -  Vendor-Specific (Optional)
 D02                     : 1  yes -  Vendor-Specific (Optional)
 D03                     : 1  yes -  Vendor-Specific (Optional)
 D04                     : 1  yes -  Vendor-Specific (Optional)
 D05                     : 1  yes -  Vendor-Specific (Optional)
 D06                     : 1  yes -  Vendor-Specific (Optional)
 D07                     : 1  yes -  Vendor-Specific (Optional)
 D08                     : 1  yes -  Vendor-Specific (Optional)
 D09                     : 1  yes -  Vendor-Specific (Optional)
 D10                     : 1  yes -  Vendor-Specific (Optional)
 D11                     : 1  yes -  Vendor-Specific (Optional)
 D12                     : 1  yes -  Vendor-Specific (Optional)
 D13                     : 1  yes -  Vendor-Specific (Optional)
 D14                     : 1  yes -  Vendor-Specific (Optional)
 D15                     : 1  yes -  Vendor-Specific (Optional)
iExtension               : 0x00
Data (HexDump)           : 1B 24 06 0A 41 76 9E A2 04 DE E3 47 8B 2B F4 34   .$..Av.....G.+.4
                           1A FF 00 3B 0F 01 02 02 FF FF 00                  ...;.......

        --------- Video Control Extension Unit Descriptor -----
bLength                  : 0x1B (27 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x06 (Extension Unit)
bUnitID                  : 0x11
guidExtensionCode        : {9A1E7291-6843-4683-6D92-39BC7906EE49}
bNumControls             : 0x0F
bNrInPins                : 0x01 (1 pins)
baSourceID[1]            : 0x0A
bControlSize             : 0x02
bmControls               : 0xFF, 0xFF
 D00                     : 1  yes -  Vendor-Specific (Optional)
 D01                     : 1  yes -  Vendor-Specific (Optional)
 D02                     : 1  yes -  Vendor-Specific (Optional)
 D03                     : 1  yes -  Vendor-Specific (Optional)
 D04                     : 1  yes -  Vendor-Specific (Optional)
 D05                     : 1  yes -  Vendor-Specific (Optional)
 D06                     : 1  yes -  Vendor-Specific (Optional)
 D07                     : 1  yes -  Vendor-Specific (Optional)
 D08                     : 1  yes -  Vendor-Specific (Optional)
 D09                     : 1  yes -  Vendor-Specific (Optional)
 D10                     : 1  yes -  Vendor-Specific (Optional)
 D11                     : 1  yes -  Vendor-Specific (Optional)
 D12                     : 1  yes -  Vendor-Specific (Optional)
 D13                     : 1  yes -  Vendor-Specific (Optional)
 D14                     : 1  yes -  Vendor-Specific (Optional)
 D15                     : 1  yes -  Vendor-Specific (Optional)
iExtension               : 0x00
Data (HexDump)           : 1B 24 06 11 91 72 1E 9A 43 68 83 46 6D 92 39 BC   .$...r..Ch.Fm.9.
                           79 06 EE 49 0F 01 0A 02 FF FF 00                  y..I.......

        ------- Video Control Output Terminal Descriptor ------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x03 (Output Terminal)
bTerminalID              : 0x03
wTerminalType            : 0x0101 (TT_STREAMING)
bAssocTerminal           : 0x00 (Not associated with an Input Terminal)
bSourceID                : 0x11
iTerminal                : 0x00
Data (HexDump)           : 09 24 03 03 01 01 00 11 00                        .$.......

        ----------------- Endpoint Descriptor -----------------
bLength                  : 0x07 (7 bytes)
bDescriptorType          : 0x05 (Endpoint Descriptor)
bEndpointAddress         : 0x84 (Direction=IN EndpointID=4)
bmAttributes             : 0x03 (TransferType=Interrupt)
wMaxPacketSize           : 0x0010
 Bits 15..13             : 0x00 (reserved, must be zero)
 Bits 12..11             : 0x00 (0 additional transactions per microframe -> allows 1..1024 bytes per packet)
 Bits 10..0              : 0x10 (16 bytes per packet)
bInterval                : 0x08 (8 ms)
Data (HexDump)           : 07 05 84 03 10 00 08                              .......

        --- Class-specific VC Interrupt Endpoint Descriptor ---
bLength                  : 0x05 (5 bytes)
bDescriptorType          : 0x25 (Video Control Endpoint)
bDescriptorSubtype       : 0x03 (Interrupt)
wMaxTransferSize         : 0x0010 (16 bytes)
Data (HexDump)           : 05 25 03 10 00                                    .%...

        ---------------- Interface Descriptor -----------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x04 (Interface Descriptor)
bInterfaceNumber         : 0x01
bAlternateSetting        : 0x00
bNumEndpoints            : 0x00 (Default Control Pipe only)
bInterfaceClass          : 0x0E (Video)
bInterfaceSubClass       : 0x02 (Video Streaming)
bInterfaceProtocol       : 0x00
iInterface               : 0x06 (String Descriptor 6)
 Language 0x0409         : "Video Streaming"
Data (HexDump)           : 09 04 01 00 00 0E 02 00 06                        .........

        ---- VC-Specific VS Video Input Header Descriptor -----
bLength                  : 0x0F (15 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x01 (Input Header)
bNumFormats              : 0x02
wTotalLength             : 0x0525 (1317 bytes)
bEndpointAddress         : 0x81 (Direction=IN  EndpointID=1)
bmInfo                   : 0x00 (Dynamic Format Change not supported)
bTerminalLink            : 0x03
bStillCaptureMethod      : 0x00 (No Still Capture)
nbTriggerSupport         : 0x00 (Hardware Triggering not supported)
bTriggerUsage            : 0x00 (Host will initiate still image capture)
nbControlSize            : 0x01
Video Payload Format 1   : 0x00
 D0                      : 0   no -  Key Frame Rate
 D1                      : 0   no -  P Frame Rate
 D2                      : 0   no -  Compression Quality
 D3                      : 0   no -  Compression Window Size
 D4                      : 0   no -  Generate Key Frame
 D5                      : 0   no -  Update Frame Segment
 D6                      : 0   no -  Reserved
 D7                      : 0   no -  Reserved
Video Payload Format 2   : 0x04
 D0                      : 0   no -  Key Frame Rate
 D1                      : 0   no -  P Frame Rate
 D2                      : 1  yes -  Compression Quality
 D3                      : 0   no -  Compression Window Size
 D4                      : 0   no -  Generate Key Frame
 D5                      : 0   no -  Update Frame Segment
 D6                      : 0   no -  Reserved
 D7                      : 0   no -  Reserved
Data (HexDump)           : 0F 24 01 02 25 05 81 00 03 00 00 00 01 00 04      .$..%..........

        ------- VS Uncompressed Format Type Descriptor --------
bLength                  : 0x1B (27 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x04 (Uncompressed Format Type)
bFormatIndex             : 0x01 (1)
bNumFrameDescriptors     : 0x09 (9)
guidFormat               : {3231564E-0000-0010-8000-00AA00389B71} (NV12)
bBitsPerPixel            : 0x0C (12 bits)
bDefaultFrameIndex       : 0x01 (1)
bAspectRatioX            : 0x00
bAspectRatioY            : 0x00
bmInterlaceFlags         : 0x00
 D0 IL stream or variable: 0 (no)
 D1 Fields per frame     : 0 (2 fields)
 D2 Field 1 first        : 0 (no)
 D3 Reserved             : 0
 D4..5 Field pattern     : 0 (Field 1 only)
 D6..7 Display Mode      : 0 (Bob only)
bCopyProtect             : 0x00 (No restrictions)
*!*ERROR:  no Color Matching Descriptor for this format
Data (HexDump)           : 1B 24 04 01 09 4E 56 31 32 00 00 10 00 80 00 00   .$...NV12.......
                           AA 00 38 9B 71 0C 01 00 00 00 00                  ..8.q......

        -------- VS Uncompressed Frame Type Descriptor --------
---> This is the Default (optimum) Frame index
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x01
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x005A (90)
dwMinBitRate             : 0x017D7840 (25000000 bps -> 3.1 MB/s)
dwMaxBitRate             : 0x017D7840 (25000000 bps -> 3.1 MB/s)
dwMaxVideoFrameBufferSize: 0x00005460 (21600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 01 00 A0 00 5A 00 40 78 7D 01 40 78 7D   .$.....Z.@x}.@x}
                           01 60 54 00 00 15 16 05 00 01 15 16 05 00         .`T...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x02
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x0078 (120)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 02 00 A0 00 78 00 00 C0 4B 03 00 C0 4B   .$.....x...K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x03
bmCapabilities           : 0x00
wWidth                   : 0x00B0 (176)
wHeight                  : 0x0090 (144)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 03 00 B0 00 90 00 00 C0 4B 03 00 C0 4B   .$.........K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x04
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00B4 (180)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 04 00 40 01 B4 00 00 C0 4B 03 00 C0 4B   .$...@.....K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x05
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00F0 (240)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 05 00 40 01 F0 00 00 C0 4B 03 00 C0 4B   .$...@.....K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x06
bmCapabilities           : 0x00
wWidth                   : 0x0160 (352)
wHeight                  : 0x0120 (288)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 06 00 60 01 20 01 00 C0 4B 03 00 C0 4B   .$...`. ...K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x07
bmCapabilities           : 0x00
wWidth                   : 0x01E0 (480)
wHeight                  : 0x010E (270)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 07 00 E0 01 0E 01 00 C0 4B 03 00 C0 4B   .$.........K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x08
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x0168 (360)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 08 00 80 02 68 01 00 C0 4B 03 00 C0 4B   .$.....h...K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x09
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x01E0 (480)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 09 00 80 02 E0 01 00 C0 4B 03 00 C0 4B   .$.........K...K
                           03 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Format Type Descriptor ----
bLength                  : 0x0B (11 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x06 (Format MJPEG)
bFormatIndex             : 0x02 (2)
bNumFrameDescriptors     : 0x10 (16)
bmFlags                  : 0x00 (Sample size is not fixed)
bDefaultFrameIndex       : 0x0E (14)
bAspectRatioX            : 0x00
bAspectRatioY            : 0x00
bmInterlaceFlags         : 0x00
 D0 IL stream or variable: 0 (no)
 D1 Fields per frame     : 0 (2 fields)
 D2 Field 1 first        : 0 (no)
 D3 Reserved             : 0
 D4..5 Field pattern     : 0 (Field 1 only)
 D6..7 Display Mode      : 0 (Bob only)
bCopyProtect             : 0x00 (No restrictions)
Data (HexDump)           : 0B 24 06 02 10 00 0E 00 00 00 00                  .$.........

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x01
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x005A (90)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 01 00 A0 00 5A 00 00 40 9C 00 00 40 9C   .$.....Z..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x02
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x0078 (120)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 02 00 A0 00 78 00 00 40 9C 00 00 40 9C   .$.....x..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x03
bmCapabilities           : 0x00
wWidth                   : 0x00B0 (176)
wHeight                  : 0x0090 (144)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 03 00 B0 00 90 00 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x04
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00B4 (180)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 04 00 40 01 B4 00 00 40 9C 00 00 40 9C   .$...@....@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x05
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00F0 (240)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 05 00 40 01 F0 00 00 40 9C 00 00 40 9C   .$...@....@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x06
bmCapabilities           : 0x00
wWidth                   : 0x0160 (352)
wHeight                  : 0x0120 (288)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 06 00 60 01 20 01 00 40 9C 00 00 40 9C   .$...`. ..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x07
bmCapabilities           : 0x00
wWidth                   : 0x01E0 (480)
wHeight                  : 0x010E (270)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 07 00 E0 01 0E 01 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x08
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x0168 (360)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 08 00 80 02 68 01 00 40 9C 00 00 40 9C   .$.....h..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x09
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x01E0 (480)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 09 00 80 02 E0 01 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0A
bmCapabilities           : 0x00
wWidth                   : 0x0320 (800)
wHeight                  : 0x0258 (600)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0A 00 20 03 58 02 00 40 9C 00 00 40 9C   .$... .X..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0B
bmCapabilities           : 0x00
wWidth                   : 0x03C0 (960)
wHeight                  : 0x021C (540)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0B 00 C0 03 1C 02 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0C
bmCapabilities           : 0x00
wWidth                   : 0x0400 (1024)
wHeight                  : 0x0240 (576)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0C 00 00 04 40 02 00 40 9C 00 00 40 9C   .$.....@..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0D
bmCapabilities           : 0x00
wWidth                   : 0x0500 (1280)
wHeight                  : 0x02D0 (720)
dwMinBitRate             : 0x01388000 (20480000 bps -> 2.5 MB/s)
dwMaxBitRate             : 0x01388000 (20480000 bps -> 2.5 MB/s)
dwMaxVideoFrameBufferSize: 0x001C2000 (1843200 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0D 00 00 05 D0 02 00 80 38 01 00 80 38   .$.........8...8
                           01 00 20 1C 00 15 16 05 00 01 15 16 05 00         .. ...........

        ----- Video Streaming MJPEG Frame Type Descriptor -----
---> This is the Default (optimum) Frame index
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0E
bmCapabilities           : 0x00
wWidth                   : 0x0780 (1920)
wHeight                  : 0x0438 (1080)
dwMinBitRate             : 0x02710000 (40960000 bps -> 5.1 MB/s)
dwMaxBitRate             : 0x02710000 (40960000 bps -> 5.1 MB/s)
dwMaxVideoFrameBufferSize: 0x003F4800 (4147200 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0E 00 80 07 38 04 00 00 71 02 00 00 71   .$.....8...q...q
                           02 00 48 3F 00 15 16 05 00 01 15 16 05 00         ..H?..........

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0F
bmCapabilities           : 0x00
wWidth                   : 0x0A00 (2560)
wHeight                  : 0x05A0 (1440)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0F 00 00 0A A0 05 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x10
bmCapabilities           : 0x00
wWidth                   : 0x0F00 (3840)
wHeight                  : 0x0870 (2160)
dwMinBitRate             : 0x03A98000 (61440000 bps -> 7.6 MB/s)
dwMaxBitRate             : 0x03A98000 (61440000 bps -> 7.6 MB/s)
dwMaxVideoFrameBufferSize: 0x00FD2000 (16588800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 10 00 00 0F 70 08 00 80 A9 03 00 80 A9   .$.....p........
                           03 00 20 FD 00 15 16 05 00 01 15 16 05 00         .. ...........

        ---- VS Frame Based Payload Format Type Descriptor ----
bLength                  : 0x1C (28 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x10 (Frame Based Format Type)
bFormatIndex             : 0x03 (3)
bNumFrameDescriptors     : 0x10 (16)
guidFormat               : {34363248-0000-0010-8000-00AA00389B71} (H264)
bBitsPerPixel            : 0x10 (16 bits)
bDefaultFrameIndex       : 0x0D (13)
bAspectRatioX            : 0x00
bAspectRatioY            : 0x00
bmInterlaceFlags         : 0x00
 D0 IL stream or variable: 0 (no)
 D1 Fields per frame     : 0 (2 fields)
 D2 Field 1 first        : 0 (no)
 D3 Reserved             : 0
 D4..5 Field pattern     : 0 (Field 1 only)
 D6..7 Display Mode      : 0 (Bob only)
bCopyProtect             : 0x00 (No restrictions)
bVariableSize            : 0x01 (Variable Size)
Data (HexDump)           : 1C 24 10 03 10 48 32 36 34 00 00 10 00 80 00 00   .$...H264.......
                           AA 00 38 9B 71 10 0D 00 00 00 00 01               ..8.q.......

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x01
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x005A (90)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 01 00 A0 00 5A 00 00 00 7D 00 00 00 7D   .$.....Z...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x02
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x0078 (120)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 02 00 A0 00 78 00 00 00 7D 00 00 00 7D   .$.....x...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x03
bmCapabilities           : 0x00
wWidth                   : 0x00B0 (176)
wHeight                  : 0x0090 (144)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 03 00 B0 00 90 00 00 00 7D 00 00 00 7D   .$.........}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x04
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00B4 (180)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 04 00 40 01 B4 00 00 00 7D 00 00 00 7D   .$...@.....}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x05
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00F0 (240)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 05 00 40 01 F0 00 00 00 7D 00 00 00 7D   .$...@.....}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x06
bmCapabilities           : 0x00
wWidth                   : 0x0160 (352)
wHeight                  : 0x0120 (288)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 06 00 60 01 20 01 00 00 7D 00 00 00 7D   .$...`. ...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x07
bmCapabilities           : 0x00
wWidth                   : 0x01E0 (480)
wHeight                  : 0x010E (270)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 07 00 E0 01 0E 01 00 00 7D 00 00 00 7D   .$.........}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x08
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x0168 (360)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 08 00 80 02 68 01 00 00 7D 00 00 00 7D   .$.....h...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x09
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x01E0 (480)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 09 00 80 02 E0 01 00 00 7D 00 00 00 7D   .$.........}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0A
bmCapabilities           : 0x00
wWidth                   : 0x0320 (800)
wHeight                  : 0x0258 (600)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0A 00 20 03 58 02 00 00 7D 00 00 00 7D   .$... .X...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0B
bmCapabilities           : 0x00
wWidth                   : 0x03C0 (960)
wHeight                  : 0x021C (540)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0B 00 C0 03 1C 02 00 00 7D 00 00 00 7D   .$.........}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0C
bmCapabilities           : 0x00
wWidth                   : 0x0400 (1024)
wHeight                  : 0x0240 (576)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0C 00 00 04 40 02 00 00 7D 00 00 00 7D   .$.....@...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0D
bmCapabilities           : 0x00
wWidth                   : 0x0500 (1280)
wHeight                  : 0x02D0 (720)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0D 00 00 05 D0 02 00 40 9C 00 00 40 9C   .$........@...@.
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0E
bmCapabilities           : 0x00
wWidth                   : 0x0780 (1920)
wHeight                  : 0x0438 (1080)
dwMinBitRate             : 0x00EA6000 (15360000 bps -> 1.9 MB/s)
dwMaxBitRate             : 0x00EA6000 (15360000 bps -> 1.9 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0E 00 80 07 38 04 00 60 EA 00 00 60 EA   .$.....8..`...`.
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0F
bmCapabilities           : 0x00
wWidth                   : 0x0A00 (2560)
wHeight                  : 0x05A0 (1440)
dwMinBitRate             : 0x01312D00 (20000000 bps -> 2.5 MB/s)
dwMaxBitRate             : 0x01312D00 (20000000 bps -> 2.5 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0F 00 00 0A A0 05 00 2D 31 01 00 2D 31   .$........-1..-1
                           01 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x10
bmCapabilities           : 0x00
wWidth                   : 0x0F00 (3840)
wHeight                  : 0x0870 (2160)
dwMinBitRate             : 0x01D4C000 (30720000 bps -> 3.8 MB/s)
dwMaxBitRate             : 0x01D4C000 (30720000 bps -> 3.8 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 10 00 00 0F 70 08 00 C0 D4 01 00 C0 D4   .$.....p........
                           01 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ------- VS Color Matching Descriptor Descriptor -------
bLength                  : 0x06 (6 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x0D (Color Matching)
bColorPrimaries          : 0x01 (BT.709, sRGB)
bTransferCharacteristics : 0x01 (BT.709)
bMatrixCoefficients      : 0x04 (SMPTE 170M)
Data (HexDump)           : 06 24 0D 01 01 04                                 .$....

        ---------------- Interface Descriptor -----------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x04 (Interface Descriptor)
bInterfaceNumber         : 0x01
bAlternateSetting        : 0x01
bNumEndpoints            : 0x01 (1 Endpoint)
bInterfaceClass          : 0x0E (Video)
bInterfaceSubClass       : 0x02 (Video Streaming)
bInterfaceProtocol       : 0x00
iInterface               : 0x06 (String Descriptor 6)
 Language 0x0409         : "Video Streaming"
Data (HexDump)           : 09 04 01 01 01 0E 02 00 06                        .........

        ----------------- Endpoint Descriptor -----------------
bLength                  : 0x07 (7 bytes)
bDescriptorType          : 0x05 (Endpoint Descriptor)
bEndpointAddress         : 0x81 (Direction=IN EndpointID=1)
bmAttributes             : 0x05 (TransferType=Isochronous  SyncType=Asynchronous  EndpointType=Data)
wMaxPacketSize           : 0x1400
 Bits 15..13             : 0x00 (reserved, must be zero)
 Bits 12..11             : 0x02 (2 additional transactions per microframe -> allows 683..1024 bytes per packet)
 Bits 10..0              : 0x400 (1024 bytes per packet)
bInterval                : 0x01 (1 ms)
Data (HexDump)           : 07 05 81 05 00 14 01                              .......

        ---------------- Interface Descriptor -----------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x04 (Interface Descriptor)
bInterfaceNumber         : 0x02
bAlternateSetting        : 0x00
bNumEndpoints            : 0x02 (2 Endpoints)
bInterfaceClass          : 0x03 (HID - Human Interface Device)
bInterfaceSubClass       : 0x00 (None)
bInterfaceProtocol       : 0x00 (None)
iInterface               : 0x0A (String Descriptor 10)
 Language 0x0409         : "HID Interface"
Data (HexDump)           : 09 04 02 00 02 03 00 00 0A                        .........

        ------------------- HID Descriptor --------------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x21 (HID Descriptor)
bcdHID                   : 0x0101 (HID Version 1.01)
bCountryCode             : 0x00 (00 = not localized)
bNumDescriptors          : 0x01
Data (HexDump)           : 09 21 01 01 00 01 22 3E 00                        .!....">.
Descriptor 1:
bDescriptorType          : 0x22 (Class=Report)
wDescriptorLength        : 0x003E (62 bytes)
Error reading descriptor : ERROR_GEN_FAILURE

        ----------------- Endpoint Descriptor -----------------
bLength                  : 0x07 (7 bytes)
bDescriptorType          : 0x05 (Endpoint Descriptor)
bEndpointAddress         : 0x82 (Direction=IN EndpointID=2)
bmAttributes             : 0x03 (TransferType=Interrupt)
wMaxPacketSize           : 0x0200
 Bits 15..13             : 0x00 (reserved, must be zero)
 Bits 12..11             : 0x00 (0 additional transactions per microframe -> allows 1..1024 bytes per packet)
 Bits 10..0              : 0x200 (512 bytes per packet)
bInterval                : 0x01 (1 ms)
Data (HexDump)           : 07 05 82 03 00 02 01                              .......

        ----------------- Endpoint Descriptor -----------------
bLength                  : 0x07 (7 bytes)
bDescriptorType          : 0x05 (Endpoint Descriptor)
bEndpointAddress         : 0x01 (Direction=OUT EndpointID=1)
bmAttributes             : 0x03 (TransferType=Interrupt)
wMaxPacketSize           : 0x0200
 Bits 15..13             : 0x00 (reserved, must be zero)
 Bits 12..11             : 0x00 (0 additional transactions per microframe -> allows 1..1024 bytes per packet)
 Bits 10..0              : 0x200 (512 bytes per packet)
bInterval                : 0x01 (1 ms)
Data (HexDump)           : 07 05 01 03 00 02 01                              .......

    ----------------- Device Qualifier Descriptor -----------------
bLength                  : 0x0A (10 bytes)
bDescriptorType          : 0x06 (Device_qualifier Descriptor)
bcdUSB                   : 0x200 (USB Version 2.00)
bDeviceClass             : 0xEF (Miscellaneous)
bDeviceSubClass          : 0x02
bDeviceProtocol          : 0x01 (IAD - Interface Association Descriptor)
bMaxPacketSize0          : 0x40 (64 Bytes)
bNumConfigurations       : 0x01 (1 other-speed configuration)
bReserved                : 0x00

    ------------ Other Speed Configuration Descriptor -------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x07 (Other_speed_configuration Descriptor)
wTotalLength             : 0x05EE (1518 bytes)
bNumInterfaces           : 0x03 (3 Interfaces)
bConfigurationValue      : 0x01 (Configuration 1)
iConfiguration           : 0x04 (String Descriptor 4)
 Language 0x0409         : "Video"
bmAttributes             : 0xC0
 D7: Reserved, set 1     : 0x01
 D6: Self Powered        : 0x01 (yes)
 D5: Remote Wakeup       : 0x00 (no)
 D4..0: Reserved, set 0  : 0x00
MaxPower                 : 0x01 (2 mA)
Data (HexDump)           : 09 07 EE 05 03 01 04 C0 01 08 0B 00 02 0E 03 00   ................
                           08 09 04 00 00 01 0E 01 00 08 0D 24 01 10 01 6A   ...........$...j
                           00 00 6C DC 02 01 01 12 24 02 02 01 02 00 00 00   ..l.....$.......
                           00 00 00 00 00 03 1A 00 00 0C 24 05 05 01 00 40   ..........$....@
                           02 FF FF 00 12 1B 24 06 0A 41 76 9E A2 04 DE E3   ......$..Av.....
                           47 8B 2B F4 34 1A FF 00 3B 0F 01 02 02 FF FF 00   G.+.4...;.......
                           1B 24 06 11 91 72 1E 9A 43 68 83 46 6D 92 39 BC   .$...r..Ch.Fm.9.
                           79 06 EE 49 0F 01 0A 02 FF FF 00 09 24 03 03 01   y..I........$...
                           01 00 11 00 07 05 84 03 10 00 08 05 25 03 10 00   ............%...
                           09 04 01 00 00 0E 02 00 06 0F 24 01 02 25 05 81   ..........$..%..
                           00 03 00 00 00 01 00 04 1B 24 04 01 09 4E 56 31   .........$...NV1
                           32 00 00 10 00 80 00 00 AA 00 38 9B 71 0C 01 00   2.........8.q...
                           00 00 00 1E 24 05 01 00 A0 00 5A 00 40 78 7D 01   ....$.....Z.@x}.
                           40 78 7D 01 60 54 00 00 15 16 05 00 01 15 16 05   @x}.`T..........
                           00 1E 24 05 02 00 A0 00 78 00 00 C0 4B 03 00 C0   ..$.....x...K...
                           4B 03 00 46 05 00 15 16 05 00 01 15 16 05 00 1E   K..F............
                           24 05 03 00 B0 00 90 00 00 C0 4B 03 00 C0 4B 03   $.........K...K.
                           00 46 05 00 15 16 05 00 01 15 16 05 00 1E 24 05   .F............$.
                           04 00 40 01 B4 00 00 C0 4B 03 00 C0 4B 03 00 46   ..@.....K...K..F
                           05 00 15 16 05 00 01 15 16 05 00 1E 24 05 05 00   ............$...
                           40 01 F0 00 00 C0 4B 03 00 C0 4B 03 00 46 05 00   @.....K...K..F..
                           15 16 05 00 01 15 16 05 00 1E 24 05 06 00 60 01   ..........$...`.
                           20 01 00 C0 4B 03 00 C0 4B 03 00 46 05 00 15 16    ...K...K..F....
                           05 00 01 15 16 05 00 1E 24 05 07 00 E0 01 0E 01   ........$.......
                           00 C0 4B 03 00 C0 4B 03 00 46 05 00 15 16 05 00   ..K...K..F......
                           01 15 16 05 00 1E 24 05 08 00 80 02 68 01 00 C0   ......$.....h...
                           4B 03 00 C0 4B 03 00 46 05 00 15 16 05 00 01 15   K...K..F........
                           16 05 00 1E 24 05 09 00 80 02 E0 01 00 C0 4B 03   ....$.........K.
                           00 C0 4B 03 00 08 07 00 15 16 05 00 01 15 16 05   ..K.............
                           00 0B 24 06 02 10 00 0E 00 00 00 00 1E 24 07 01   ..$..........$..
                           00 A0 00 5A 00 00 40 9C 00 00 40 9C 00 00 08 07   ...Z..@...@.....
                           00 15 16 05 00 01 15 16 05 00 1E 24 07 02 00 A0   ...........$....
                           00 78 00 00 40 9C 00 00 40 9C 00 00 08 07 00 15   .x..@...@.......
                           16 05 00 01 15 16 05 00 1E 24 07 03 00 B0 00 90   .........$......
                           00 00 40 9C 00 00 40 9C 00 00 08 07 00 15 16 05   ..@...@.........
                           00 01 15 16 05 00 1E 24 07 04 00 40 01 B4 00 00   .......$...@....
                           40 9C 00 00 40 9C 00 00 08 07 00 15 16 05 00 01   @...@...........
                           15 16 05 00 1E 24 07 05 00 40 01 F0 00 00 40 9C   .....$...@....@.
                           00 00 40 9C 00 00 08 07 00 15 16 05 00 01 15 16   ..@.............
                           05 00 1E 24 07 06 00 60 01 20 01 00 40 9C 00 00   ...$...`. ..@...
                           40 9C 00 00 08 07 00 15 16 05 00 01 15 16 05 00   @...............
                           1E 24 07 07 00 E0 01 0E 01 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00 1E 24   ...............$
                           07 08 00 80 02 68 01 00 40 9C 00 00 40 9C 00 00   .....h..@...@...
                           08 07 00 15 16 05 00 01 15 16 05 00 1E 24 07 09   .............$..
                           00 80 02 E0 01 00 40 9C 00 00 40 9C 00 00 08 07   ......@...@.....
                           00 15 16 05 00 01 15 16 05 00 1E 24 07 0A 00 20   ...........$...
                           03 58 02 00 40 9C 00 00 40 9C 00 00 08 07 00 15   .X..@...@.......
                           16 05 00 01 15 16 05 00 1E 24 07 0B 00 C0 03 1C   .........$......
                           02 00 40 9C 00 00 40 9C 00 00 08 07 00 15 16 05   ..@...@.........
                           00 01 15 16 05 00 1E 24 07 0C 00 00 04 40 02 00   .......$.....@..
                           40 9C 00 00 40 9C 00 00 08 07 00 15 16 05 00 01   @...@...........
                           15 16 05 00 1E 24 07 0D 00 00 05 D0 02 00 80 38   .....$.........8
                           01 00 80 38 01 00 20 1C 00 15 16 05 00 01 15 16   ...8.. .........
                           05 00 1E 24 07 0E 00 80 07 38 04 00 00 71 02 00   ...$.....8...q..
                           00 71 02 00 48 3F 00 15 16 05 00 01 15 16 05 00   .q..H?..........
                           1E 24 07 0F 00 00 0A A0 05 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00 1E 24   ...............$
                           07 10 00 00 0F 70 08 00 80 A9 03 00 80 A9 03 00   .....p..........
                           20 FD 00 15 16 05 00 01 15 16 05 00 1C 24 10 03    ............$..
                           10 48 32 36 34 00 00 10 00 80 00 00 AA 00 38 9B   .H264.........8.
                           71 10 0D 00 00 00 00 01 1E 24 11 01 00 A0 00 5A   q........$.....Z
                           00 00 00 7D 00 00 00 7D 00 15 16 05 00 01 00 00   ...}...}........
                           00 00 15 16 05 00 1E 24 11 02 00 A0 00 78 00 00   .......$.....x..
                           00 7D 00 00 00 7D 00 15 16 05 00 01 00 00 00 00   .}...}..........
                           15 16 05 00 1E 24 11 03 00 B0 00 90 00 00 00 7D   .....$.........}
                           00 00 00 7D 00 15 16 05 00 01 00 00 00 00 15 16   ...}............
                           05 00 1E 24 11 04 00 40 01 B4 00 00 00 7D 00 00   ...$...@.....}..
                           00 7D 00 15 16 05 00 01 00 00 00 00 15 16 05 00   .}..............
                           1E 24 11 05 00 40 01 F0 00 00 00 7D 00 00 00 7D   .$...@.....}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00 1E 24   ...............$
                           11 06 00 60 01 20 01 00 00 7D 00 00 00 7D 00 15   ...`. ...}...}..
                           16 05 00 01 00 00 00 00 15 16 05 00 1E 24 11 07   .............$..
                           00 E0 01 0E 01 00 00 7D 00 00 00 7D 00 15 16 05   .......}...}....
                           00 01 00 00 00 00 15 16 05 00 1E 24 11 08 00 80   ...........$....
                           02 68 01 00 00 7D 00 00 00 7D 00 15 16 05 00 01   .h...}...}......
                           00 00 00 00 15 16 05 00 1E 24 11 09 00 80 02 E0   .........$......
                           01 00 00 7D 00 00 00 7D 00 15 16 05 00 01 00 00   ...}...}........
                           00 00 15 16 05 00 1E 24 11 0A 00 20 03 58 02 00   .......$... .X..
                           00 7D 00 00 00 7D 00 15 16 05 00 01 00 00 00 00   .}...}..........
                           15 16 05 00 1E 24 11 0B 00 C0 03 1C 02 00 00 7D   .....$.........}
                           00 00 00 7D 00 15 16 05 00 01 00 00 00 00 15 16   ...}............
                           05 00 1E 24 11 0C 00 00 04 40 02 00 00 7D 00 00   ...$.....@...}..
                           00 7D 00 15 16 05 00 01 00 00 00 00 15 16 05 00   .}..............
                           1E 24 11 0D 00 00 05 D0 02 00 40 9C 00 00 40 9C   .$........@...@.
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00 1E 24   ...............$
                           11 0E 00 80 07 38 04 00 60 EA 00 00 60 EA 00 15   .....8..`...`...
                           16 05 00 01 00 00 00 00 15 16 05 00 1E 24 11 0F   .............$..
                           00 00 0A A0 05 00 2D 31 01 00 2D 31 01 15 16 05   ......-1..-1....
                           00 01 00 00 00 00 15 16 05 00 1E 24 11 10 00 00   ...........$....
                           0F 70 08 00 C0 D4 01 00 C0 D4 01 15 16 05 00 01   .p..............
                           00 00 00 00 15 16 05 00 06 24 0D 01 01 04 09 04   .........$......
                           01 01 01 0E 02 00 06 07 05 81 05 FF 03 01 09 04   ................
                           02 00 02 03 00 00 0A 09 21 01 01 00 01 22 3E 00   ........!....">.
                           07 05 82 03 00 02 01 07 05 01 03 00 02 01         ..............

        ------------------- IAD Descriptor --------------------
bLength                  : 0x08 (8 bytes)
bDescriptorType          : 0x0B
bFirstInterface          : 0x00
bInterfaceCount          : 0x02
bFunctionClass           : 0x0E (Video)
bFunctionSubClass        : 0x03 (Video Interface Collection)
bFunctionProtocol        : 0x00 (PC_PROTOCOL_UNDEFINED protocol)
iFunction                : 0x08 (String Descriptor 8)
 Language 0x0409         : "Smart Camera"
Data (HexDump)           : 08 0B 00 02 0E 03 00 08                           ........

        ---------------- Interface Descriptor -----------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x04 (Interface Descriptor)
bInterfaceNumber         : 0x00
bAlternateSetting        : 0x00
bNumEndpoints            : 0x01 (1 Endpoint)
bInterfaceClass          : 0x0E (Video)
bInterfaceSubClass       : 0x01 (Video Control)
bInterfaceProtocol       : 0x00
iInterface               : 0x08 (String Descriptor 8)
 Language 0x0409         : "Smart Camera"
Data (HexDump)           : 09 04 00 00 01 0E 01 00 08                        .........

        ------- Video Control Interface Header Descriptor -----
bLength                  : 0x0D (13 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x01 (Video Control Header)
bcdUVC                   : 0x0110 (UVC Version 1.10)
wTotalLength             : 0x006A (106 bytes)
dwClockFreq              : 0x02DC6C00 (48 MHz)
bInCollection            : 0x01 (1 VideoStreaming interface)
baInterfaceNr[1]         : 0x01
Data (HexDump)           : 0D 24 01 10 01 6A 00 00 6C DC 02 01 01            .$...j..l....

        -------- Video Control Input Terminal Descriptor ------
bLength                  : 0x12 (18 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x02 (Input Terminal)
bTerminalID              : 0x02
wTerminalType            : 0x0201 (ITT_CAMERA)
bAssocTerminal           : 0x00 (Not associated with an Output Terminal)
iTerminal                : 0x00
Camera Input Terminal Data:
wObjectiveFocalLengthMin : 0x0000
wObjectiveFocalLengthMax : 0x0000
wOcularFocalLength       : 0x0000
bControlSize             : 0x03
bmControls               : 0x1A, 0x00, 0x00
 D00                     : 0   no -  Scanning Mode
 D01                     : 1  yes -  Auto-Exposure Mode
 D02                     : 0   no -  Auto-Exposure Priority
 D03                     : 1  yes -  Exposure Time (Absolute)
 D04                     : 1  yes -  Exposure Time (Relative)
 D05                     : 0   no -  Focus (Absolute)
 D06                     : 0   no -  Focus (Relative)
 D07                     : 0   no -  Iris (Absolute)
 D08                     : 0   no -  Iris (Relative)
 D09                     : 0   no -  Zoom (Absolute)
 D10                     : 0   no -  Zoom (Relative)
 D11                     : 0   no -  Pan (Absolute)
 D12                     : 0   no -  Pan (Relative)
 D13                     : 0   no -  Roll (Absolute)
 D14                     : 0   no -  Roll (Relative)
 D15                     : 0   no -  Tilt (Absolute)
 D16                     : 0   no -  Tilt (Relative)
 D17                     : 0   no -  Focus Auto
 D18                     : 0   no -  Reserved
 D19                     : 0   no -  Reserved
 D20                     : 0   no -  Reserved
 D21                     : 0   no -  Reserved
 D22                     : 0   no -  Reserved
 D23                     : 0   no -  Reserved
Data (HexDump)           : 12 24 02 02 01 02 00 00 00 00 00 00 00 00 03 1A   .$..............
                           00 00                                             ..
Data (HexDump)           : 12 24 02 02 01 02 00 00 00 00 00 00 00 00 03 1A   .$..............
                           00 00                                             ..

        -------- Video Control Processing Unit Descriptor -----
bLength                  : 0x0C (12 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x05 (Processing Unit)
bUnitID                  : 0x05
bSourceID                : 0x01
wMaxMultiplier           : 0x4000 (163.84x Zoom)
bControlSize             : 0x02
bmControls               : 0xFF, 0xFF
 D00                     : 1  yes -  Brightness
 D01                     : 1  yes -  Contrast
 D02                     : 1  yes -  Hue
 D03                     : 1  yes -  Saturation
 D04                     : 1  yes -  Sharpness
 D05                     : 1  yes -  Gamma
 D06                     : 1  yes -  White Balance Temperature
 D07                     : 1  yes -  White Balance Component
 D08                     : 1  yes -  Backlight Compensation
 D09                     : 1  yes -  Gain
 D10                     : 1  yes -  Power Line Frequency
 D11                     : 1  yes -  Hue, Auto
 D12                     : 1  yes -  White Balance Temperature, Auto
 D13                     : 1  yes -  White Balance Component, Auto
 D14                     : 1  yes -  Digital Multiplier
 D15                     : 1  yes -  Digital Multiplier Limit
iProcessing              : 0x00
bmVideoStandards         : 0x12
 D00                   : 0   no -  None
 D01                   : 1  yes -  NTSC  - 525/60
 D02                   : 0   no -  PAL   - 625/50
 D03                   : 0   no -  SECAM - 625/50
 D04                   : 1  yes -  NTSC  - 625/50
 D05                   : 0   no -  PAL   - 525/60
 D06                   : 0   no -  Reserved
 D07                   : 0   no -  Reserved
Data (HexDump)           : 0C 24 05 05 01 00 40 02 FF FF 00 12               .$....@.....

        --------- Video Control Extension Unit Descriptor -----
bLength                  : 0x1B (27 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x06 (Extension Unit)
bUnitID                  : 0x0A
guidExtensionCode        : {A29E7641-DE04-47E3-8B2B-F4341AFF003B}
bNumControls             : 0x0F
bNrInPins                : 0x01 (1 pins)
baSourceID[1]            : 0x02
bControlSize             : 0x02
bmControls               : 0xFF, 0xFF
 D00                     : 1  yes -  Vendor-Specific (Optional)
 D01                     : 1  yes -  Vendor-Specific (Optional)
 D02                     : 1  yes -  Vendor-Specific (Optional)
 D03                     : 1  yes -  Vendor-Specific (Optional)
 D04                     : 1  yes -  Vendor-Specific (Optional)
 D05                     : 1  yes -  Vendor-Specific (Optional)
 D06                     : 1  yes -  Vendor-Specific (Optional)
 D07                     : 1  yes -  Vendor-Specific (Optional)
 D08                     : 1  yes -  Vendor-Specific (Optional)
 D09                     : 1  yes -  Vendor-Specific (Optional)
 D10                     : 1  yes -  Vendor-Specific (Optional)
 D11                     : 1  yes -  Vendor-Specific (Optional)
 D12                     : 1  yes -  Vendor-Specific (Optional)
 D13                     : 1  yes -  Vendor-Specific (Optional)
 D14                     : 1  yes -  Vendor-Specific (Optional)
 D15                     : 1  yes -  Vendor-Specific (Optional)
iExtension               : 0x00
Data (HexDump)           : 1B 24 06 0A 41 76 9E A2 04 DE E3 47 8B 2B F4 34   .$..Av.....G.+.4
                           1A FF 00 3B 0F 01 02 02 FF FF 00                  ...;.......

        --------- Video Control Extension Unit Descriptor -----
bLength                  : 0x1B (27 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x06 (Extension Unit)
bUnitID                  : 0x11
guidExtensionCode        : {9A1E7291-6843-4683-6D92-39BC7906EE49}
bNumControls             : 0x0F
bNrInPins                : 0x01 (1 pins)
baSourceID[1]            : 0x0A
bControlSize             : 0x02
bmControls               : 0xFF, 0xFF
 D00                     : 1  yes -  Vendor-Specific (Optional)
 D01                     : 1  yes -  Vendor-Specific (Optional)
 D02                     : 1  yes -  Vendor-Specific (Optional)
 D03                     : 1  yes -  Vendor-Specific (Optional)
 D04                     : 1  yes -  Vendor-Specific (Optional)
 D05                     : 1  yes -  Vendor-Specific (Optional)
 D06                     : 1  yes -  Vendor-Specific (Optional)
 D07                     : 1  yes -  Vendor-Specific (Optional)
 D08                     : 1  yes -  Vendor-Specific (Optional)
 D09                     : 1  yes -  Vendor-Specific (Optional)
 D10                     : 1  yes -  Vendor-Specific (Optional)
 D11                     : 1  yes -  Vendor-Specific (Optional)
 D12                     : 1  yes -  Vendor-Specific (Optional)
 D13                     : 1  yes -  Vendor-Specific (Optional)
 D14                     : 1  yes -  Vendor-Specific (Optional)
 D15                     : 1  yes -  Vendor-Specific (Optional)
iExtension               : 0x00
Data (HexDump)           : 1B 24 06 11 91 72 1E 9A 43 68 83 46 6D 92 39 BC   .$...r..Ch.Fm.9.
                           79 06 EE 49 0F 01 0A 02 FF FF 00                  y..I.......

        ------- Video Control Output Terminal Descriptor ------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x24 (Video Control Interface)
bDescriptorSubtype       : 0x03 (Output Terminal)
bTerminalID              : 0x03
wTerminalType            : 0x0101 (TT_STREAMING)
bAssocTerminal           : 0x00 (Not associated with an Input Terminal)
bSourceID                : 0x11
iTerminal                : 0x00
Data (HexDump)           : 09 24 03 03 01 01 00 11 00                        .$.......

        ----------------- Endpoint Descriptor -----------------
bLength                  : 0x07 (7 bytes)
bDescriptorType          : 0x05 (Endpoint Descriptor)
bEndpointAddress         : 0x84 (Direction=IN EndpointID=4)
bmAttributes             : 0x03 (TransferType=Interrupt)
wMaxPacketSize           : 0x0010
 Bits 15..13             : 0x00 (reserved, must be zero)
 Bits 12..11             : 0x00 (0 additional transactions per microframe -> allows 1..1024 bytes per packet)
 Bits 10..0              : 0x10 (16 bytes per packet)
bInterval                : 0x08 (8 ms)
Data (HexDump)           : 07 05 84 03 10 00 08                              .......

        --- Class-specific VC Interrupt Endpoint Descriptor ---
bLength                  : 0x05 (5 bytes)
bDescriptorType          : 0x25 (Video Control Endpoint)
bDescriptorSubtype       : 0x03 (Interrupt)
wMaxTransferSize         : 0x0010 (16 bytes)
Data (HexDump)           : 05 25 03 10 00                                    .%...

        ---------------- Interface Descriptor -----------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x04 (Interface Descriptor)
bInterfaceNumber         : 0x01
bAlternateSetting        : 0x00
bNumEndpoints            : 0x00 (Default Control Pipe only)
bInterfaceClass          : 0x0E (Video)
bInterfaceSubClass       : 0x02 (Video Streaming)
bInterfaceProtocol       : 0x00
iInterface               : 0x06 (String Descriptor 6)
 Language 0x0409         : "Video Streaming"
Data (HexDump)           : 09 04 01 00 00 0E 02 00 06                        .........

        ---- VC-Specific VS Video Input Header Descriptor -----
bLength                  : 0x0F (15 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x01 (Input Header)
bNumFormats              : 0x02
wTotalLength             : 0x0525 (1317 bytes)
bEndpointAddress         : 0x81 (Direction=IN  EndpointID=1)
bmInfo                   : 0x00 (Dynamic Format Change not supported)
bTerminalLink            : 0x03
bStillCaptureMethod      : 0x00 (No Still Capture)
nbTriggerSupport         : 0x00 (Hardware Triggering not supported)
bTriggerUsage            : 0x00 (Host will initiate still image capture)
nbControlSize            : 0x01
Video Payload Format 1   : 0x00
 D0                      : 0   no -  Key Frame Rate
 D1                      : 0   no -  P Frame Rate
 D2                      : 0   no -  Compression Quality
 D3                      : 0   no -  Compression Window Size
 D4                      : 0   no -  Generate Key Frame
 D5                      : 0   no -  Update Frame Segment
 D6                      : 0   no -  Reserved
 D7                      : 0   no -  Reserved
Video Payload Format 2   : 0x04
 D0                      : 0   no -  Key Frame Rate
 D1                      : 0   no -  P Frame Rate
 D2                      : 1  yes -  Compression Quality
 D3                      : 0   no -  Compression Window Size
 D4                      : 0   no -  Generate Key Frame
 D5                      : 0   no -  Update Frame Segment
 D6                      : 0   no -  Reserved
 D7                      : 0   no -  Reserved
Data (HexDump)           : 0F 24 01 02 25 05 81 00 03 00 00 00 01 00 04      .$..%..........

        ------- VS Uncompressed Format Type Descriptor --------
bLength                  : 0x1B (27 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x04 (Uncompressed Format Type)
bFormatIndex             : 0x01 (1)
bNumFrameDescriptors     : 0x09 (9)
guidFormat               : {3231564E-0000-0010-8000-00AA00389B71} (NV12)
bBitsPerPixel            : 0x0C (12 bits)
bDefaultFrameIndex       : 0x01 (1)
bAspectRatioX            : 0x00
bAspectRatioY            : 0x00
bmInterlaceFlags         : 0x00
 D0 IL stream or variable: 0 (no)
 D1 Fields per frame     : 0 (2 fields)
 D2 Field 1 first        : 0 (no)
 D3 Reserved             : 0
 D4..5 Field pattern     : 0 (Field 1 only)
 D6..7 Display Mode      : 0 (Bob only)
bCopyProtect             : 0x00 (No restrictions)
*!*ERROR:  no Color Matching Descriptor for this format
Data (HexDump)           : 1B 24 04 01 09 4E 56 31 32 00 00 10 00 80 00 00   .$...NV12.......
                           AA 00 38 9B 71 0C 01 00 00 00 00                  ..8.q......

        -------- VS Uncompressed Frame Type Descriptor --------
---> This is the Default (optimum) Frame index
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x01
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x005A (90)
dwMinBitRate             : 0x017D7840 (25000000 bps -> 3.1 MB/s)
dwMaxBitRate             : 0x017D7840 (25000000 bps -> 3.1 MB/s)
dwMaxVideoFrameBufferSize: 0x00005460 (21600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 01 00 A0 00 5A 00 40 78 7D 01 40 78 7D   .$.....Z.@x}.@x}
                           01 60 54 00 00 15 16 05 00 01 15 16 05 00         .`T...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x02
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x0078 (120)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 02 00 A0 00 78 00 00 C0 4B 03 00 C0 4B   .$.....x...K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x03
bmCapabilities           : 0x00
wWidth                   : 0x00B0 (176)
wHeight                  : 0x0090 (144)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 03 00 B0 00 90 00 00 C0 4B 03 00 C0 4B   .$.........K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x04
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00B4 (180)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 04 00 40 01 B4 00 00 C0 4B 03 00 C0 4B   .$...@.....K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x05
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00F0 (240)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 05 00 40 01 F0 00 00 C0 4B 03 00 C0 4B   .$...@.....K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x06
bmCapabilities           : 0x00
wWidth                   : 0x0160 (352)
wHeight                  : 0x0120 (288)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 06 00 60 01 20 01 00 C0 4B 03 00 C0 4B   .$...`. ...K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x07
bmCapabilities           : 0x00
wWidth                   : 0x01E0 (480)
wHeight                  : 0x010E (270)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 07 00 E0 01 0E 01 00 C0 4B 03 00 C0 4B   .$.........K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x08
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x0168 (360)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00054600 (345600 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 08 00 80 02 68 01 00 C0 4B 03 00 C0 4B   .$.....h...K...K
                           03 00 46 05 00 15 16 05 00 01 15 16 05 00         ..F...........

        -------- VS Uncompressed Frame Type Descriptor --------
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x05 (Uncompressed Frame Type)
bFrameIndex              : 0x09
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x01E0 (480)
dwMinBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxBitRate             : 0x034BC000 (55296000 bps -> 6.9 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 05 09 00 80 02 E0 01 00 C0 4B 03 00 C0 4B   .$.........K...K
                           03 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Format Type Descriptor ----
bLength                  : 0x0B (11 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x06 (Format MJPEG)
bFormatIndex             : 0x02 (2)
bNumFrameDescriptors     : 0x10 (16)
bmFlags                  : 0x00 (Sample size is not fixed)
bDefaultFrameIndex       : 0x0E (14)
bAspectRatioX            : 0x00
bAspectRatioY            : 0x00
bmInterlaceFlags         : 0x00
 D0 IL stream or variable: 0 (no)
 D1 Fields per frame     : 0 (2 fields)
 D2 Field 1 first        : 0 (no)
 D3 Reserved             : 0
 D4..5 Field pattern     : 0 (Field 1 only)
 D6..7 Display Mode      : 0 (Bob only)
bCopyProtect             : 0x00 (No restrictions)
Data (HexDump)           : 0B 24 06 02 10 00 0E 00 00 00 00                  .$.........

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x01
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x005A (90)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 01 00 A0 00 5A 00 00 40 9C 00 00 40 9C   .$.....Z..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x02
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x0078 (120)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 02 00 A0 00 78 00 00 40 9C 00 00 40 9C   .$.....x..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x03
bmCapabilities           : 0x00
wWidth                   : 0x00B0 (176)
wHeight                  : 0x0090 (144)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 03 00 B0 00 90 00 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x04
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00B4 (180)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 04 00 40 01 B4 00 00 40 9C 00 00 40 9C   .$...@....@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x05
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00F0 (240)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 05 00 40 01 F0 00 00 40 9C 00 00 40 9C   .$...@....@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x06
bmCapabilities           : 0x00
wWidth                   : 0x0160 (352)
wHeight                  : 0x0120 (288)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 06 00 60 01 20 01 00 40 9C 00 00 40 9C   .$...`. ..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x07
bmCapabilities           : 0x00
wWidth                   : 0x01E0 (480)
wHeight                  : 0x010E (270)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 07 00 E0 01 0E 01 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x08
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x0168 (360)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 08 00 80 02 68 01 00 40 9C 00 00 40 9C   .$.....h..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x09
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x01E0 (480)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 09 00 80 02 E0 01 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0A
bmCapabilities           : 0x00
wWidth                   : 0x0320 (800)
wHeight                  : 0x0258 (600)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0A 00 20 03 58 02 00 40 9C 00 00 40 9C   .$... .X..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0B
bmCapabilities           : 0x00
wWidth                   : 0x03C0 (960)
wHeight                  : 0x021C (540)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0B 00 C0 03 1C 02 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0C
bmCapabilities           : 0x00
wWidth                   : 0x0400 (1024)
wHeight                  : 0x0240 (576)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0C 00 00 04 40 02 00 40 9C 00 00 40 9C   .$.....@..@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0D
bmCapabilities           : 0x00
wWidth                   : 0x0500 (1280)
wHeight                  : 0x02D0 (720)
dwMinBitRate             : 0x01388000 (20480000 bps -> 2.5 MB/s)
dwMaxBitRate             : 0x01388000 (20480000 bps -> 2.5 MB/s)
dwMaxVideoFrameBufferSize: 0x001C2000 (1843200 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0D 00 00 05 D0 02 00 80 38 01 00 80 38   .$.........8...8
                           01 00 20 1C 00 15 16 05 00 01 15 16 05 00         .. ...........

        ----- Video Streaming MJPEG Frame Type Descriptor -----
---> This is the Default (optimum) Frame index
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0E
bmCapabilities           : 0x00
wWidth                   : 0x0780 (1920)
wHeight                  : 0x0438 (1080)
dwMinBitRate             : 0x02710000 (40960000 bps -> 5.1 MB/s)
dwMaxBitRate             : 0x02710000 (40960000 bps -> 5.1 MB/s)
dwMaxVideoFrameBufferSize: 0x003F4800 (4147200 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0E 00 80 07 38 04 00 00 71 02 00 00 71   .$.....8...q...q
                           02 00 48 3F 00 15 16 05 00 01 15 16 05 00         ..H?..........

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x0F
bmCapabilities           : 0x00
wWidth                   : 0x0A00 (2560)
wHeight                  : 0x05A0 (1440)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxVideoFrameBufferSize: 0x00070800 (460800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 0F 00 00 0A A0 05 00 40 9C 00 00 40 9C   .$........@...@.
                           00 00 08 07 00 15 16 05 00 01 15 16 05 00         ..............

        ----- Video Streaming MJPEG Frame Type Descriptor -----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x07 (MJPEG Frame Type)
bFrameIndex              : 0x10
bmCapabilities           : 0x00
wWidth                   : 0x0F00 (3840)
wHeight                  : 0x0870 (2160)
dwMinBitRate             : 0x03A98000 (61440000 bps -> 7.6 MB/s)
dwMaxBitRate             : 0x03A98000 (61440000 bps -> 7.6 MB/s)
dwMaxVideoFrameBufferSize: 0x00FD2000 (16588800 bytes)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 07 10 00 00 0F 70 08 00 80 A9 03 00 80 A9   .$.....p........
                           03 00 20 FD 00 15 16 05 00 01 15 16 05 00         .. ...........

        ---- VS Frame Based Payload Format Type Descriptor ----
bLength                  : 0x1C (28 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x10 (Frame Based Format Type)
bFormatIndex             : 0x03 (3)
bNumFrameDescriptors     : 0x10 (16)
guidFormat               : {34363248-0000-0010-8000-00AA00389B71} (H264)
bBitsPerPixel            : 0x10 (16 bits)
bDefaultFrameIndex       : 0x0D (13)
bAspectRatioX            : 0x00
bAspectRatioY            : 0x00
bmInterlaceFlags         : 0x00
 D0 IL stream or variable: 0 (no)
 D1 Fields per frame     : 0 (2 fields)
 D2 Field 1 first        : 0 (no)
 D3 Reserved             : 0
 D4..5 Field pattern     : 0 (Field 1 only)
 D6..7 Display Mode      : 0 (Bob only)
bCopyProtect             : 0x00 (No restrictions)
bVariableSize            : 0x01 (Variable Size)
Data (HexDump)           : 1C 24 10 03 10 48 32 36 34 00 00 10 00 80 00 00   .$...H264.......
                           AA 00 38 9B 71 10 0D 00 00 00 00 01               ..8.q.......

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x01
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x005A (90)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 01 00 A0 00 5A 00 00 00 7D 00 00 00 7D   .$.....Z...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x02
bmCapabilities           : 0x00
wWidth                   : 0x00A0 (160)
wHeight                  : 0x0078 (120)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 02 00 A0 00 78 00 00 00 7D 00 00 00 7D   .$.....x...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x03
bmCapabilities           : 0x00
wWidth                   : 0x00B0 (176)
wHeight                  : 0x0090 (144)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 03 00 B0 00 90 00 00 00 7D 00 00 00 7D   .$.........}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x04
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00B4 (180)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 04 00 40 01 B4 00 00 00 7D 00 00 00 7D   .$...@.....}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x05
bmCapabilities           : 0x00
wWidth                   : 0x0140 (320)
wHeight                  : 0x00F0 (240)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 05 00 40 01 F0 00 00 00 7D 00 00 00 7D   .$...@.....}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x06
bmCapabilities           : 0x00
wWidth                   : 0x0160 (352)
wHeight                  : 0x0120 (288)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 06 00 60 01 20 01 00 00 7D 00 00 00 7D   .$...`. ...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x07
bmCapabilities           : 0x00
wWidth                   : 0x01E0 (480)
wHeight                  : 0x010E (270)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 07 00 E0 01 0E 01 00 00 7D 00 00 00 7D   .$.........}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x08
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x0168 (360)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 08 00 80 02 68 01 00 00 7D 00 00 00 7D   .$.....h...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x09
bmCapabilities           : 0x00
wWidth                   : 0x0280 (640)
wHeight                  : 0x01E0 (480)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 09 00 80 02 E0 01 00 00 7D 00 00 00 7D   .$.........}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0A
bmCapabilities           : 0x00
wWidth                   : 0x0320 (800)
wHeight                  : 0x0258 (600)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0A 00 20 03 58 02 00 00 7D 00 00 00 7D   .$... .X...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0B
bmCapabilities           : 0x00
wWidth                   : 0x03C0 (960)
wHeight                  : 0x021C (540)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0B 00 C0 03 1C 02 00 00 7D 00 00 00 7D   .$.........}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0C
bmCapabilities           : 0x00
wWidth                   : 0x0400 (1024)
wHeight                  : 0x0240 (576)
dwMinBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwMaxBitRate             : 0x007D0000 (8192000 bps -> 1 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0C 00 00 04 40 02 00 00 7D 00 00 00 7D   .$.....@...}...}
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0D
bmCapabilities           : 0x00
wWidth                   : 0x0500 (1280)
wHeight                  : 0x02D0 (720)
dwMinBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwMaxBitRate             : 0x009C4000 (10240000 bps -> 1.2 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0D 00 00 05 D0 02 00 40 9C 00 00 40 9C   .$........@...@.
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0E
bmCapabilities           : 0x00
wWidth                   : 0x0780 (1920)
wHeight                  : 0x0438 (1080)
dwMinBitRate             : 0x00EA6000 (15360000 bps -> 1.9 MB/s)
dwMaxBitRate             : 0x00EA6000 (15360000 bps -> 1.9 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0E 00 80 07 38 04 00 60 EA 00 00 60 EA   .$.....8..`...`.
                           00 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x0F
bmCapabilities           : 0x00
wWidth                   : 0x0A00 (2560)
wHeight                  : 0x05A0 (1440)
dwMinBitRate             : 0x01312D00 (20000000 bps -> 2.5 MB/s)
dwMaxBitRate             : 0x01312D00 (20000000 bps -> 2.5 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 0F 00 00 0A A0 05 00 2D 31 01 00 2D 31   .$........-1..-1
                           01 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ----- VS Frame Based Payload Frame Type Descriptor ----
bLength                  : 0x1E (30 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x11 (Frame Based Payload Frame Type)
bFrameIndex              : 0x10
bmCapabilities           : 0x00
wWidth                   : 0x0F00 (3840)
wHeight                  : 0x0870 (2160)
dwMinBitRate             : 0x01D4C000 (30720000 bps -> 3.8 MB/s)
dwMaxBitRate             : 0x01D4C000 (30720000 bps -> 3.8 MB/s)
dwDefaultFrameInterval   : 0x00051615 (33.3333 ms -> 30.000 fps)
bFrameIntervalType       : 0x01 (1 discrete frame interval supported)
dwBytesPerLine           : 0x00 (0 bytes)
adwFrameInterval[1]      : 0x00051615 (33.3333 ms -> 30.000 fps)
Data (HexDump)           : 1E 24 11 10 00 00 0F 70 08 00 C0 D4 01 00 C0 D4   .$.....p........
                           01 15 16 05 00 01 00 00 00 00 15 16 05 00         ..............

        ------- VS Color Matching Descriptor Descriptor -------
bLength                  : 0x06 (6 bytes)
bDescriptorType          : 0x24 (Video Streaming Interface)
bDescriptorSubtype       : 0x0D (Color Matching)
bColorPrimaries          : 0x01 (BT.709, sRGB)
bTransferCharacteristics : 0x01 (BT.709)
bMatrixCoefficients      : 0x04 (SMPTE 170M)
Data (HexDump)           : 06 24 0D 01 01 04                                 .$....

        ---------------- Interface Descriptor -----------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x04 (Interface Descriptor)
bInterfaceNumber         : 0x01
bAlternateSetting        : 0x01
bNumEndpoints            : 0x01 (1 Endpoint)
bInterfaceClass          : 0x0E (Video)
bInterfaceSubClass       : 0x02 (Video Streaming)
bInterfaceProtocol       : 0x00
iInterface               : 0x06 (String Descriptor 6)
 Language 0x0409         : "Video Streaming"
Data (HexDump)           : 09 04 01 01 01 0E 02 00 06                        .........

        ----------------- Endpoint Descriptor -----------------
bLength                  : 0x07 (7 bytes)
bDescriptorType          : 0x05 (Endpoint Descriptor)
bEndpointAddress         : 0x81 (Direction=IN EndpointID=1)
bmAttributes             : 0x05 (TransferType=Isochronous  SyncType=Asynchronous  EndpointType=Data)
wMaxPacketSize           : 0x03FF
 Bits 15..13             : 0x00 (reserved, must be zero)
 Bits 12..11             : 0x00 (0 additional transactions per microframe -> allows 1..1024 bytes per packet)
 Bits 10..0              : 0x3FF (1023 bytes per packet)
bInterval                : 0x01 (1 ms)
Data (HexDump)           : 07 05 81 05 FF 03 01                              .......

        ---------------- Interface Descriptor -----------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x04 (Interface Descriptor)
bInterfaceNumber         : 0x02
bAlternateSetting        : 0x00
bNumEndpoints            : 0x02 (2 Endpoints)
bInterfaceClass          : 0x03 (HID - Human Interface Device)
bInterfaceSubClass       : 0x00 (None)
bInterfaceProtocol       : 0x00 (None)
iInterface               : 0x0A (String Descriptor 10)
 Language 0x0409         : "HID Interface"
Data (HexDump)           : 09 04 02 00 02 03 00 00 0A                        .........

        ------------------- HID Descriptor --------------------
bLength                  : 0x09 (9 bytes)
bDescriptorType          : 0x21 (HID Descriptor)
bcdHID                   : 0x0101 (HID Version 1.01)
bCountryCode             : 0x00 (00 = not localized)
bNumDescriptors          : 0x01
Data (HexDump)           : 09 21 01 01 00 01 22 3E 00                        .!....">.
Descriptor 1:
bDescriptorType          : 0x22 (Class=Report)
wDescriptorLength        : 0x003E (62 bytes)
Error reading descriptor : ERROR_GEN_FAILURE

        ----------------- Endpoint Descriptor -----------------
bLength                  : 0x07 (7 bytes)
bDescriptorType          : 0x05 (Endpoint Descriptor)
bEndpointAddress         : 0x82 (Direction=IN EndpointID=2)
bmAttributes             : 0x03 (TransferType=Interrupt)
wMaxPacketSize           : 0x0200
 Bits 15..13             : 0x00 (reserved, must be zero)
 Bits 12..11             : 0x00 (0 additional transactions per microframe -> allows 1..1024 bytes per packet)
 Bits 10..0              : 0x200 (512 bytes per packet)
bInterval                : 0x01 (1 ms)
Data (HexDump)           : 07 05 82 03 00 02 01                              .......

        ----------------- Endpoint Descriptor -----------------
bLength                  : 0x07 (7 bytes)
bDescriptorType          : 0x05 (Endpoint Descriptor)
bEndpointAddress         : 0x01 (Direction=OUT EndpointID=1)
bmAttributes             : 0x03 (TransferType=Interrupt)
wMaxPacketSize           : 0x0200
 Bits 15..13             : 0x00 (reserved, must be zero)
 Bits 12..11             : 0x00 (0 additional transactions per microframe -> allows 1..1024 bytes per packet)
 Bits 10..0              : 0x200 (512 bytes per packet)
bInterval                : 0x01 (1 ms)
Data (HexDump)           : 07 05 01 03 00 02 01                              .......

      -------------------- String Descriptors -------------------
             ------ String Descriptor 0 ------
bLength                  : 0x04 (4 bytes)
bDescriptorType          : 0x03 (String Descriptor)
Language ID[0]           : 0x0409 (English - United States)
Data (HexDump)           : 04 03 09 04                                       ....
             ------ String Descriptor 1 ------
bLength                  : 0x22 (34 bytes)
bDescriptorType          : 0x03 (String Descriptor)
Language 0x0409          : "Linux Foundation"
Data (HexDump)           : 22 03 4C 00 69 00 6E 00 75 00 78 00 20 00 46 00   ".L.i.n.u.x. .F.
                           6F 00 75 00 6E 00 64 00 61 00 74 00 69 00 6F 00   o.u.n.d.a.t.i.o.
                           6E 00                                             n.
             ------ String Descriptor 2 ------
bLength                  : 0x1C (28 bytes)
bDescriptorType          : 0x03 (String Descriptor)
Language 0x0409          : "Webcam gadget"
Data (HexDump)           : 1C 03 57 00 65 00 62 00 63 00 61 00 6D 00 20 00   ..W.e.b.c.a.m. .
                           67 00 61 00 64 00 67 00 65 00 74 00               g.a.d.g.e.t.
             ------ String Descriptor 4 ------
bLength                  : 0x0C (12 bytes)
bDescriptorType          : 0x03 (String Descriptor)
Language 0x0409          : "Video"
Data (HexDump)           : 0C 03 56 00 69 00 64 00 65 00 6F 00               ..V.i.d.e.o.
             ------ String Descriptor 6 ------
bLength                  : 0x20 (32 bytes)
bDescriptorType          : 0x03 (String Descriptor)
Language 0x0409          : "Video Streaming"
Data (HexDump)           : 20 03 56 00 69 00 64 00 65 00 6F 00 20 00 53 00    .V.i.d.e.o. .S.
                           74 00 72 00 65 00 61 00 6D 00 69 00 6E 00 67 00   t.r.e.a.m.i.n.g.
             ------ String Descriptor 8 ------
bLength                  : 0x1A (26 bytes)
bDescriptorType          : 0x03 (String Descriptor)
Language 0x0409          : "Smart Camera"
Data (HexDump)           : 1A 03 53 00 6D 00 61 00 72 00 74 00 20 00 43 00   ..S.m.a.r.t. .C.
                           61 00 6D 00 65 00 72 00 61 00                     a.m.e.r.a.
             ------ String Descriptor 10 ------
bLength                  : 0x1C (28 bytes)
bDescriptorType          : 0x03 (String Descriptor)
Language 0x0409          : "HID Interface"
Data (HexDump)           : 1C 03 48 00 49 00 44 00 20 00 49 00 6E 00 74 00   ..H.I.D. .I.n.t.
                           65 00 72 00 66 00 61 00 63 00 65 00               e.r.f.a.c.e.

*/