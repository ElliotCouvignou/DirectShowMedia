#include "DirectShowMediaCommon.h"
#include "DirectShowMediaType.h"
#include "DirectShowMedia.h"


FString GUIDToUEString(const GUID& guid)
{
	// Implement a conversion function that translates known GUIDs into meaningful strings.
	// You can start with a few known GUIDs and expand as needed.
	// For example:
	if (guid == MEDIATYPE_Video) return TEXT("MEDIATYPE_Video");
	else if (guid == MEDIASUBTYPE_MJPG) return TEXT("MEDIASUBTYPE_MJPG");
	else if (guid == MEDIASUBTYPE_NV12) return TEXT("MEDIASUBTYPE_NV12");
	else if (guid == MEDIASUBTYPE_RGB32) return TEXT("MEDIASUBTYPE_RGB32");
	else if (guid == MEDIASUBTYPE_YUY2) return TEXT("MEDIASUBTYPE_YUY2");
	else if (guid == MEDIASUBTYPE_UYVY) return TEXT("MEDIASUBTYPE_UYVY");
	// Add more GUIDs as necessary...
	else return FString::Printf(TEXT("{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}"),
								guid.Data1, guid.Data2, guid.Data3, 
								guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], 
								guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
}

FString CompressionToUEString(DWORD compression)
{
	// If known compressions (like BI_RGB, BI_RLE8) are used, you can translate them. 
	// For example:
	if (compression == BI_RGB) return TEXT("BI_RGB");
	else if(compression == BI_JPEG) return TEXT("BI_JPEG");
	// Add more compressions as necessary...
	else return FString::Printf(TEXT("Unknown compression: %lu"), compression);
}

void LogAudioMediaType(const AM_MEDIA_TYPE& mt)
{
	if (mt.formattype == FORMAT_WaveFormatEx && mt.pbFormat != nullptr)
	{
		WAVEFORMATEX* pWfex = reinterpret_cast<WAVEFORMATEX*>(mt.pbFormat);

		UE_LOG(LogDirectShowMedia, Log, TEXT("---- AUDIO MEDIA TYPE ----"));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Major Type: %s"), *GUIDToUEString(mt.majortype));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Sub Type: %s"), *GUIDToUEString(mt.subtype));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Fixed Size Samples: %d"), mt.bFixedSizeSamples);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Temporal Compression: %d"), mt.bTemporalCompression);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Sample Size: %ld"), mt.lSampleSize);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Format Type: %s"), *GUIDToUEString(mt.formattype));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Format Tag: %d"), pWfex->wFormatTag);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Channels: %d"), pWfex->nChannels);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Samples Per Second: %ld"), pWfex->nSamplesPerSec);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Average Bytes Per Second: %ld"), pWfex->nAvgBytesPerSec);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Block Align: %d"), pWfex->nBlockAlign);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Bits Per Sample: %d"), pWfex->wBitsPerSample);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Extra Info Size: %d"), pWfex->cbSize);
	}
	else
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Unsupported or unknown format type."));
	}
}
void LogVideoMediaType(const AM_MEDIA_TYPE& mt)
{
	if (mt.formattype == FORMAT_VideoInfo && mt.pbFormat != nullptr)
	{
		VIDEOINFOHEADER* pVih = reinterpret_cast<VIDEOINFOHEADER*>(mt.pbFormat);

		REFERENCE_TIME avgFrameDuration = pVih->AvgTimePerFrame;
		double frameRate = 0.0;
		if (avgFrameDuration > 0)
			frameRate = 1.0e7 / avgFrameDuration;

		UE_LOG(LogDirectShowMedia, Log, TEXT("---- VIDEO MEDIA TYPE ----"));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Major Type: %s"), *GUIDToUEString(mt.majortype));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Sub Type: %s"), *GUIDToUEString(mt.subtype));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Fixed Size Samples: %d"), mt.bFixedSizeSamples);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Temporal Compression: %d"), mt.bTemporalCompression);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Sample Size: %ld"), mt.lSampleSize);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Format Type: %s"), *GUIDToUEString(mt.formattype));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Width: %ld"), pVih->bmiHeader.biWidth);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Height: %ld"), pVih->bmiHeader.biHeight);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Bit Count: %d"), pVih->bmiHeader.biBitCount);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Compression: %s"), *CompressionToUEString(pVih->bmiHeader.biCompression));
		UE_LOG(LogDirectShowMedia, Log, TEXT("Image Size: %ld"), pVih->bmiHeader.biSizeImage);
		UE_LOG(LogDirectShowMedia, Log, TEXT("Frame Rate: %.2f"), frameRate);
	}
	else
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Unsupported or unknown format type."));
	}
}


