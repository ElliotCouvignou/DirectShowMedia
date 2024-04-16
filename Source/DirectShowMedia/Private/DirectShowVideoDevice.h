#pragma once


#include "Windows/AllowWindowsPlatformTypes.h"
#include <dshow.h>
#include "Windows/HideWindowsPlatformTypes.h"
#include "DirectShowMediaType.h"
#include "IMediaAudioSample.h"
#include "Microsoft/COMPointer.h"
#include "IMediaTextureSample.h"
#include "IMediaTracks.h"
#include "MediaPlayerOptions.h"

struct ISampleGrabber;
class FDirectShowCallbackHandler;
struct IBaseFilter;


/** Track format. */
struct FDShowFormat
{
	GUID MajorType;     //majortype of samplegrabber
	GUID MinorType;
	FString TypeName;

	struct
	{
		uint32 BitsPerSample;
		uint32 NumChannels;
		uint32 SampleRate;
	}
	Audio;

	struct
	{
		uint32 BitRate;
		EMediaTextureSampleFormat FormatType;
		float FrameRate;
		TRange<float> FrameRates;
		FIntPoint OutputDim;
	}
	Video;
};


/** Track information. */
struct FDShowTrack
{
	FText DisplayName;
	TArray<FDShowFormat> Formats;
	FString Language;
	FString Name;
	bool Protected;
	int32 SelectedFormat;
};


DECLARE_DELEGATE_OneParam(FOnTracksUpdated, uint32 SelectedIndex);


class FDirectShowVideoDevice
{
public:
	FDirectShowVideoDevice();
	virtual ~FDirectShowVideoDevice();

	/**
	* Initialize the track collection.
 	*
 	* @param InMediaSource The media source object.
 	* @param Url The media source URL.
 	* @see IsInitialized, Shutdown
 	*/
	bool Initialize(const FString& Url, AM_MEDIA_TYPE* Format, AM_MEDIA_TYPE* AudioFormat = nullptr);
	bool TryInitializeAudio(const FString& InFriendlyName, AM_MEDIA_TYPE* Format);
	bool InitializeGraph();

	void SetAudioBuffer(float delayMs);
	
	void FillVideoFormatData(IPin* SourcePin);
	void FillAudioFormatData(IPin* SourcePin);
	void FillFormatDataFromURL(const FString& Url, const FString& OptionalAudioDeviceName);

	bool TrySetupVideoTrack(FDShowTrack& Track, AM_MEDIA_TYPE* pmt, AM_MEDIA_TYPE* cmt, VIDEO_STREAM_CONFIG_CAPS* Optionalscc = nullptr);
	bool TrySetupAudioTrack(FDShowTrack& Track, AM_MEDIA_TYPE* pmt, AUDIO_STREAM_CONFIG_CAPS* Optionalscc = nullptr);
	
	FString GetFormatTypeFromGUID(const GUID& Id) const;
	EMediaTextureSampleFormat GetTextureSampleFormatTypeFromGUID(const GUID& Id) const;
	EMediaTextureSampleFormat GetTextureSampleFormat() const;
	EMediaAudioSampleFormat GetCurrentAudioSampleFormat() const { return SampleFormat; }

	FIntPoint GetTextureSize() const { return FIntPoint(Width, Height); }
	int32 GetTextureSizeX() const { return Width; }
	float GetFramerate() const { return CurrentFPS; }
	GUID GetCurrentSubtype() const { return CurrentSubtype; }
	FIntPoint GetAspectRatio() const;
	
	uint32 GetSampleRate() const { return SampleRate; }
	uint32 GetNumChannels() const { return NumChannels; }

	FString GetFriendlyName() const { return Friendlyname; }
	FString GetAudioFriendlyName() const { return AudioDeviceFriendlyName; }
	void SetAudioFriendlyName(const FString& InNewName)  { AudioDeviceFriendlyName = InNewName; }
	
	TArray<FDShowTrack>& GetVideoTracks() { return VideoTracks; }
	TArray<FDShowTrack>& GetAudioTracks() { return AudioTracks; }

	bool IsDeviceSetToFormat(const FDShowFormat& FormatInfo);
	bool SetFormatInfo(const FString& Url, const FDShowFormat& VideoFormatInfo, const FDShowFormat* AudioFormatInfo = nullptr);

