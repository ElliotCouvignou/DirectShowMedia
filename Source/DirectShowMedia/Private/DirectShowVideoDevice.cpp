

#include "DirectShowVideoDevice.h"

#include "DirectShow/DirectShow-1.0.0/src/Public/mtype.h"

#include "DirectShowMedia.h"
#include "DirectShowMediaCommon.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "uuids.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "DirectShowCallbackHandler.h"

#define LOCTEXT_NAMESPACE "DirectShowMediaTracks"

#define DEMUX_VIDEO_PINNAME "Video Demux"
#define DEMUX_AUDIO_PINNAME "Video Demux"

CLSID const CLSID_MSDTV = {0x212690FB, 0x83E5, 0x4526, 0x8F, 0xD7, 0x74, 0x47, 0x8B, 0x79, 0x39, 0xCD};


FDirectShowVideoDevice::FDirectShowVideoDevice()
	:bHasAudio(false),
	CurrentSample(nullptr),
	CurrentBuffer(nullptr),
	CurrentFPS(0.f),
	CurrentSubtype(MEDIASUBTYPE_None),
	CurrentSelectedAudioTrack(INDEX_NONE),
	CurrentSelectedCaptionTrack(INDEX_NONE),
	CurrentSelectedMetadataTrack(INDEX_NONE),
	CurrentSelectedVideoTrack(INDEX_NONE),
	Demux(nullptr)
{
	// Filtername = (WCHAR*)FMemory::Malloc(MAX_DEVICE_NAME * sizeof(WCHAR));
	// AudioFiltername = (WCHAR*)FMemory::Malloc(MAX_DEVICE_NAME * sizeof(WCHAR));
	//
	// FMemory::Memset(Filtername, 0, MAX_DEVICE_NAME * sizeof(WCHAR));
	
	VideoCallbackhandler = new FDirectShowCallbackHandler();
	AudioCallbackhandler = new FDirectShowCallbackHandler();
}

FDirectShowVideoDevice::~FDirectShowVideoDevice()
{
	//FMemory::Free(Filtername);

	Stop();

	if(VideoCallbackhandler)
	{
		delete VideoCallbackhandler;
		VideoCallbackhandler = nullptr;
	}
	if(AudioCallbackhandler)
	{
		delete AudioCallbackhandler;
		AudioCallbackhandler = nullptr;
	}
}

bool FDirectShowVideoDevice::InitializeGraph()
{
	HRESULT HResult = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IFilterGraph2,
									   (void**)&Graph);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to create filter graph: %x"), HResult);
		return false;
	}

	HResult = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void**)&Capture);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to set filter graph: %x"), HResult);
		return false;
	}

	HResult = Graph->QueryInterface(IID_IMediaControl, (void**)&Control);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to Graph->QueryInterface(IID_IMediaControl, (void**)&Control): %x"), HResult);
		return false;
	}

	// HResult = CoCreateInstance(CLSID_MPEG2Demultiplexer, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void **)&Demux);
	// if (HResult != S_OK)
	// {
	// 	UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to create demux: %x "), HResult)
	// 	return false;
	// }
	
	// HResult = CoCreateInstance(CLSID_SystemClock, NULL, CLSCTX_INPROC_SERVER, IID_IReferenceClock, (void**)&Clock);
	// if (HResult != S_OK)
	// {
	// 	UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to create systemClock: %x "), HResult)
	// 	return false;
	// }
	//
	// TComPtr<IMediaFilter> pMediaFilter;
	// HResult = Graph->QueryInterface(IID_IMediaFilter, (void**)&pMediaFilter);
	// if (FAILED(HResult))
	// {
	// 	UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to Graph->QueryInterface(IID_IMediaFilter): %x "), HResult)
	// 	return false;
	// }
	// HResult = pMediaFilter->SetSyncSource(Clock);
	// if (FAILED(HResult))
	// {
	// 	UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to pMediaFilter->SetSyncSource(Clock): %x "), HResult)
	// 	return false;
	// }

	Capture->SetFiltergraph(Graph);

	return true;
}

void FDirectShowVideoDevice::SetAudioBuffer(float delayMs)
{
	if(!AudioSourcefilter.IsValid())
		return;
	
	TComPtr<IPin> Pin;
	if(!GetPin(AudioSourcefilter, PINDIR_OUTPUT, &Pin))
		return;

	TComPtr<IAMStreamConfig> StreamConfig;
	HRESULT hr = Pin->QueryInterface(IID_IAMStreamConfig, (void**)&StreamConfig);
	if(hr != S_OK)
		return;

	TComPtr<IAMBufferNegotiation> Negotiation;
	hr = Pin->QueryInterface(IID_IAMBufferNegotiation, (void**)&Negotiation);
	if(hr != S_OK)
		return;

	AM_MEDIA_TYPE* mt = nullptr;
	if(StreamConfig->GetFormat(&mt) != S_OK)
		return;

	if (mt->formattype != FORMAT_WaveFormatEx && mt->cbFormat != sizeof(WAVEFORMATEX))
		return;

	WAVEFORMATEX *wfex = (WAVEFORMATEX *)mt->pbFormat;

	//LogMediaType2(*mt);

	ALLOCATOR_PROPERTIES Properties;
	Properties.cBuffers = -1;
	Properties.cbBuffer = wfex->nAvgBytesPerSec * (delayMs / 1000.f);
	Properties.cbAlign = -1;
	Properties.cbPrefix = -1;
	hr = Negotiation->SuggestAllocatorProperties(&Properties);

	UE_LOG(LogDirectShowMedia, Log, TEXT("Allocating cbBuffer size: %ld"), Properties.cbBuffer );
	if(hr != S_OK)
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to set audio allocator properties: %d"), hr);
	}
}