HRESULT SetPinMediaType(IPin* pPin, const GUID& subtype)
{
	if(!pPin)
		return -1;
		
	TComPtr<IEnumMediaTypes> pEnum = NULL;
	DShowMediaTypePtr pType;
	HRESULT hr = pPin->EnumMediaTypes(&pEnum);
	if (FAILED(hr))
	{
		return hr;
	}

	while (S_OK == pEnum->Next(1, pType, NULL))
	{
		if (pType->formattype == FORMAT_VideoInfo && pType->subtype == subtype)
		{
			hr = pPin->QueryAccept(pType);
			if (hr == S_OK)
			{
				break;
			}
		}
	}

	return hr;
}

bool IsPinConnected(IPin* Pin)
{
	if(!Pin)
		return false;

	TComPtr<IPin> testConnectedPin;
	return Pin->ConnectedTo(&testConnectedPin) >= 0;
}


bool GetPin(TComPtr<IBaseFilter> pFilter, PIN_DIRECTION PinDir, IPin** Pin, bool RequestUnconnected)
{
	TComPtr<IEnumPins> pEnum = NULL;
	TComPtr<IPin> pPin = NULL;

	if(!pFilter.IsValid())
		return false;
	
	pFilter->EnumPins(&pEnum);
	if (pEnum == NULL)
	{
		return false;
	}

	while(pEnum->Next(1, &pPin, NULL) == S_OK)
	{
		PIN_DIRECTION ThisPinDir;
		pPin->QueryDirection(&ThisPinDir);
		if (ThisPinDir == PinDir)
		{
			TComPtr<IPin> testConnectedPin;
			if(RequestUnconnected && IsPinConnected(pPin))
			{
				continue;	
			}
			
			// transfer ownership
			*Pin = pPin;
			(*Pin)->AddRef();			
			return true;
		}
	}
	return false;
}

bool GetPin(TComPtr<IBaseFilter> pFilter, PIN_DIRECTION PinDir, GUID MajorType, 
    GUID Category, IPin** Pin)
{
    TComPtr<IEnumPins> pEnum = NULL;
    TComPtr<IPin> pPin = NULL;

    if (!pFilter.IsValid())
        return false;

    HRESULT hr = pFilter->EnumPins(&pEnum);
    if (FAILED(hr) || pEnum == NULL)
    {
        return false;
    }

    while (pEnum->Next(1, &pPin, NULL) == S_OK)
    {
    	// test Direction
        PIN_DIRECTION ThisPinDir;
        hr = pPin->QueryDirection(&ThisPinDir);
        if (FAILED(hr))
        {
            continue;
        }

        if (ThisPinDir != PinDir)
			continue;

    	// Test Category
    	TComPtr<IKsPropertySet> pKs;
    	hr = pPin->QueryInterface(IID_PPV_ARGS(&pKs));
    	if (SUCCEEDED(hr))
    	{
    		DWORD cbReturned;
    		GUID PinCategory = GUID_NULL;

    		hr = pKs->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, NULL, 0, &PinCategory, sizeof(GUID), &cbReturned);
    		if (FAILED(hr) || PinCategory != Category)
    		{
    			continue;
    		}
    	}

    	// Test major
    	TComPtr<IEnumMediaTypes> pEnumMediaTypes;
    	hr = pPin->EnumMediaTypes(&pEnumMediaTypes);
    	if (SUCCEEDED(hr) && pEnumMediaTypes)
    	{
    		AM_MEDIA_TYPE* pMediaType;
    		while (pEnumMediaTypes->Next(1, &pMediaType, NULL) == S_OK)
    		{
    			if (pMediaType->majortype == MajorType)
    			{
    				// transfer ownership
    				*Pin = pPin;
    				(*Pin)->AddRef(); 
    				return true;
    			}
    		}
    	}
    	
    }
	return false;
}