	bool GetVideoFormatFromInfo(const FString& Url, const FDShowFormat& FormatInfo, AM_MEDIA_TYPE* MediaType);
	bool GetAudioFormatFromInfo(const FString& FriendlyName, const FDShowFormat& FormatInfo, AM_MEDIA_TYPE* MediaType);
	//bool GetMediaTypeFromFormatInfo(const FString& Url, FDShowFormat& FormatInfo, DShowMediaType& outMediaType);

	bool RequestFrameRateChange(int32 TrackIndex, int32 FormatIndex, float newFrameRate);
	bool DoesHaveAudioDevice() const { return bHasAudio; }
	
	HRESULT SetupMjpegDecompressorGraph();
	HRESULT SetupH264Graph();
	HRESULT ConnectVideoGraph();
	HRESULT ConnectAudioGraph();   
	

	FDirectShowCallbackHandler* GetVideoCallbackHandler() const { return VideoCallbackhandler; }
	FDirectShowCallbackHandler* GetAudioCallbackHandler() const { return AudioCallbackhandler; }

	void Start();
	void Stop();

	FOnTracksUpdated OnVideoTracksUpdated;
	FOnTracksUpdated OnAudioTracksUpdated;
	bool bIsInitialized = false;

protected:

	// these should prob be moved to own common dshow file
	// bool GetPin(TComPtr<IBaseFilter> pFilter, PIN_DIRECTION PinDir, IPin** Pin);
	// bool GetPin(TComPtr<IBaseFilter> pFilter, PIN_DIRECTION PinDir, GUID MajorType, GUID Category, IPin** Pin);
	// bool GetPin(const FString& Url, PIN_DIRECTION PinDir, IPin** Pin);

	bool IsFormatValid(const FDShowFormat& FormatInfo, AM_MEDIA_TYPE& MediaType, const BYTE *ConfigCaps = nullptr);
	
	FCriticalSection CriticalSection;
	
	/** The available video tracks. */
	TArray<FDShowTrack> VideoTracks;
	/** The available audio tracks. */
	TArray<FDShowTrack> AudioTracks;

	bool bHasAudio;
	
	double CurrentTime;
	IMediaSample* CurrentSample;
	char* CurrentBuffer;
	long CurrentLength;

	int32 Width;
	int32 Height;
	float CurrentFPS;

	EMediaAudioSampleFormat SampleFormat;
	uint32 BitsPerSample;
	uint32 NumChannels;
	uint32 SampleRate;
	
	GUID CurrentSubtype;
	GUID CurrentAudioSubtype;
	FString Friendlyname = "";
	FString AudioDeviceFriendlyName = "";
	//WCHAR* Filtername;
	//WCHAR* AudioFiltername;
	FString URL;

	uint32 CurrentSelectedAudioTrack;
	uint32 CurrentSelectedCaptionTrack;
	uint32 CurrentSelectedMetadataTrack;
	uint32 CurrentSelectedVideoTrack;	


	// DShowMediaType SourceMediaType;
	// DShowMediaType SamplerMediaType;

	TComPtr<IFilterGraph2> Graph;
	TComPtr<ICaptureGraphBuilder2> Capture;
	TComPtr<IMediaControl> Control;
	TComPtr<IBaseFilter> Demux;
	TComPtr<IReferenceClock> Clock;
	
	// Video stuff
	TComPtr<IBaseFilter> VideoSourcefilter;
	TComPtr<IBaseFilter> DecompressorFilter;  // used when in mjpg format
	TComPtr<IBaseFilter> ColorConverterFilter;  // used when in mjpg format
	TComPtr<IBaseFilter> VideoSamplegrabberfilter;	
	TComPtr<ISampleGrabber> VideoSamplegrabber;
	FDirectShowCallbackHandler* VideoCallbackhandler;
	
	// Audio stuff
	TComPtr<IBaseFilter> AudioSourcefilter;
	TComPtr<IBaseFilter> AudioSamplegrabberfilter;	
	TComPtr<ISampleGrabber> AudioSamplegrabber;
	FDirectShowCallbackHandler* AudioCallbackhandler;
};