bool FDirectShowVideoDevice::Initialize(const FString& Url, AM_MEDIA_TYPE* Format, AM_MEDIA_TYPE* AudioFormat)
{
	HRESULT HResult;
	VARIANT Name;
	TComPtr<ICreateDevEnum> DevEnum;
	TComPtr<IEnumMoniker> EnumMoniker;
	TComPtr<IMoniker> Moniker;
	TComPtr<IPropertyBag> PropertyBag;
	bool DeviceFound = false;

	Stop();
	bIsInitialized = false;
	
	Width = 0;
	Height = 0;
	CurrentSubtype = MEDIASUBTYPE_None;
	CurrentFPS = 0;

	if(!InitializeGraph())
	{
		return false;
	}
	
	//create an enumerator for video input devices
	HResult = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&DevEnum);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to CoCreateInstance(CLSID_SystemDeviceEnum): %d"), HResult);
		return false;;
	}

	if(!DevEnum.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid DevEnum in FDirectShowVideoDevice::Initialize()"));
		return false;
	}

	HResult = DevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &EnumMoniker, NULL);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to DevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory): %d"), HResult);
		return false;;
	}

	if(!EnumMoniker.IsValid())
	{
		UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid EnumMoniker in FDirectShowVideoDevice::Initialize()"));
		return false;
	}
	
	while (EnumMoniker->Next(1, &Moniker, 0) == S_OK && !DeviceFound)
	{
		if(!Moniker.IsValid())
		{
			UE_LOG(LogDirectShowMedia, Warning, TEXT("Invalid Moniker in FDirectShowVideoDevice::Initialize()"));
			return false;
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
				//Initialize the VideoDevice struct
				Friendlyname = Name.bstrVal;
				
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

				URL = Url; 
				
				//add a filter for the device
				HResult = Graph->AddSourceFilterForMoniker(Moniker, 0, GetData(Friendlyname), &VideoSourcefilter);
				if (HResult != S_OK)
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to Graph->AddSourceFilterForMoniker: %d"), HResult);
					continue;
				}
				
				//create a samplegrabber filter for the device
				HResult = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&VideoSamplegrabberfilter);
				if (HResult < 0)
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to query samplegrabber from samplegrabberfilter: %d"), HResult);
					continue;
				}
				
				//set mediatype on the samplegrabber
				HResult = VideoSamplegrabberfilter->QueryInterface(IID_ISampleGrabber, (void**)&VideoSamplegrabber);
				if (HResult != S_OK)
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to query samplegrabber from samplegrabberfilter: %d"), HResult);
					continue;
				}

				FString VideoSGName = Friendlyname + " SG";
				Graph->AddFilter(VideoSamplegrabberfilter, GetData(VideoSGName));

				//set the media type
				DShowMediaType tempmt(*Format);
				TComPtr<IPin> Pin;
				if (GetPin(VideoSourcefilter, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &Pin))
				{
					TComPtr<IAMStreamConfig> StreamConfig;
					HResult = Pin->QueryInterface(IID_IAMStreamConfig, (void**)&StreamConfig);
					if (SUCCEEDED(HResult) && StreamConfig.IsValid())
					{
						//LogMediaType(tempmt);
						HResult = StreamConfig->SetFormat(Format);
						if(FAILED(HResult) && HResult != E_NOTIMPL)
						{
							UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to set format format: %d"), HResult);
							continue;
						}
					}
				}
				else
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to GetPin(Sourcefilter, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &Pin)"));
					continue;
				}
				
				if(tempmt->subtype == MEDIASUBTYPE_MJPG || tempmt->subtype == MEDIASUBTYPE_H264)
				{
					tempmt->subtype = MEDIASUBTYPE_ARGB32;
				}
				
				HResult = VideoSamplegrabber->SetMediaType(tempmt);
				if (HResult != S_OK)
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to set SetMediaType: %d"), HResult);
					continue;
				}
				
				// Setup demux Video Input Pin
				if(Demux)
				{
					TComPtr<IMpeg2Demultiplexer> Demuxiplier;
					HResult = Demux->QueryInterface(IID_IMpeg2Demultiplexer, (void**)&Demuxiplier);
					if(HResult != S_OK)
					{
						UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to QueryInterface(IID_IMpeg2Demultiplexer) on audio demux"));
						continue;
					}
					
					TComPtr<IPin> pin;
				
					// use tempmt as its suited for output of decoders
					VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER*>(tempmt->pbFormat);
					VIDEOINFOHEADER *Formatvih = reinterpret_cast<VIDEOINFOHEADER*>(Format->pbFormat);
					vih->bmiHeader.biSize = sizeof(vih->bmiHeader);
					vih->bmiHeader.biWidth = Formatvih->bmiHeader.biWidth;
					vih->bmiHeader.biHeight = Formatvih->bmiHeader.biHeight;
					vih->bmiHeader.biCompression = Formatvih->bmiHeader.biCompression;
					vih->rcSource.right = Formatvih->rcSource.right;
					vih->rcSource.bottom = Formatvih->rcSource.bottom;
					vih->AvgTimePerFrame = Formatvih->AvgTimePerFrame;

					if (!vih->bmiHeader.biCompression)
					{
						UE_LOG(LogDirectShowMedia, Error, TEXT("Invalid video format when making demux Video INput pin"));
						return false;
					}

					tempmt->majortype = MEDIATYPE_Audio;
					tempmt->subtype = Format->subtype;
					tempmt->formattype = FORMAT_WaveFormatEx;
					tempmt->bTemporalCompression = true;
					
					FString VideoDemuxName(DEMUX_VIDEO_PINNAME);
					HResult = Demuxiplier->CreateOutputPin(Format, GetData(VideoDemuxName), &pin);
					if(HResult != S_OK)
					{
						UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to Demuxiplier->CreateOutputPin(tempmt, GetData(VideoDemuxName), &pin)"));
						continue;
					}
				}
				
				// Setup Audio components 
				if(bHasAudio && !AudioDeviceFriendlyName.IsEmpty())
				{
					bHasAudio = TryInitializeAudio(AudioDeviceFriendlyName, AudioFormat);
				}

				// setup video connections
				if( Format->subtype == MEDIASUBTYPE_MJPG)
					SetupMjpegDecompressorGraph();
				else if( Format->subtype == MEDIASUBTYPE_H264)
					SetupH264Graph();
				else
					ConnectVideoGraph();
					
				//add the callback to the sample grabber
				HResult = VideoSamplegrabber->SetCallback(VideoCallbackhandler, 0);
				if (HResult != S_OK)
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to set callback on samplegrabber: %d"), HResult);
					continue;
				}
				
				DeviceFound = true;
				
				//set the render path
				// these can fail and everything will be fine if device was called to change format TODO: figure out why
				HResult = Capture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, VideoSourcefilter, NULL, VideoSamplegrabberfilter);
				if (HResult < 0)
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to call Capture->RenderStream: %d"), HResult)
					//continue;
				}
				if(bHasAudio)
				{
					HResult = Capture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Audio, AudioSourcefilter, NULL, AudioSamplegrabberfilter);
					if (HResult < 0)
					{
						UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to call Capture->RenderStream: %d"), HResult)
						//continue;
					}
				}
				
			}
			VariantClear(&Name);
		}
	}

	if(DeviceFound)
	{
		HResult = Control->Run();
		if (HResult != S_OK)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to Control->Run() %d"), HResult);
		}
		
		bIsInitialized = true;
		
		// look up the media type:
		TComPtr<IPin> Pin;
		if(GetPin(VideoSourcefilter, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &Pin))
		{
			DShowMediaType cmt;
			HResult = Pin->ConnectionMediaType(cmt);
			if (HResult == S_OK)
			{
				UE_LOG(LogDirectShowMedia, Log, TEXT("\nOpening Devices... Printing Media Types:\n\n"))
				//LogVideoMediaType(*cmt);
				if (cmt->formattype == FORMAT_VideoInfo)
				{
					const VIDEOINFOHEADER * VideoInfo = reinterpret_cast<VIDEOINFOHEADER*>(cmt->pbFormat);
					Width = VideoInfo->bmiHeader.biWidth;
					Height = VideoInfo->bmiHeader.biHeight;
					CurrentFPS = 1.0 / (VideoInfo->AvgTimePerFrame * 1.0e-7);
				}
				CurrentSubtype = cmt->subtype;
			}
			else
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to get mediatype after initialization: %d"), HResult)
			}

			// Re-collect data (could have FPS changed)
			//FillFormatData(Pin);
		}

		if(bHasAudio)
		{
			//Control->Run();

			TComPtr<IPin> AudioPin;
			if(GetPin(AudioSourcefilter, PINDIR_OUTPUT, &AudioPin))
			{
				DShowMediaType cmt;
				HResult = AudioPin->ConnectionMediaType(cmt);
				if (HResult == S_OK)
				{
					//LogAudioMediaType(*cmt);
					if (cmt->formattype == FORMAT_WaveFormatEx)
					{
						const WAVEFORMATEX * AudioInfo = reinterpret_cast<WAVEFORMATEX*>(cmt->pbFormat);
						BitsPerSample = AudioInfo->wBitsPerSample;
						NumChannels = AudioInfo->nChannels;
						SampleRate = AudioInfo->nSamplesPerSec;
						SampleFormat = GetAudioSampleFormatBits(AudioInfo);
					}
					CurrentAudioSubtype = cmt->subtype;
				}
				else
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to get mediatype after initialization: %d"), HResult)
				}
			
			}
		
			// FillAudioFormatData(Pin);
		}
	}
	else
	{
		this->Stop();
	}

	return DeviceFound;
}

