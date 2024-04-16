// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectShowMedia.h"
#include "Windows/AllowWindowsPlatformTypes.h" 
#include <dshow.h>

#include "DirectShowMediaCommon.h"
#include "IMediaModule.h"
#include "Microsoft/COMPointer.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "Player/DirectShowMediaPlayer.h"

#define LOCTEXT_NAMESPACE "FDirectShowMediaModule"

DEFINE_LOG_CATEGORY(LogDirectShowMedia);

void FDirectShowMediaModule::StartupModule()
{
	FPlatformMisc::CoInitialize();

	// register capture device support
	auto MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

	if (MediaModule != nullptr)
	{
		MediaModule->RegisterCaptureSupport(*this);
	}

	Initialized = true;
}

void FDirectShowMediaModule::ShutdownModule()
{
	FPlatformMisc::CoUninitialize();
}

void FDirectShowMediaModule::EnumerateAudioCaptureDevices(TArray<FMediaCaptureDeviceInfo>& OutDeviceInfos)
{
	HRESULT HResult;
	VARIANT Info;
	TComPtr<ICreateDevEnum> DevEnum;
	TComPtr<IEnumMoniker> EnumMoniker;
	TComPtr<IMoniker> Moniker;
	TComPtr<IPropertyBag> PropertyBag;
	
	//create an enumerator for video input devices
	HResult = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&DevEnum);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CoCreateInstance(CLSID_SystemDeviceEnum) in EnumerateAudioCaptureDevices: %d"), HResult);
		return;
	}	

	HResult = DevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &EnumMoniker, NULL);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CreateClassEnumerator() in EnumerateAudioCaptureDevices: %d"), HResult);
		return;
	}

	if (HResult == S_FALSE)
	{
		return;
	}
	
	while (EnumMoniker->Next(1, &Moniker, 0) == S_OK)
	{
		HResult = Moniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&PropertyBag);
		if (HResult >= 0)
		{
			VariantInit(&Info);

			FMediaCaptureDeviceInfo NewInfo;
			bool InfoValid = true;
			NewInfo.Type = EMediaCaptureDeviceType::Audio;

			HResult = PropertyBag->Read(L"FriendlyName", &Info, 0);
			if(HResult == S_OK)
			{
				// convert to FText
				BSTR infoBSTR = Info.bstrVal;
				std::wstring infowstring(infoBSTR);
				NewInfo.DisplayName = FText::FromString(FString(infowstring.c_str()));
			}
			else
			{
				//get the description  "Description"
				HResult = PropertyBag->Read(L"Description", &Info, 0);
				if(HResult == S_OK)
				{
					// convert to FString
					BSTR infoBSTR = Info.bstrVal;
					std::wstring infowstring(infoBSTR);
					NewInfo.DisplayName = FText::FromString(FString(infowstring.c_str()));
				}
				else
					InfoValid = false;
			}
						
			FString tempUrl = "";
			HResult = PropertyBag->Read(L"DevicePath", &Info, 0);
			if(HResult >= 0)
			{
				// convert to FString
				BSTR infoBSTR = Info.bstrVal;
				std::wstring infowstring(infoBSTR);
				NewInfo.Url = FString(infowstring.c_str());
			}

			// Can occur from virtual devices which have no path, instead use name
			if(NewInfo.Url.IsEmpty())
			{
				NewInfo.Url = NewInfo.DisplayName.ToString();
			}

			if(InfoValid)
				OutDeviceInfos.Add(NewInfo);
			
			VariantClear(&Info);

		}

	}
}

void FDirectShowMediaModule::EnumerateVideoCaptureDevices(TArray<FMediaCaptureDeviceInfo>& OutDeviceInfos)
{
	HRESULT HResult;
	VARIANT Info;
	TComPtr<ICreateDevEnum> DevEnum;
	TComPtr<IEnumMoniker> EnumMoniker;
	TComPtr<IMoniker> Moniker;
	TComPtr<IPropertyBag> PropertyBag;
	
	//create an enumerator for video input devices
	HResult = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&DevEnum);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CoCreateInstance(CLSID_SystemDeviceEnum) in EnumerateVideoCaptureDevices: %d"), HResult);
		return;
	}	

	if(!DevEnum.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid DevEnum in EnumerateVideoCaptureDevices"));
		return;
	}
	
	HResult = DevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &EnumMoniker, NULL);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Failed to CreateClassEnumerator() in EnumerateVideoCaptureDevices: %d"), HResult);
		return;
	}

	if(!EnumMoniker.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid EnumMoniker in EnumerateVideoCaptureDevices"));
		return;
	}

	while (EnumMoniker->Next(1, &Moniker, 0) == S_OK)
	{
		if(!Moniker.IsValid())
		{
			UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid Moniker in EnumerateVideoCaptureDevices"));
			return;
			
		}
		HResult = Moniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&PropertyBag);
		if (HResult >= 0)
		{			
			VariantInit(&Info);

			FMediaCaptureDeviceInfo NewInfo;
			bool InfoValid = true;
			NewInfo.Type = EMediaCaptureDeviceType::Video;  

			HResult = PropertyBag->Read(L"FriendlyName", &Info, 0);
			if(HResult == S_OK)
			{
				// convert to FText
				BSTR infoBSTR = Info.bstrVal;
				std::wstring infowstring(infoBSTR);
				NewInfo.DisplayName = FText::FromString(FString(infowstring.c_str()));
			}
			else
			{
				//get the description  "Description"
				HResult = PropertyBag->Read(L"Description", &Info, 0);
				if(HResult == S_OK)
				{
					// convert to FString
					BSTR infoBSTR = Info.bstrVal;
					std::wstring infowstring(infoBSTR);
					NewInfo.DisplayName = FText::FromString(FString(infowstring.c_str()));
				}
				else
					InfoValid = false;
			}
						
			FString tempUrl = "";
			HResult = PropertyBag->Read(L"DevicePath", &Info, 0);
			if(HResult >= 0)
			{
				// convert to FString
				BSTR infoBSTR = Info.bstrVal;
				std::wstring infowstring(infoBSTR);
				NewInfo.Url = FString(infowstring.c_str());
			}

			// Can occur from virtual devices which have no path, instead use name
			if(NewInfo.Url.IsEmpty())
			{
				NewInfo.Url = NewInfo.DisplayName.ToString();
			}

			TComPtr<IPin> sourcePin;
			if(!GetPin(NewInfo.Url, CLSID_VideoInputDeviceCategory,MEDIATYPE_Video,PINDIR_OUTPUT, &sourcePin))
			{
				InfoValid = false;
			}

			if(InfoValid)
				OutDeviceInfos.Add(NewInfo);
			
			VariantClear(&Info);
		}
	}
}

TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> FDirectShowMediaModule::CreatePlayer(ISinkMediaEvent& EventSink)
{
	if (!Initialized)
	{
		return nullptr;
	}

	return MakeShareable(new FDirectShowMediaPlayer(EventSink));
}
#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FDirectShowMediaModule, DirectShowMedia)