bool GetPin(const FString& Url, const IID& clsidDeviceClass, const GUID& MajorType, PIN_DIRECTION PinDir, IPin** Pin)
{
	HRESULT HResult;
	VARIANT Name;
	TComPtr<ICreateDevEnum> DevEnum;
	TComPtr<IEnumMoniker> EnumMoniker;
	TComPtr<IMoniker> Moniker;
	TComPtr<IPropertyBag> PropertyBag;
	bool DeviceFound = false;
	
	//create an enumerator for video input devices
	HResult = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&DevEnum);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CoCreateInstance(CLSID_SystemDeviceEnum) in GetPin(): %d"), HResult);
		return false;
	}
	
	if(!DevEnum.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid DevEnum in GetPin()"));
		return false;
	}

	HResult = DevEnum->CreateClassEnumerator(clsidDeviceClass, &EnumMoniker, NULL);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CreateClassEnumerator() in GetPin(): %d"), HResult);
		return false;
	}
	
	if(!EnumMoniker.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid EnumMoniker in GetPin()"));
		return false;
	}
	
	while (EnumMoniker->Next(1, &Moniker, 0) == S_OK && !DeviceFound)
	{
		if(!Moniker.IsValid())
		{
			UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid Moniker in GetPin()"));
			continue;
		}
		
		HResult = Moniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&PropertyBag);
		if (HResult >= 0)
		{
			VariantInit(&Name);

			//get the description
			HResult = PropertyBag->Read(L"Description", &Name, 0);
			if (HResult < 0)
			{
				HResult = PropertyBag->Read(L"FriendlyName", &Name, 0);
			}

			if (HResult >= 0)
			{
				FString tempUrl = "";
				HResult = PropertyBag->Read(L"DevicePath", &Name, 0);
				if(HResult >= 0)
				{
					// Assuming the DevicePath is a BSTR (you may need to confirm this is the case for your specific device)
					BSTR bstrDevicePath = Name.bstrVal;

					// Convert BSTR to a wide string
					std::wstring wideDevicePath(bstrDevicePath);

					// Convert wide string to FString
					tempUrl = FString(wideDevicePath.c_str());
				}

				// Can occur from virtual devices which have no path, instead use name
				if(tempUrl.IsEmpty())
				{
					tempUrl = Name.bstrVal;
				}

				// Check Device URL
				if(!tempUrl.Equals(Url))
					continue;

				// Found device, now get media types
				TComPtr<IBaseFilter> pFilter = NULL;
				HResult = Moniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&pFilter);
				if (SUCCEEDED(HResult))
				{
					return GetPin(pFilter, PinDir, MajorType, PIN_CATEGORY_CAPTURE, Pin);
				}
				
			}
			VariantClear(&Name);
		}
	}

	return false;
}

bool TryGetAudioPinByFriendlyName(const FString& InFriendlyName, IPin** Pin)
{
	HRESULT HResult;
	VARIANT Name;
	TComPtr<ICreateDevEnum> DevEnum;
	TComPtr<IEnumMoniker> EnumMoniker;
	TComPtr<IMoniker> Moniker;
	TComPtr<IPropertyBag> PropertyBag;
	bool DeviceFound = false;
	
	//create an enumerator for video input devices
	HResult = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&DevEnum);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CoCreateInstance(CLSID_SystemDeviceEnum) in TryGetAudioPinByFriendlyName(): %d"), HResult);
		return false;
	}
	
	if(!DevEnum.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid DevEnum in TryGetAudioPinByFriendlyName()"));
		return false;
	}

	HResult = DevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &EnumMoniker, NULL);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CreateClassEnumerator() in TryGetAudioPinByFriendlyName(): %d"), HResult);
		return false;
	}
	
	if(!EnumMoniker.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid EnumMoniker in TryGetAudioPinByFriendlyName()"));
		return false;
	}
	
	while (EnumMoniker->Next(1, &Moniker, 0) == S_OK && !DeviceFound)
	{
		if(!Moniker.IsValid())
		{
			UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid Moniker in TryGetAudioPinByFriendlyName()"));
			continue;
		}
		
		HResult = Moniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&PropertyBag);
		if (HResult >= 0)
		{
			VariantInit(&Name);

			//get the description
			HResult = PropertyBag->Read(L"Description", &Name, 0);
			if (HResult < 0)
			{
				HResult = PropertyBag->Read(L"FriendlyName", &Name, 0);
			}

			if (HResult >= 0)
			{
				FString strFriendlyName = Name.bstrVal;
				if(!strFriendlyName.Contains(InFriendlyName, ESearchCase::IgnoreCase))
					continue;

				// Found device, now get media types
				TComPtr<IBaseFilter> pFilter = NULL;
				HResult = Moniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&pFilter);
				if (SUCCEEDED(HResult))
				{
					return GetPin(pFilter, PINDIR_OUTPUT, MEDIATYPE_Audio, PIN_CATEGORY_CAPTURE, Pin);
				}
				
			}
			VariantClear(&Name);
		}
	}

	return false;
}