bool FDirectShowVideoDevice::TryInitializeAudio(const FString& InFriendlyName, AM_MEDIA_TYPE* Format)
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
		UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to CoCreateInstance(CLSID_SystemDeviceEnum): %d"), HResult);
		return false;;
	}	

	HResult = DevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &EnumMoniker, NULL);
	if (HResult < 0)
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to DevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory): %d"), HResult);
		return false;;
	}
	
	while (EnumMoniker->Next(1, &Moniker, 0) == S_OK && !DeviceFound)
	{
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
				//Initialize the VideoDevice struct
				FString strFriendlyName = Name.bstrVal;
				if(!strFriendlyName.Contains(InFriendlyName, ESearchCase::IgnoreCase))
					continue;
				
				AudioDeviceFriendlyName = strFriendlyName;

				DeviceFound = true;
				
				//add a filter for the device
				HResult = Graph->AddSourceFilterForMoniker(Moniker, 0, GetData(AudioDeviceFriendlyName), &AudioSourcefilter);
				if (HResult != S_OK)
				{
					continue;
				}
				
				//create a samplegrabber filter for the device
				HResult = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&AudioSamplegrabberfilter);
				if (HResult < 0)
				{
					continue;
				}

				//set mediatype on the samplegrabber
				HResult = AudioSamplegrabberfilter->QueryInterface(IID_ISampleGrabber, (void**)&AudioSamplegrabber);
				if (HResult != S_OK)
				{
					continue;
				}

				FString AudioSGName = AudioDeviceFriendlyName + " SG";
				Graph->AddFilter(AudioSamplegrabberfilter, GetData(AudioSGName));
				
				//set the media type
				if(Format)
				{
					TComPtr<IPin> Pin;
					if (GetPin(AudioSourcefilter, PINDIR_OUTPUT, &Pin))
					{
						TComPtr<IAMStreamConfig> StreamConfig;
						HResult = Pin->QueryInterface(IID_IAMStreamConfig, (void**)&StreamConfig);
						if (SUCCEEDED(HResult) && StreamConfig.IsValid())
						{
							HResult = StreamConfig->SetFormat(Format);
							if(FAILED(HResult) && HResult != E_NOTIMPL)
							{
								UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to set format format: %d"), HResult);
								continue;
							}
						}
					}
					else
					{
						UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to GetPin(Sourcefilter, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &Pin)"));
						continue;
					}
				}

				// Fix audio delay through custom bufffer size
				SetAudioBuffer(20);
				
				//Samplegrabber->SetBufferSamples(true);

				//add the callback to the sample grabber
				HResult = AudioSamplegrabber->SetCallback(AudioCallbackhandler, 0);
				if (HResult != S_OK)
				{
					continue;
				}

				// Setup demux Audio Input Pin
				if(Demux)
				{
					TComPtr<IMpeg2Demultiplexer> Demuxiplier;
					HResult = Demux->QueryInterface(IID_IMpeg2Demultiplexer, (void**)&Demuxiplier);
					if(HResult != S_OK)
					{
						UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to QueryInterface(IID_IMpeg2Demultiplexer) on audio demux"));
						continue;
					}
					
					TComPtr<IPin> pin;
				
					// use tempmt as its suited for output of decoders
					DShowMediaType tempmt(*Format);
					WAVEFORMATEX *wfex = reinterpret_cast<WAVEFORMATEX*>(tempmt->pbFormat);
					WAVEFORMATEX *Formatwfex = reinterpret_cast<WAVEFORMATEX*>(Format->pbFormat);
					wfex->wFormatTag = Formatwfex->wFormatTag;
					wfex->nChannels = 2;
					wfex->nSamplesPerSec = Formatwfex->nSamplesPerSec;
					wfex->wBitsPerSample = 16;

					if (!wfex->wFormatTag)
					{
						UE_LOG(LogDirectShowMedia, Error, TEXT("CreateDemuxAudioPin: Invalid audio format"));
						return false;
					}

					tempmt->majortype = MEDIATYPE_Audio;
					tempmt->subtype = Format->subtype;
					tempmt->formattype = FORMAT_WaveFormatEx;
					tempmt->bTemporalCompression = true;
					
					FString AudioDemuxName(DEMUX_AUDIO_PINNAME);

					//UE_LOG(LogDirectShowMedia, Log, TEXT("\n\n	Audio!! Creating Audio Demux Pin... Format:\n"));
					//LogAudioMediaType(*Format);
					HResult = Demuxiplier->CreateOutputPin(Format, GetData(AudioDemuxName), &pin);
					if(HResult != S_OK)
					{
						UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to Demuxiplier->CreateOutputPin(tempmt, GetData(AudioDemuxName), &pin): %d"), HResult);
						continue;
					}
				}
				ConnectAudioGraph();
				
				//set the render path
				// HResult = Capture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Audio, AudioSourcefilter, NULL, AudioSamplegrabberfilter);
				// if (HResult < 0)
				// {
				// 	UE_LOG(LogDirectShowMedia, Error, TEXT("Audio!! Failed to call Capture->RenderStream: %d"), HResult)
				// 	continue;
				// }
			}
			VariantClear(&Name);
		}
	}

	return DeviceFound;
}


void FDirectShowVideoDevice::FillVideoFormatData(IPin* SourcePin)
{
	if(!SourcePin)
		return;
	
	uint32 selectedIndex = -1;

	VideoTracks.Empty();

	// Query for IAMStreamConfig
	TComPtr<IAMStreamConfig> StreamConfig;
	HRESULT hr = SourcePin->QueryInterface(IID_IAMStreamConfig, (void**)&StreamConfig);
	if (SUCCEEDED(hr) && StreamConfig)
	{
		// Get current media type
		DShowMediaTypePtr cmt;
		hr = StreamConfig->GetFormat(cmt);
		if(hr != S_OK)
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("cant get current media type to fill format data: %d"), hr)
		}

		int32 TrackIndex = VideoTracks.AddDefaulted();
		FDShowTrack* Track = &VideoTracks[TrackIndex];

		Track->DisplayName = FText::Format(LOCTEXT("UnnamedStreamFormat", "Unnamed Track (Stream {0})"), FText::AsNumber((uint32)TrackIndex));
		
		// use StreamConfig to query the formats/caabilities.
		int iCount = 0, iSize = 0;
		hr = StreamConfig->GetNumberOfCapabilities(&iCount, &iSize);
		
		if (SUCCEEDED(hr) && iSize == sizeof(VIDEO_STREAM_CONFIG_CAPS))
		{
			for (int iFormat = 0; iFormat < iCount; iFormat++)
			{
				DShowMediaTypePtr pmt;
				VIDEO_STREAM_CONFIG_CAPS scc;
				hr = StreamConfig->GetStreamCaps(iFormat, pmt, (BYTE*)&scc);
       
				if (SUCCEEDED(hr) && pmt.IsValid() && pmt->formattype == FORMAT_VideoInfo)
				{
					if(!TrySetupVideoTrack(*Track,pmt, cmt, &scc))
						continue;
					
					VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
					VIDEOINFOHEADER* cvih = (VIDEOINFOHEADER*)cmt->pbFormat;
					if(FMath::Abs(vih->bmiHeader.biWidth - cvih->bmiHeader.biWidth) < 1.f && FMath::Abs(vih->bmiHeader.biHeight - cvih->bmiHeader.biHeight) < 1.f
						&& pmt->subtype == cmt->subtype)
					{
						selectedIndex = Track->Formats.Num() - 1;
					}
				}
			}
		}
		else if (hr == E_NOTIMPL)
		{
			// elgato devices implemented in this fashion i guess
			TComPtr<IEnumMediaTypes> MediaTypes;
			DShowMediaTypePtr pmt;
			if (SUCCEEDED(SourcePin->EnumMediaTypes(&MediaTypes))) {
				ULONG count = 0;

				while (MediaTypes->Next(1, pmt, &count) == S_OK)
				{
					if (SUCCEEDED(hr) && pmt.IsValid() && pmt->formattype == FORMAT_VideoInfo)
					{
						if(!TrySetupVideoTrack(*Track,pmt, cmt))
							continue;
						
						VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
						VIDEOINFOHEADER* cvih = (VIDEOINFOHEADER*)cmt->pbFormat;
						if(FMath::Abs(vih->bmiHeader.biWidth - cvih->bmiHeader.biWidth) < 1.f && FMath::Abs(vih->bmiHeader.biHeight - cvih->bmiHeader.biHeight) < 1.f
							&& pmt->subtype == cmt->subtype)
						{
							selectedIndex = Track->Formats.Num() - 1;
						}
					}
				}
			}
		}
	}
	
	if(VideoTracks.Num() > 0)
		VideoTracks[0].SelectedFormat = selectedIndex;

	// both checks seem different so can never bee too sure
	if(OnVideoTracksUpdated.IsBound())
	{
		OnVideoTracksUpdated.ExecuteIfBound(selectedIndex);
	}
}

void FDirectShowVideoDevice::FillAudioFormatData(IPin* SourcePin)
{
	if(!SourcePin)
		return;
	
	uint32 selectedIndex = -1;

	AudioTracks.Empty();

	// Query for IAMStreamConfig
	TComPtr<IAMStreamConfig> StreamConfig;
	HRESULT hr = SourcePin->QueryInterface(IID_IAMStreamConfig, (void**)&StreamConfig);
	if (SUCCEEDED(hr) && StreamConfig)
	{
		// Get current media type
		DShowMediaTypePtr cmt;
		hr = StreamConfig->GetFormat(cmt);
		if(hr != S_OK)
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("cant get current media type to fill format data: %d"), hr)
		}
		
		int32 TrackIndex = AudioTracks.AddDefaulted();
		FDShowTrack* Track = &AudioTracks[TrackIndex];

		Track->DisplayName = FText::Format(LOCTEXT("UnnamedStreamFormat", "Unnamed Track (Stream {0})"), FText::AsNumber((uint32)TrackIndex));
		
		// use StreamConfig to query the formats/caabilities.
		int iCount = 0, iSize = 0;
		hr = StreamConfig->GetNumberOfCapabilities(&iCount, &iSize);
		
		if (SUCCEEDED(hr) && iSize == sizeof(AUDIO_STREAM_CONFIG_CAPS))
		{
			for (int iFormat = 0; iFormat < iCount; iFormat++)
			{
				DShowMediaTypePtr pmt;
				AUDIO_STREAM_CONFIG_CAPS scc;
				hr = StreamConfig->GetStreamCaps(iFormat, pmt, (BYTE*)&scc);
       
				if (SUCCEEDED(hr) && pmt.IsValid() && pmt->formattype == FORMAT_WaveFormatEx)
				{
					if(!TrySetupAudioTrack(*Track,pmt, &scc))
						continue;
					WAVEFORMATEX* wfex = (WAVEFORMATEX*)pmt->pbFormat;
					WAVEFORMATEX* cmtwfex = (WAVEFORMATEX*)cmt->pbFormat;
					
					if(pmt->subtype == cmt->subtype &&  cmtwfex->nSamplesPerSec == wfex->nSamplesPerSec &&
							cmtwfex->nChannels == wfex->nChannels &&
							cmtwfex->wBitsPerSample == wfex->wBitsPerSample)
					{
						selectedIndex = Track->Formats.Num() - 1;
					}
				}
			}
		}
		else if (hr == E_NOTIMPL)
		{
			// elgato devices implemented in this fashion i guess
			TComPtr<IEnumMediaTypes> MediaTypes;
			DShowMediaTypePtr pmt;
			if (SUCCEEDED(SourcePin->EnumMediaTypes(&MediaTypes))) {
				ULONG count = 0;

				while (MediaTypes->Next(1, pmt, &count) == S_OK)
				{
					if (SUCCEEDED(hr) && pmt.IsValid() && pmt->formattype == FORMAT_WaveFormatEx)
					{
						if(!TrySetupAudioTrack(*Track,pmt))
							continue;
						WAVEFORMATEX* wfex = (WAVEFORMATEX*)pmt->pbFormat;
						WAVEFORMATEX* cmtwfex = (WAVEFORMATEX*)cmt->pbFormat;
					
						if(cmtwfex->nSamplesPerSec == wfex->nSamplesPerSec &&
							cmtwfex->nChannels == wfex->nChannels &&
							cmtwfex->wBitsPerSample == wfex->wBitsPerSample)
						{
							selectedIndex = Track->Formats.Num() - 1;
						}
					}
				}
			}
		}
	}
	
	if(AudioTracks.Num() > 0)
		AudioTracks[0].SelectedFormat = selectedIndex;

	// both checks seem different so can never bee too sure
	if(OnAudioTracksUpdated.IsBound())
	{
		OnAudioTracksUpdated.ExecuteIfBound(selectedIndex);
	}
}

void FDirectShowVideoDevice::FillFormatDataFromURL(const FString& Url, const FString& OptionalAudioDeviceName)
{
	TComPtr<IPin> sourcePin;
	if(GetPin(Url, CLSID_VideoInputDeviceCategory,MEDIATYPE_Video,PINDIR_OUTPUT, &sourcePin))
	{
		FillVideoFormatData(sourcePin);
	}
	if(!OptionalAudioDeviceName.IsEmpty() && !OptionalAudioDeviceName.Equals("Auto"))
	{
		if(OptionalAudioDeviceName.Equals("None"))
		{
			bHasAudio = false;
			AudioDeviceFriendlyName = "";
			return;
		}
		
		if(TryGetAudioPinByFriendlyName(*OptionalAudioDeviceName, &sourcePin))
		{
			FillAudioFormatData(sourcePin);
			AudioDeviceFriendlyName = OptionalAudioDeviceName;
			bHasAudio = true;
		}
		else
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to fill AUdio format data for device: %s"), *OptionalAudioDeviceName)
		}
	}
	else
	{
		// try find device by video's friendlyname
		FString videoDeviceName;
		if(!GetDeviceFriendlyName(Url, CLSID_VideoInputDeviceCategory, videoDeviceName))
			return;

		if(TryGetAudioPinByFriendlyName(videoDeviceName, &sourcePin))
		{
			FillAudioFormatData(sourcePin);
			AudioDeviceFriendlyName = videoDeviceName;
			bHasAudio = true;
		}
	}
}