bool GetDeviceFriendlyName(const FString& Url, const IID& clsidDeviceClass, FString& OutFriendlyName)
{
	HRESULT HResult;
	VARIANT Name;
	TComPtr<ICreateDevEnum> DevEnum;
	TComPtr<IEnumMoniker> EnumMoniker;
	TComPtr<IMoniker> Moniker;
	TComPtr<IPropertyBag> PropertyBag;
	bool DeviceFound = false;
	
	//create an enumerator for video input devices
	HResult = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&DevEnum);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CoCreateInstance(CLSID_SystemDeviceEnum) in GetDeviceFriendlyName(): %d"), HResult);
		return false;
	}	

	if(!DevEnum.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid DevEnum in GetDeviceFriendlyName()"));
		return false;
	}
	
	HResult = DevEnum->CreateClassEnumerator(clsidDeviceClass, &EnumMoniker, NULL);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CreateClassEnumerator() in GetDeviceFriendlyName(): %d"), HResult);
		return false;
	}

	if(!EnumMoniker.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid EnumMoniker in GetDeviceFriendlyName()"));
		return false;
	}
	
	while (EnumMoniker->Next(1, &Moniker, 0) == S_OK && !DeviceFound)
	{
		if(!Moniker.IsValid())
		{
			UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid Moniker in GetDeviceFriendlyName()"));
			continue;
		}
		
		HResult = Moniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&PropertyBag);
		if (HResult >= 0)
		{
			VariantInit(&Name);

			//get the description
			HResult = PropertyBag->Read(L"Description", &Name, 0);
			if (HResult < 0)
			{
				HResult = PropertyBag->Read(L"FriendlyName", &Name, 0);
				OutFriendlyName = Name.bstrVal;
			}

			if (HResult >= 0)
			{
				FString tempUrl = "";
				HResult = PropertyBag->Read(L"DevicePath", &Name, 0);
				if(HResult >= 0)
				{
					// Assuming the DevicePath is a BSTR (you may need to confirm this is the case for your specific device)
					BSTR bstrDevicePath = Name.bstrVal;

					// Convert BSTR to a wide string
					std::wstring wideDevicePath(bstrDevicePath);

					// Convert wide string to FString
					tempUrl = FString(wideDevicePath.c_str());
				}

				// Can occur from virtual devices which have no path, instead use name
				if(tempUrl.IsEmpty())
				{
					tempUrl = Name.bstrVal;
				}

				// Check Device URL
				if(!tempUrl.Equals(Url))
					continue;
				
				return true;
				
			}
			VariantClear(&Name);
		}
	}

	return false;
}

EMediaAudioSampleFormat GetAudioSampleFormatBits(const WAVEFORMATEX* wfex)
{
	switch (wfex->wBitsPerSample)
	{
	case 8:
		return EMediaAudioSampleFormat::Int8;
	case 16:
		return EMediaAudioSampleFormat::Int16;
	case 32:
		return EMediaAudioSampleFormat::Float;
		
	default:
		return EMediaAudioSampleFormat::Undefined;
	}
}