bool FDirectShowVideoDevice::TrySetupVideoTrack(FDShowTrack& Track, AM_MEDIA_TYPE* pmt, AM_MEDIA_TYPE* cmt,
	VIDEO_STREAM_CONFIG_CAPS* Optionalscc)
{
	if(!pmt || !cmt)
		return false;
	
	VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
	VIDEOINFOHEADER* cvih = (VIDEOINFOHEADER*)cmt->pbFormat;

	FString TypeName = GetFormatTypeFromGUID(pmt->subtype);

	// Ignore unsupported formats
	if(TypeName.Equals("Possibly Unsupported Format"))
		return false;
	
	// determine if currently selected index
	// TODO: account for frame granularity changes
	bool isCurrentTrack = false;
	if(FMath::Abs(vih->bmiHeader.biWidth - cvih->bmiHeader.biWidth) < 1.f && FMath::Abs(vih->bmiHeader.biHeight - cvih->bmiHeader.biHeight) < 1.f
		&& pmt->subtype == CurrentSubtype)
	{
		isCurrentTrack = true;
	}

	if(Optionalscc)
	{
		// some devices may incorrectly fill this out
		if(Optionalscc->MaxFrameInterval < Optionalscc->MinFrameInterval)
		{
			Optionalscc->MaxFrameInterval = Optionalscc->MinFrameInterval;
		}
	}

	// Compute FPS from AvgTimePerFrame (in 100-ns units)
	float fps = (CurrentFPS > 0 && isCurrentTrack) ? CurrentFPS : 1.0 / (vih->AvgTimePerFrame * 1.0e-7);
	
	// Calc range
	TRange<float> fpsrates = (Optionalscc) ? TRange<float>(1.0 / (Optionalscc->MaxFrameInterval * 1.0e-7), 1.0 / (Optionalscc->MinFrameInterval * 1.0e-7)) : TRange<float>(fps, fps);
	
	if(Optionalscc)
		fps = FMath::Clamp(fps, fpsrates.GetLowerBoundValue(), fpsrates.GetUpperBoundValue());

	
	// Create new index
	Track.Formats.Add(FDShowFormat({
		 pmt->majortype,
		 pmt->subtype,
		 TypeName,
		 {
		 0
		 },
		 {
		 (uint32)vih->dwBitRate,
		 GetTextureSampleFormatTypeFromGUID(pmt->subtype),
		 fps,  
		 fpsrates,
		 FIntPoint(vih->bmiHeader.biWidth, vih->bmiHeader.biHeight)
	 }}));
	 
	 return true;
}

bool FDirectShowVideoDevice::TrySetupAudioTrack(FDShowTrack& Track, AM_MEDIA_TYPE* pmt, 
	AUDIO_STREAM_CONFIG_CAPS* Optionalscc)
{
	if(!pmt)
		return false;
	
	WAVEFORMATEX* wfex = (WAVEFORMATEX*)pmt->pbFormat;

	FString TypeName = GetFormatTypeFromGUID(pmt->subtype);

	// Ignore unsupported formats
	if(TypeName.Equals("Possibly Unsupported Format"))
		return false;

	if(GetAudioSampleFormatBits(wfex) != EMediaAudioSampleFormat::Int16)
		return false;
					
	// Create new index
	Track.Formats.Add(FDShowFormat({
		 pmt->majortype,
		 pmt->subtype,
		 TypeName,
		 {
			 wfex->wBitsPerSample,
			 wfex->nChannels,
			 wfex->nSamplesPerSec
		 },
		 {
		 0
		 }
	}));
	
	return true;
}

bool FDirectShowVideoDevice::IsDeviceSetToFormat(const FDShowFormat& FormatInfo)
{
	if(FormatInfo.Video.FrameRate == CurrentFPS && FormatInfo.Video.OutputDim.X == Width &&
		FormatInfo.Video.OutputDim.Y == Height && FormatInfo.Video.FormatType == GetTextureSampleFormat())
			return true;

	return false;
}

bool FDirectShowVideoDevice::SetFormatInfo(const FString& Url, const FDShowFormat& VideoFormatInfo, const FDShowFormat* AudioFormatInfo)
{
	// check redundancy
	if(IsDeviceSetToFormat(VideoFormatInfo))
		return true;
	
	DShowMediaTypePtr pmt;
	TComPtr<IPin> Pin;

	DShowMediaType Videopmt;
	if(!GetVideoFormatFromInfo(Url, VideoFormatInfo, Videopmt))
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("SetFormatInfo failed when getting video format"))
		return false;
	}

	DShowMediaType Audiopmt;
	FString videoDeviceName;
	if(bHasAudio && AudioTracks.Num() > 0 && AudioTracks[0].Formats.Num() > 0)
	{
		FDShowFormat AudioFormat;
		if(!AudioFormatInfo)
		{
			AudioFormat = AudioTracks[0].Formats[0];
		}

		if(!GetAudioFormatFromInfo(AudioDeviceFriendlyName, AudioFormat, Audiopmt))
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("SetFormatInfo failed when getting Audio format"))
			return false;
		}
	}

	

	
	// found the media type to use
	return Initialize(Url, Videopmt, Audiopmt);
}

bool FDirectShowVideoDevice::GetVideoFormatFromInfo(const FString& Url, const FDShowFormat& FormatInfo, AM_MEDIA_TYPE* MediaType)
{
	DShowMediaTypePtr pmt;
	TComPtr<IPin> Pin;
	
	if (GetPin(Url, CLSID_VideoInputDeviceCategory,MEDIATYPE_Video,PINDIR_OUTPUT, &Pin))
	{
		TComPtr<IAMStreamConfig> StreamConfig;
		HRESULT hr = Pin->QueryInterface(IID_IAMStreamConfig, (void**)&StreamConfig);
		if (SUCCEEDED(hr) && StreamConfig)
		{
			// use StreamConfig to query the formats/caabilities.
			int iCount = 0, iSize = 0;
			hr = StreamConfig->GetNumberOfCapabilities(&iCount, &iSize);
			
			if (SUCCEEDED(hr) && iSize == sizeof(VIDEO_STREAM_CONFIG_CAPS))
			{
				for (int iFormat = 0; iFormat < iCount; iFormat++)
				{
					VIDEO_STREAM_CONFIG_CAPS scc;
					hr = StreamConfig->GetStreamCaps(iFormat, pmt, (BYTE*)&scc);
        
					if (SUCCEEDED(hr) && pmt.IsValid() && pmt->formattype == FORMAT_VideoInfo)
					{
						VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
						
						// sometimes this can be 0 which is invalid for testing purposes
						scc.MaxFrameInterval = FMath::Max(scc.MaxFrameInterval, scc.MinFrameInterval);
						
 						FString formatname = GetFormatTypeFromGUID(pmt->subtype);
						if(!IsFormatValid(FormatInfo, pmt, (BYTE*)&scc))
							continue;

						// We found the media type
						// Add requested FPS
						vih->AvgTimePerFrame = FMath::Clamp(10000000 / FormatInfo.Video.FrameRate, scc.MinFrameInterval, scc.MaxFrameInterval);

						CopyMediaType(MediaType, pmt);
						return true;
					}
				}
			}
			else if (hr == E_NOTIMPL)
			{
				// elgato devices implemented in this fashion i guess
				TComPtr<IEnumMediaTypes> MediaTypes;
				if (SUCCEEDED(Pin->EnumMediaTypes(&MediaTypes)))
				{
					ULONG count = 0;

					while (MediaTypes->Next(1, pmt, &count) == S_OK)
					{
						if (SUCCEEDED(hr) && pmt.IsValid() && pmt->formattype == FORMAT_VideoInfo)
						{

							if(!IsFormatValid(FormatInfo, pmt, nullptr))
								continue;

							CopyMediaType(MediaType, pmt);
							return true;
						}
					}
				}
			}
		}
	}
	
	return false;
}

bool FDirectShowVideoDevice::GetAudioFormatFromInfo(const FString& FriendlyName, const FDShowFormat& FormatInfo, AM_MEDIA_TYPE* MediaType)
{
	DShowMediaTypePtr pmt;
	TComPtr<IPin> Pin;
	
	if (TryGetAudioPinByFriendlyName(FriendlyName, &Pin))
	{
		TComPtr<IAMStreamConfig> StreamConfig;
		HRESULT hr = Pin->QueryInterface(IID_IAMStreamConfig, (void**)&StreamConfig);
		if (SUCCEEDED(hr) && StreamConfig)
		{
			// use StreamConfig to query the formats/caabilities.
			int iCount = 0, iSize = 0;
			hr = StreamConfig->GetNumberOfCapabilities(&iCount, &iSize);
			
			if (SUCCEEDED(hr) && iSize == sizeof(AUDIO_STREAM_CONFIG_CAPS))
			{
				for (int iFormat = 0; iFormat < iCount; iFormat++)
				{
					AUDIO_STREAM_CONFIG_CAPS scc;
					hr = StreamConfig->GetStreamCaps(iFormat, pmt, (BYTE*)&scc);
        
					if (SUCCEEDED(hr) && pmt.IsValid() && pmt->formattype == FORMAT_WaveFormatEx)
					{
						WAVEFORMATEX* wfex = (WAVEFORMATEX*)pmt->pbFormat;
                    
						// TODO: find correct format and set
						if(pmt->subtype != FormatInfo.MinorType)  // Adjust as needed for your audio format
							continue;

						// TODO: refactor this to include granularity resizes?
						if(FormatInfo.Audio.SampleRate != wfex->nSamplesPerSec ||
						   FormatInfo.Audio.NumChannels != wfex->nChannels ||
						   FormatInfo.Audio.BitsPerSample != wfex->wBitsPerSample)
						   	continue;
						
						CopyMediaType(MediaType, pmt);
						return true;
					}
				}
			}
		}
	}
	
	return false;
}


bool FDirectShowVideoDevice::IsFormatValid(const FDShowFormat& FormatInfo, AM_MEDIA_TYPE& MediaType,
                                           const BYTE* ConfigCaps)
{
	// Initialize caps
	long long MinInterval, MaxInterval;
	int32 MinWidth, MaxWidth, MinHeight, MaxHeight;
	uint32 dWidth, dHeight;   // granularity/delta of width height

	VIDEOINFOHEADER* pVih = (VIDEOINFOHEADER*)MediaType.pbFormat;
	const VIDEO_STREAM_CONFIG_CAPS* scc = (const VIDEO_STREAM_CONFIG_CAPS *)ConfigCaps;
	
	if(!scc)
	{
		MinInterval = MaxInterval = pVih->AvgTimePerFrame;
		MinWidth = MaxWidth = pVih->bmiHeader.biWidth;
		MinHeight = MaxHeight = pVih->bmiHeader.biHeight;
		dWidth = dHeight = 1; // this doesnt matter
	}
	else
	{
		MinInterval = scc->MinFrameInterval;
		MaxInterval = scc->MaxFrameInterval;
		MinWidth = scc->MinOutputSize.cx > 0 ? scc->MinOutputSize.cx : pVih->bmiHeader.biWidth;
		MinHeight = scc->MinOutputSize.cy > 0 ? scc->MinOutputSize.cy : pVih->bmiHeader.biHeight;
		MaxWidth = scc->MaxOutputSize.cx > 0 ? scc->MaxOutputSize.cx : pVih->bmiHeader.biWidth;
		MaxHeight = scc->MaxOutputSize.cy > 0 ? scc->MaxOutputSize.cy : pVih->bmiHeader.biHeight;
		dWidth = scc->OutputGranularityX;
	}

	//LogMediaType(MediaType);
	if(FormatInfo.MinorType != MediaType.subtype)
		return false;

	long long FrameInterval = 10000000 / FormatInfo.Video.FrameRate;
	if(MaxInterval == MinInterval && abs(FrameInterval - MaxInterval) < 1)
	{
		// we count this as valid
		pVih->AvgTimePerFrame = MinInterval;
	}
	else if(FrameInterval < MinInterval || FrameInterval > MaxInterval)
		return false;

	if(FormatInfo.Video.OutputDim.X < MinWidth || FormatInfo.Video.OutputDim.Y < MinHeight ||
	   FormatInfo.Video.OutputDim.X > MaxWidth || FormatInfo.Video.OutputDim.Y > MaxHeight)
	   	return false;

	return true;
}

HRESULT FDirectShowVideoDevice::SetupMjpegDecompressorGraph()
{
	HRESULT hr = S_OK;
	
	// Create the MJPG Decompressor filter.
	hr = CoCreateInstance(CLSID_MjpegDec, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&DecompressorFilter));
	if (FAILED(hr)) {
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to create Decompressor from COCreateInstance: %d"), hr);
		return hr;
	}
	
	// Add the MJPG Decompressor filter to the graph.
	hr = Graph->AddFilter(DecompressorFilter, L"MJPEG Decompressor");
	if (FAILED(hr)) {
		DecompressorFilter.Reset();
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to add Decompressor Filter to Graph: %d"), hr);
		return hr;
	}

	 // Create the Color Space Converter filter.
    hr = CoCreateInstance(CLSID_Colour, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ColorConverterFilter));
    if (FAILED(hr)) 
    {
        UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to create Color Space Converter filter: %d"), hr);
        return hr;
    }

	TComPtr<IPin> pColorConverterOut;
	GetPin(ColorConverterFilter, PINDIR_OUTPUT, &pColorConverterOut);
	hr = SetPinMediaType(pColorConverterOut, MEDIASUBTYPE_ARGB32);
	if (FAILED(hr)) 
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to etPinMediaType(pColorConverterOut, MEDIASUBTYPE_RGB32): %d"), hr);
		return hr;
	}
	
    hr = Graph->AddFilter(ColorConverterFilter, L"Color Space Converter");
    if (FAILED(hr)) 
    {
        ColorConverterFilter.Reset();
        UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to add Color Space Converter to graph: %d"), hr);
        return hr;
    }
	
	ConnectVideoGraph();
	
	return hr;
}


HRESULT FDirectShowVideoDevice::SetupH264Graph()
{
	HRESULT hr = S_OK;
	
	// Create the MJPG Decompressor filter.
	hr = CoCreateInstance(CLSID_MSDTV, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&DecompressorFilter));
	if (FAILED(hr)) {
		return hr;
	}
	
	// Add the H.264 Decoder filter to the graph.
	hr = Graph->AddFilter(DecompressorFilter, L"H.264 Decoder");
	if (FAILED(hr)) {
		DecompressorFilter.Reset();
		return hr;
	}

	 // Create the Color Space Converter filter.
    hr = CoCreateInstance(CLSID_Colour, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ColorConverterFilter));
    if (FAILED(hr)) 
    {
        UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to create Color Space Converter filter"));
        return hr;
    }

	TComPtr<IPin> pColorConverterOut;
	GetPin(ColorConverterFilter, PINDIR_OUTPUT, &pColorConverterOut);
	SetPinMediaType(pColorConverterOut, MEDIASUBTYPE_ARGB32);
    hr = Graph->AddFilter(ColorConverterFilter, L"Color Space Converter");
    if (FAILED(hr)) 
    {
        ColorConverterFilter.Reset();
        UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to add Color Space Converter to graph"));
        return hr;
    }
	
	ConnectVideoGraph();
	
	return hr;
}

HRESULT FDirectShowVideoDevice::ConnectVideoGraph()
{
	HRESULT hr;
	
	if(DecompressorFilter && ColorConverterFilter)
	{
		// Connect the source filter to the MJPEG Decompressor
		TComPtr<IPin> pSourceOut;
		GetPin(VideoSourcefilter, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &pSourceOut);
		TComPtr<IPin> pMJPEGDecIn;
		GetPin(DecompressorFilter, PINDIR_INPUT, &pMJPEGDecIn);	
		hr = Graph->Connect(pSourceOut, pMJPEGDecIn);
		if (FAILED(hr)) {
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Source to MJPEG Decompressor"));
			return hr;
		}

		// Connect MJPEG Decompressor's output pin to Color Space Converter's input pin.
		TComPtr<IPin> pMJPEGDecOut;
		GetPin(DecompressorFilter, PINDIR_OUTPUT, &pMJPEGDecOut);
		TComPtr<IPin> pColorConverterIn;
		GetPin(ColorConverterFilter, PINDIR_INPUT, &pColorConverterIn);
	
		hr = Graph->Connect(pMJPEGDecOut, pColorConverterIn);
		if (FAILED(hr)) 
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect MJPEG Decompressor to Color Space Converter: %d"), hr);
			return hr;
		}

		if (Demux)
		{
			// Connect Color Space Converter's output pin to Demux's input pin.
			TComPtr<IPin> pColorConverterOut;
			GetPin(ColorConverterFilter, PINDIR_OUTPUT, &pColorConverterOut);
			// TComPtr<IPin> pSampleGrabIn;
			// GetPin(VideoSamplegrabberfilter, PINDIR_INPUT, &pSampleGrabIn);
			TComPtr<IPin> pDemuxIn;
			GetPin(Demux, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &pDemuxIn);
			hr = Graph->Connect(pColorConverterOut, pDemuxIn);
			if (FAILED(hr)) 
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Color Space Converter to SampleGrabber: %d"), hr);
				return hr;
			}

			// Demux to sampleGrabber
			TComPtr<IPin> pDemuxOut;
			GetPin(Demux, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &pDemuxOut);
			TComPtr<IPin> pSampleGrabIn;
			GetPin(VideoSamplegrabberfilter, PINDIR_INPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &pSampleGrabIn);
			hr = Graph->ConnectDirect(pDemuxOut, pSampleGrabIn, nullptr);
			if (FAILED(hr)) {
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Video demux to samplegrabber: %d"), hr);
				return hr;
			}
		}
		else
		{
			// Connect Color Space Converter's output pin to SampleGrabber's input pin.
			TComPtr<IPin> pColorConverterOut;
			GetPin(ColorConverterFilter, PINDIR_OUTPUT, &pColorConverterOut);
			TComPtr<IPin> pSampleGrabIn;
			GetPin(VideoSamplegrabberfilter, PINDIR_INPUT, &pSampleGrabIn);
			hr = Graph->Connect(pColorConverterOut, pSampleGrabIn);
			if (FAILED(hr)) 
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Color Space Converter to SampleGrabber: %d"), hr);
				return hr;
			}
		}
	}
	else if (Demux)
	{
		// Connect the source filter to the demix input pin.
		TComPtr<IPin> pSourceOut;
		GetPin(VideoSourcefilter, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &pSourceOut);
		TComPtr<IPin> pDemuxIn;
		GetPin(Demux, PINDIR_INPUT, &pDemuxIn, true);
		hr = Graph->ConnectDirect(pSourceOut, pDemuxIn, nullptr);
		if (FAILED(hr)) {
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Video source filter to demux: %d"), hr);
			return hr;
		}

		TComPtr<IPin> pDemuxOut;
		GetPin(Demux, PINDIR_OUTPUT, &pDemuxOut, true);
		TComPtr<IPin> pSampleGrabIn;
		GetPin(VideoSamplegrabberfilter, PINDIR_INPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &pSampleGrabIn);
		hr = Graph->ConnectDirect(pDemuxOut, pSampleGrabIn, nullptr);
		if (FAILED(hr)) {
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Video demux to samplegrabber: %d"), hr);
			return hr;
		}
	}
	else
	{
		TComPtr<IPin> pSourceOut;
		GetPin(VideoSourcefilter, PINDIR_OUTPUT, MEDIATYPE_Video, PIN_CATEGORY_CAPTURE, &pSourceOut);
		TComPtr<IPin> pSampleGrabIn;
		GetPin(VideoSamplegrabberfilter, PINDIR_INPUT, &pSampleGrabIn);
		hr = Graph->Connect(pSourceOut, pSampleGrabIn);
		if (FAILED(hr)) {
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Video source filter to samplegrabber: %d"), hr);
			return hr;
		}
	}

	return S_OK;
}

HRESULT FDirectShowVideoDevice::ConnectAudioGraph()
{
	HRESULT hr;

	if(Demux)
	{
		// Connect the source filter to the SampleGrabber's input pin.
		TComPtr<IPin> pSourceOut;
		GetPin(AudioSourcefilter, PINDIR_OUTPUT, MEDIATYPE_Audio, PIN_CATEGORY_CAPTURE, &pSourceOut);
		TComPtr<IPin> pDemuxIn;
		GetPin(Demux, PINDIR_INPUT, &pSourceOut, true);
		hr = Graph->ConnectDirect(pSourceOut, pDemuxIn, nullptr);
		if (FAILED(hr)) {
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Audio source filter to demux"));
			return hr;
		}

		TComPtr<IPin> pDemuxOut;
		GetPin(Demux, PINDIR_OUTPUT, &pDemuxOut, true);
		TComPtr<IPin> pSampleGrabIn;
		GetPin(AudioSamplegrabberfilter, PINDIR_INPUT, MEDIATYPE_Audio, PIN_CATEGORY_CAPTURE, &pSampleGrabIn);
		hr = Graph->ConnectDirect(pDemuxOut, pSampleGrabIn, nullptr);
		if (FAILED(hr)) {
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect audio demux to samplegrabber "));
			return hr;
		}
	}
	else
	{
		TComPtr<IPin> pSourceOut;
		GetPin(AudioSourcefilter, PINDIR_OUTPUT, MEDIATYPE_Audio, PIN_CATEGORY_CAPTURE, &pSourceOut);
		TComPtr<IPin> pSampleGrabIn;
		GetPin(AudioSamplegrabberfilter, PINDIR_INPUT, &pSampleGrabIn);
		hr = Graph->Connect(pSourceOut, pSampleGrabIn);
		if (FAILED(hr)) {
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to connect Audio source filter to samplegrabber"));
			return hr;
		}
	}
	
	return S_OK;
}


bool FDirectShowVideoDevice::RequestFrameRateChange(int32 TrackIndex, int32 FormatIndex, float newFrameRate)
{
	if(!VideoTracks.IsValidIndex(TrackIndex))
		return false;

	FDShowTrack& Track = VideoTracks[TrackIndex];

	if(!Track.Formats.IsValidIndex(FormatIndex))
		return false;

	FDShowFormat& Format = Track.Formats[FormatIndex];

	if (FMath::RoundToZero(Format.Video.FrameRates.GetUpperBoundValue()) < FMath::RoundToZero(newFrameRate))
	{
		return false; // FPS too high
	}

	if (FMath::Abs(Track.Formats[FormatIndex].Video.FrameRate - newFrameRate) < 1.f)
	{
		return false; // frame rate already set
	}
	Format.Video.FrameRate = FMath::RoundToZero(newFrameRate);
	
	return true;
}

FIntPoint FDirectShowVideoDevice::GetAspectRatio() const
{
	int b = Height;
	int divisor = Width;
	while (b != 0) {
		int temp = divisor % b;
		divisor = b;
		b = temp;
	}
	
	return FIntPoint(Width / divisor, Height / divisor);
}



void FDirectShowVideoDevice::Start()
{
	HRESULT HResult;
	if(VideoSamplegrabberfilter.IsValid())
	{
		HResult = VideoSamplegrabberfilter->Run(0);
		if (HResult < 0)
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to start Samplegrabberfilter: %d"), HResult)
		}
	}

	if(ColorConverterFilter.IsValid())
	{
		HResult = ColorConverterFilter->Run(0);
		if (HResult < 0)
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to start ColorConverterFilter: %d"), HResult)
		}
	}
	
	// might not be valid/needed 
	if(DecompressorFilter.IsValid())
	{
		HResult = DecompressorFilter->Run(0);
		if (HResult < 0)
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to start DecompressorFilter: %d"), HResult)
		}
	}

	if(VideoSourcefilter.IsValid())
	{
		HResult = VideoSourcefilter->Run(0);
		if (HResult < 0)
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to start Sourcefilter: %d"), HResult)
		}
	}
}

void FDirectShowVideoDevice::Stop()
{
	HRESULT HResult;

	// if(Sourcefilter.IsValid())
	// {
	// 	HResult = Sourcefilter->Stop();
	// }
	//
	// if(DecompressorFilter.IsValid())
	// {
	// 	HResult = DecompressorFilter->Stop();
	// }
	//
	// if(ColorConverterFilter.IsValid())
	// {
	// 	HResult = ColorConverterFilter->Stop();
	// }
	//
	// if(Samplegrabberfilter.IsValid())
	// {
	// 	HResult = Samplegrabberfilter->Stop();
	// }
	
	if(Control.IsValid() && bIsInitialized)
	{
		HResult = Control->Stop();
		if (FAILED(HResult)) 
		{
			UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to stop the graph"));
			return;
		}
	}
	
	if (Graph.IsValid())
	{
		if (VideoSamplegrabberfilter.IsValid())
		{
			
			HResult = Graph->RemoveFilter(VideoSamplegrabberfilter);
			if (FAILED(HResult)) 
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to RemoveFilter(Samplegrabberfilter)"));
			}
		}
		if (ColorConverterFilter.IsValid())
		{
			HResult = Graph->RemoveFilter(ColorConverterFilter);
			if (FAILED(HResult)) 
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to RemoveFilter(ColorConverterFilter)"));
			}
		}
		if (DecompressorFilter.IsValid())
		{
			HResult = Graph->RemoveFilter(DecompressorFilter);
			if (FAILED(HResult)) 
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to RemoveFilter(DecompressorFilter)"));
			}
		}
		if (VideoSourcefilter.IsValid())
		{
			HResult = Graph->RemoveFilter(VideoSourcefilter);
			if (FAILED(HResult)) 
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to RemoveFilter(Sourcefilter)"));
			}
		}

		if (AudioSamplegrabberfilter.IsValid())
		{
			HResult = Graph->RemoveFilter(AudioSamplegrabberfilter);
			if (FAILED(HResult)) 
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to RemoveFilter(AudioSamplegrabberfilter)"));
			}
		}
		if (AudioSourcefilter.IsValid())
		{
			HResult = Graph->RemoveFilter(AudioSourcefilter);
			if (FAILED(HResult)) 
			{
				UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to RemoveFilter(AudioSourcefilter)"));
			}
		}
	}
	
	//ReleaseFilters();
	Width = 0;
	Height = 0;
	CurrentSubtype = MEDIASUBTYPE_None;
	CurrentFPS = 0;
	
	VideoSourcefilter.Reset();
	DecompressorFilter.Reset();
	ColorConverterFilter.Reset();
	VideoSamplegrabberfilter.Reset();
	VideoSamplegrabber.Reset();
	
	AudioSourcefilter.Reset();
	AudioSamplegrabberfilter.Reset();
	AudioSamplegrabber.Reset();

	Graph.Reset();
	Capture.Reset();
	Control.Reset();
	Demux.Reset();
	Clock.Reset();
	

	bIsInitialized = false;
}

FString FDirectShowVideoDevice::GetFormatTypeFromGUID(const GUID& Id) const
{
	if(Id == MEDIASUBTYPE_MJPG)
		return FString("MJPG");
	else if( Id == MEDIASUBTYPE_YUY2)
		return FString("YUY2");
	else if( Id == MEDIASUBTYPE_NV12)
		return FString("NV12");
	else if( Id == MEDIASUBTYPE_YUYV)
		return FString("YUYV");
	else if( Id == MEDIASUBTYPE_UYVY)
		return FString("UYVY");
	else if( Id == MEDIASUBTYPE_H264)
		return FString("H264");
	else if( Id == MEDIASUBTYPE_ARGB32)
		return FString("ARGB32");
	else if( Id == MEDIASUBTYPE_RGB32)
		return FString("RGB32");
	if(Id == MEDIASUBTYPE_PCM)
		return FString("PCM");

	

	
	// Add more cases as needed

	//UE_LOG(LogDirectShowMedia, Error, TEXT("No Format Found from subtype GUID: %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"),
	//					   Id.Data1, Id.Data2, Id.Data3,
	//					   Id.Data4[0], Id.Data4[1], Id.Data4[2], Id.Data4[3],
	//					   Id.Data4[4], Id.Data4[5], Id.Data4[6], Id.Data4[7])

	return FString("Possibly Unsupported Format");
}

EMediaTextureSampleFormat FDirectShowVideoDevice::GetTextureSampleFormatTypeFromGUID(const GUID& Id) const
{
	// TODO: verify this idk 
	if(Id == MEDIASUBTYPE_MJPG)
		return EMediaTextureSampleFormat::CharBGRA;
	else if(Id == MEDIASUBTYPE_YUY2)
		return EMediaTextureSampleFormat::CharYUY2;
	else if(Id == MEDIASUBTYPE_NV12)
		return EMediaTextureSampleFormat::CharNV12;
	else if(Id == MEDIASUBTYPE_YUYV)
		return EMediaTextureSampleFormat::CharYUY2;
	else if(Id == MEDIASUBTYPE_UYVY)
		return EMediaTextureSampleFormat::CharUYVY;
	else if(Id == MEDIASUBTYPE_H264)
		return EMediaTextureSampleFormat::CharUYVY;
	else if(Id == MEDIASUBTYPE_ARGB32)
		return EMediaTextureSampleFormat::CharBGRA;
	else if(Id == MEDIASUBTYPE_RGB32)
		return EMediaTextureSampleFormat::CharBGRA;
	
	return EMediaTextureSampleFormat::Undefined;
}

EMediaTextureSampleFormat FDirectShowVideoDevice::GetTextureSampleFormat() const
{
	return GetTextureSampleFormatTypeFromGUID(CurrentSubtype);
}


#undef LOCTEXT_NAMESPACE
