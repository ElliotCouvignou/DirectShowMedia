// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectShowMediaTracks.h"
#include "DirectShowVideoDevice.h"
// #include "DirectShowMediaCommon.h"
#include "DirectShowMedia.h"

#include "MediaHelpers.h"
#include "MediaSampleQueueDepths.h"
#include "MediaPlayerOptions.h"
#include "Misc/ScopeLock.h"
#include "UObject/Class.h"

#if WITH_ENGINE
	#include "Engine/Engine.h"
#endif



#include "DirectShowCallbackHandler.h"
#include "Player/DirectShowMediaTextureSample.h"

#include "DirectShowMediaAudioSample.h"
#include "DirectShowMediaBinarySample.h"
#include "DirectShowMediaOverlaySample.h"
#include "IMediaOptions.h"


#define LOCTEXT_NAMESPACE "FDirectShowMediaTracks"

#define DirectShowMEDIATRACKS_TRACE_FORMATS 0

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1



/* FDirectShowMediaTracks structors
 *****************************************************************************/

FDirectShowMediaTracks::FDirectShowMediaTracks() :
	MediaSourceChanged(false),
	CurrentRate(0.f),
	Info(""),
	SourceUrl(""),
	DesiredAudioDevice(""),
	SelectionChanged(false),
	AudioSamplePool(new FDirectShowMediaAudioSamplePool),
	VideoSamplePool(new FDirectShowMediaTextureSamplePool),
	SelectedAudioTrack(INDEX_NONE),
	SelectedCaptionTrack(INDEX_NONE),
    SelectedMetadataTrack(INDEX_NONE),
	SelectedVideoTrack(INDEX_NONE),
	CurrentState (EMediaState::Closed),
	LastState (EMediaState::Closed),
	CurrentTime(FTimespan::Zero()),
	ShouldLoop(false),
	Duration(FTimespan::Zero()),
	TargetTime(FTimespan::Zero()),
	CurrentVideoDevice(nullptr)
	//CurrentAudioDevice(nullptr)
 {

 }


FDirectShowMediaTracks::~FDirectShowMediaTracks()
{
	Shutdown();

	delete AudioSamplePool;
	AudioSamplePool = nullptr;

	delete VideoSamplePool;
	VideoSamplePool = nullptr;

	delete CurrentVideoDevice;
	CurrentVideoDevice = nullptr;
}

void FDirectShowMediaTracks::Initialize(const FString& Url, const IMediaOptions* Options)
{
	FString indesiredAudioDevice = (Options) ? Options->GetMediaOption(FName("AudioDeviceName"), FString()) : FString();
	
// 	if(SourceUrl.Equals(Url) && DesiredAudioDevice.Equals(indesiredAudioDevice) || Url.IsEmpty())
// 		return;
	if(IsDuplicateInitialize(Url,Options))
		return;
	//Shutdown();

	FScopeLock Lock(&CriticalSection);
	if(CurrentVideoDevice)
	{
		delete CurrentVideoDevice;
		CurrentVideoDevice = nullptr;
	}
	bShuttingDown = false;
	SourceUrl = Url;
	DesiredAudioDevice = indesiredAudioDevice;
	MediaSourceChanged = true;
	SelectionChanged = true;
	
	/// Setup video device ///
	CurrentVideoDevice = new FDirectShowVideoDevice();
	if(FDirectShowCallbackHandler* VideoCallback = CurrentVideoDevice->GetVideoCallbackHandler())
	{
		VideoCallback->OnSampleCB.BindLambda([this](double Time, IMediaSample* Sample) {
			this->HandleMediaSamplerVideoSample(Time, Sample);
		});
	}
	if(FDirectShowCallbackHandler* VideoCallback = CurrentVideoDevice->GetAudioCallbackHandler())
	{
		VideoCallback->OnSampleCB.BindLambda([this](double Time, IMediaSample* Sample) {
			this->HandleMediaSamplerAudioSample(Time, Sample);
		});
	}
	
	CurrentVideoDevice->OnVideoTracksUpdated.BindLambda([this](uint32 SelectedIndex) {
		this->OnVideoTracksUpdated(SelectedIndex);
	});
	CurrentVideoDevice->OnAudioTracksUpdated.BindLambda([this](uint32 SelectedIndex) {
		this->OnAudioTracksUpdated(SelectedIndex);
	});
	
	
	CurrentVideoDevice->FillFormatDataFromURL(Url, DesiredAudioDevice);

	if(Options && VideoTracks.Num() > 0)
	{
		FDShowFormat VideoFormat;
		// pre-select video format
		
		const int64 TrackIdx = Options->GetMediaOption(FName("VideoTrackIndex"), (int64)0);
		const int64 FormatIdx = Options->GetMediaOption(FName("VideoFormatIndex"), (int64)-1);
		const int64 FrameRate = Options->GetMediaOption(FName("VideoFramerate"), (int64)-1);
		
		if(VideoTracks.IsValidIndex(TrackIdx) && VideoTracks[TrackIdx].Formats.IsValidIndex(FormatIdx))
		{
			VideoFormat = VideoTracks[TrackIdx].Formats[FormatIdx];
			VideoTracks[TrackIdx].SelectedFormat = FormatIdx;
			VideoFormat.Video.FrameRate = (FrameRate <= 0) ? VideoFormat.Video.FrameRates.GetUpperBoundValue() : FrameRate;
		}

//		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Url, VideoFormat]()
//		{
		//	FScopeLock Lock(&CriticalSection);
			if(!CurrentVideoDevice || bShuttingDown)
				return;
			bIsInitializing = true;
			if(!CurrentVideoDevice->SetFormatInfo(Url, VideoFormat))  // this will call Initialize;
			{
				// failed to get pre-selected info, just open default
				UE_LOG(LogDirectShowMedia, Error, TEXT("failed to get pre-selected video format initialized, procedding with default settings initialization"))
				if(VideoTracks[0].Formats.Num() > 0 &&  !CurrentVideoDevice->SetFormatInfo(Url, VideoTracks[0].Formats[0]))
				{
					UE_LOG(LogDirectShowMedia, Error, TEXT("failed to initialide video device"))
					bIsInitializing = false;
					Shutdown();
				}
			}
			bIsInitializing = false;
//		});
	}
	else
	{
		Shutdown();
		CurrentState = EMediaState::Closed;
		DeferredEvents.Enqueue(EMediaEvent::MediaClosed);
		return;
	}
	
	SetRate(0.0f);
	CurrentState = EMediaState::Preparing;
	DeferredEvents.Enqueue(EMediaEvent::MediaOpened);
}


bool FDirectShowMediaTracks::IsDuplicateInitialize(const FString& Url, const class IMediaOptions* Options)
{
	FString indesiredAudioDevice = (Options) ? Options->GetMediaOption(FName("AudioDeviceName"), FString()) : FString();

	if(Url.IsEmpty())
		return true;

	if (SourceUrl.Equals(Url) && DesiredAudioDevice.Equals(indesiredAudioDevice))
	{
		if (Options && VideoTracks.Num() > 0)
		{
			FDShowFormat VideoFormat;
			// pre-select video format

			const int64 TrackIdx = Options->GetMediaOption(FName("VideoTrackIndex"), (int64)0);
			const int64 FormatIdx = Options->GetMediaOption(FName("VideoFormatIndex"), (int64)-1);
			const int64 FrameRate = Options->GetMediaOption(FName("VideoFramerate"), (int64)-1);

			if (VideoTracks.IsValidIndex(TrackIdx) && VideoTracks[TrackIdx].Formats.IsValidIndex(FormatIdx))
			{
				if (SelectedVideoTrack != TrackIdx)
				{
					return false;
				}
				VideoFormat = VideoTracks[TrackIdx].Formats[FormatIdx];
				if (VideoTracks[TrackIdx].SelectedFormat == FormatIdx)
				{
					const FDShowFormat* Format = GetVideoFormat(TrackIdx, FormatIdx);
					float CurrentFPS = Format->Video.FrameRate;
					if (CurrentVideoDevice)
					{
						CurrentFPS = CurrentVideoDevice->GetFramerate();
					}
					if (CurrentFPS != ((FrameRate <= 0) ? VideoFormat.Video.FrameRates.GetUpperBoundValue() : FrameRate))
					{
						return false;
					}
					return true;
				}
				else
				{
					return false;
				}
			}
		}
	}

	return false;
}

void FDirectShowMediaTracks::Shutdown()
{
	bShuttingDown = true;
	UE_LOG(LogDirectShowMedia, Verbose, TEXT("Tracks: %p: Shutting down (media source)"), this);
	if(bIsInitializing)
		return;
	if(CurrentVideoDevice)
	{
		CurrentVideoDevice->Stop();
		delete CurrentVideoDevice;
		CurrentVideoDevice = nullptr;
	}
	FScopeLock Lock(&CriticalSection);
	// if(CurrentAudioDevice)
	// {
	// 	CurrentAudioDevice->Stop();
	// 	delete CurrentAudioDevice;
	// 	CurrentAudioDevice = nullptr;
	// 	
	// }
	

	
	CurrentState =  EMediaState::Closed;
	
	SourceUrl = "";
	DesiredAudioDevice = "";

	AudioSamplePool->Reset();
	VideoSamplePool->Reset();
	
	AudioTracks.Empty();
	MetadataTracks.Empty();
	CaptionTracks.Empty();
	VideoTracks.Empty();

	MediaSourceChanged = false;
	SelectionChanged = false;
}

void FDirectShowMediaTracks::AppendStats(FString& OutStats) const
{
	FScopeLock Lock(&CriticalSection);

	// audio tracks
	OutStats += TEXT("Audio Tracks\n");
	
	if (AudioTracks.Num() == 0)
	{
		OutStats += TEXT("\tnone\n");
	}
	else
	{
		for (const FDShowTrack& Track : AudioTracks)
		{
			OutStats += FString::Printf(TEXT("\t%s\n"), *Track.DisplayName.ToString());
			OutStats += TEXT("\t\tNot implemented yet");
		}
	}

	// video tracks
	OutStats += TEXT("Video Tracks\n");

	if (VideoTracks.Num() == 0)
	{
		OutStats += TEXT("\tnone\n");
	}
	else
	{
		for (const FDShowTrack& Track : VideoTracks)
		{
			OutStats += FString::Printf(TEXT("\t%s\n"), *Track.DisplayName.ToString());
			OutStats += TEXT("\t\tNot implemented yet");
		}
	}
}

void FDirectShowMediaTracks::ClearFlags()
{
	//FScopeLock Lock(&CriticalSection);

	MediaSourceChanged = false;
	SelectionChanged = false;
}

void FDirectShowMediaTracks::GetEvents(TArray<EMediaEvent>& OutEvents)
{
	EMediaEvent Event;

	while (DeferredEvents.Dequeue(Event))
	{
		OutEvents.Add(Event);
	}
}


void FDirectShowMediaTracks::GetFlags(bool& OutMediaSourceChanged, bool& OutSelectionChanged) const
{
	//FScopeLock Lock(&CriticalSection);

	OutMediaSourceChanged = MediaSourceChanged;
	OutSelectionChanged = SelectionChanged;
}


FTimespan FDirectShowMediaTracks::GetDuration() const
{
	//FScopeLock Lock(&CriticalSection);

	// TODO: maube this?
	if (CurrentVideoDevice == NULL)
	{
		return FTimespan::Zero();
	}
	
	return FTimespan::Zero();
}

void FDirectShowMediaTracks::OnVideoTracksUpdated(uint32 SelectedIndex)
{
	if(!CurrentVideoDevice)
		return;
	
	FScopeLock Lock(&CriticalSection);
	// TODO: only video tracks are implemented and filled
	VideoTracks.Empty();
	
	TArray<FDShowTrack>& newVideoTracks = CurrentVideoDevice->GetVideoTracks();
	for(auto elem : newVideoTracks)
	{
		VideoTracks.Add(elem);
	}

	if(VideoTracks.Num() > 0)
		SelectedVideoTrack = 0;

	VideoTracks[SelectedVideoTrack].SelectedFormat = (SelectedIndex < 0) ? 0 : SelectedIndex;
	UE_LOG(LogDirectShowMedia, Log, TEXT("VideoTracks[SelectedVideoTrack].SelectedFormat = %d"), SelectedIndex)
}

void FDirectShowMediaTracks::OnAudioTracksUpdated(uint32 SelectedIndex)
{
	if(!CurrentVideoDevice)
		return;
	
	FScopeLock Lock(&CriticalSection);
	// TODO: only Audio tracks are implemented and filled
	AudioTracks.Empty();
	
	TArray<FDShowTrack>& newAudioTracks = CurrentVideoDevice->GetAudioTracks();
	for(auto elem : newAudioTracks)
	{
		AudioTracks.Add(elem);
	}

	if(AudioTracks.Num() > 0)
		SelectedAudioTrack = 0;

	AudioTracks[SelectedAudioTrack].SelectedFormat = SelectedIndex;
	UE_LOG(LogDirectShowMedia, Log, TEXT("AudioTracks[SelectedAudioTrack].SelectedFormat = %d"), SelectedIndex)
}

void FDirectShowMediaTracks::SetSessionState(EMediaState InState)
{
	UE_LOG(LogDirectShowMedia, VeryVerbose, TEXT("FDirectShowMediaTracks::SetSessionState %d"), InState);
	LastState = CurrentState;
	CurrentState = InState;
}

void FDirectShowMediaTracks::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
	TargetTime = Timecode;

	double time = Timecode.GetTotalSeconds();
	UE_LOG(LogDirectShowMedia, VeryVerbose, TEXT("Tracks: %p: TimeCode %.3f"), this, (float)time);
}


/* IMediaSamples interface
 *****************************************************************************/




bool FDirectShowMediaTracks::FetchCaption(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample)
{
	TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe> Sample;

	if (!CaptionSampleQueue.Peek(Sample))
	{
		return false;
	}

	const FTimespan SampleTime = Sample->GetTime().Time;

	if (!TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
	{
		return false;
	}

	if (!CaptionSampleQueue.Dequeue(Sample))
	{
		return false;
	}

	OutSample = Sample;

	return true;
}


bool FDirectShowMediaTracks::FetchMetadata(TRange<FTimespan> TimeRange, TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>& OutSample)
{
	TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe> Sample;

	if (!MetadataSampleQueue.Peek(Sample))
	{
		return false;
	}

	const FTimespan SampleTime = Sample->GetTime().Time;

	if (!TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
	{
		return false;
	}

	if (!MetadataSampleQueue.Dequeue(Sample))
	{
		return false;
	}

	OutSample = Sample;

	return true;
}

bool FDirectShowMediaTracks::FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample)
{
	TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe> Sample;

	if (!AudioSampleQueue.Peek(Sample))
	{
		return false;
	}

	const FTimespan SampleTime = Sample->GetTime().Time;

	if (!TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
	{
		return false;
	}

	if (!AudioSampleQueue.Dequeue(Sample))
	{
		return false;
	}

	OutSample = Sample;

	return true;
}

bool FDirectShowMediaTracks::FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample)
{
	TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;

	if (!VideoSampleQueue.Peek(Sample))
	{
		return false;
	}

	const FTimespan SampleTime = Sample->GetTime().Time;
	
	if (!TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
	{
		return false;
	}

	if (!VideoSampleQueue.Dequeue(Sample))
	{
		return false;
	}
	
	OutSample = Sample;

	return true;
}


void FDirectShowMediaTracks::FlushSamples()
{
	UE_LOG(LogDirectShowMedia, VeryVerbose, TEXT("FDirectShowMediaTracks::FlushSamples"));
	AudioSampleQueue.RequestFlush();
	CaptionSampleQueue.RequestFlush();
	MetadataSampleQueue.RequestFlush();
	VideoSampleQueue.RequestFlush();
}


bool FDirectShowMediaTracks::PeekVideoSampleTime(FMediaTimeStamp & TimeStamp)
{
	TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
	if (!VideoSampleQueue.Peek(Sample))
	{
		return false;
	}
	TimeStamp = FMediaTimeStamp(Sample->GetTime());
	return true;
}

/* IMediaTracks interface
 *****************************************************************************/

bool FDirectShowMediaTracks::GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const
{
	//FScopeLock Lock(&CriticalSection);

	if (!CurrentVideoDevice || CurrentState == EMediaState::Preparing)
	{
		return false;
	}

	const FDShowFormat* Format = GetAudioFormat(TrackIndex, FormatIndex);
	
	if (Format == nullptr)
	{
		return false; // format not found
	}

	OutFormat.BitsPerSample = Format->Audio.BitsPerSample;
	OutFormat.NumChannels = Format->Audio.NumChannels;
	OutFormat.SampleRate = Format->Audio.SampleRate;
	OutFormat.TypeName = Format->TypeName;

	return true;
}


int32 FDirectShowMediaTracks::GetNumTracks(EMediaTrackType TrackType) const
{
	//FScopeLock Lock(&CriticalSection);
	if (!CurrentVideoDevice)
	{
		return 0;
	}
	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		return AudioTracks.Num();

	case EMediaTrackType::Metadata:
		return MetadataTracks.Num();

	case EMediaTrackType::Caption:
		return CaptionTracks.Num();

	case EMediaTrackType::Video:
		return VideoTracks.Num();

	default:
		break; // unsupported track type
	}

	return 0;
}


int32 FDirectShowMediaTracks::GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const
{
	//FScopeLock Lock(&CriticalSection);
	if (!CurrentVideoDevice)
	{
		return 0;
	}
	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return AudioTracks[TrackIndex].Formats.Num();
		}

	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return 1;
		}

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return 1;
		}

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return VideoTracks[TrackIndex].Formats.Num();
		}

	default:
		break; // unsupported track type
	}

	return 0;
}


int32 FDirectShowMediaTracks::GetSelectedTrack(EMediaTrackType TrackType) const
{
	if (!CurrentVideoDevice)
	{
		return INDEX_NONE;
	}

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		return SelectedAudioTrack;

	case EMediaTrackType::Caption:
		return SelectedCaptionTrack;

	case EMediaTrackType::Metadata:
		return SelectedMetadataTrack;

	case EMediaTrackType::Video:
		return SelectedVideoTrack;

	default:
		break; // unsupported track type
	}

	return INDEX_NONE;
}


FText FDirectShowMediaTracks::GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	//FScopeLock Lock(&CriticalSection);
	if (!CurrentVideoDevice)
	{
		return FText::GetEmpty();
	}

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return AudioTracks[TrackIndex].DisplayName;
		}
		break;
	
	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return MetadataTracks[TrackIndex].DisplayName;
		}
		break;

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return CaptionTracks[TrackIndex].DisplayName;
		}
		break;

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return VideoTracks[TrackIndex].DisplayName;
		}
		break;

	default:
		break; // unsupported track type
	}

	return FText::GetEmpty();
}


int32 FDirectShowMediaTracks::GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const
{
	//FScopeLock Lock(&CriticalSection);
	if (!CurrentVideoDevice)
	{
		return INDEX_NONE;
	}

	const FDShowTrack* Track = GetTrack(TrackType, TrackIndex);
	return (Track != nullptr) ? Track->SelectedFormat : INDEX_NONE;
}


FString FDirectShowMediaTracks::GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const
{
	//FScopeLock Lock(&CriticalSection);
	if (!CurrentVideoDevice)
	{
		return FString();
	}

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return AudioTracks[TrackIndex].Language;
		}
		break;

	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return MetadataTracks[TrackIndex].Language;
		}
		break;

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return CaptionTracks[TrackIndex].Language;
		}
		break;

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return VideoTracks[TrackIndex].Language;
		}
		break;

	default:
		break; // unsupported track type
	}

	return FString();
}


FString FDirectShowMediaTracks::GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	//FScopeLock Lock(&CriticalSection);

	if (!CurrentVideoDevice)
	{
		return FString();
	}

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return AudioTracks[TrackIndex].Name;
		}
		break;

	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return MetadataTracks[TrackIndex].Name;
		}
		break;

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return CaptionTracks[TrackIndex].Name;
		}
		break;

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return VideoTracks[TrackIndex].Name;
		}
		break;

	default:
		break; // unsupported track type
	}

	return FString();
}


bool FDirectShowMediaTracks::GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const
{
	//FScopeLock Lock(&CriticalSection);
	if (!CurrentVideoDevice)
	{
		return false;
	}

	const FDShowFormat* Format = GetVideoFormat(TrackIndex, FormatIndex);
	
	if (Format == nullptr)
	{
		return false; // format not found
	}

	OutFormat.Dim = Format->Video.OutputDim;
	OutFormat.FrameRate = Format->Video.FrameRate;
	if(CurrentVideoDevice)
	{
		OutFormat.FrameRate = CurrentVideoDevice->GetFramerate();
	}
	
	OutFormat.FrameRates = Format->Video.FrameRates;
	OutFormat.TypeName = Format->TypeName;

	return true;
}


bool FDirectShowMediaTracks::SelectTrack(EMediaTrackType TrackType, int32 TrackIndex)
{
	// TODO: verify if this check is good
	if (CurrentVideoDevice == nullptr)
	{
		return false; // not initialized
	}

	UE_LOG(LogDirectShowMedia, Verbose, TEXT("Tracks %p: Selecting %s track %i"), this, *MediaUtils::TrackTypeToString(TrackType), TrackIndex);

	//FScopeLock Lock(&CriticalSection);

	int32* SelectedTrack = nullptr;
	TArray<FDShowTrack>* Tracks = nullptr;

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		SelectedTrack = &SelectedAudioTrack;
		Tracks = &AudioTracks;
		break;

	case EMediaTrackType::Caption:
		SelectedTrack = &SelectedCaptionTrack;
		Tracks = &CaptionTracks;
		break;

	case EMediaTrackType::Metadata:
		SelectedTrack = &SelectedMetadataTrack;
		Tracks = &MetadataTracks;
		break;

	case EMediaTrackType::Video:
		SelectedTrack = &SelectedVideoTrack;
		Tracks = &VideoTracks;
		break;

	default:
		return false; // unsupported track type
	}

	check(SelectedTrack != nullptr);
	check(Tracks != nullptr);

	if (TrackIndex == *SelectedTrack)
	{
		return true; // already selected
	}

	if ((TrackIndex != INDEX_NONE) && !Tracks->IsValidIndex(TrackIndex))
	{
		return false; // invalid track
	}

	if(TrackIndex > 0)
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("Multiple tracks unsupported in Direct Show"));

		return false;
	}
	

	return true;
}


bool FDirectShowMediaTracks::SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex)
{
	UE_LOG(LogDirectShowMedia, Verbose, TEXT("Tracks %p: Setting format on %s track %i to %i"), this, *MediaUtils::TrackTypeToString(TrackType), TrackIndex, FormatIndex);
	if (!CurrentVideoDevice)
	{
		return false;
	}

	TArray<FDShowTrack>* Tracks = nullptr;

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		Tracks = &AudioTracks;
		break;

	case EMediaTrackType::Caption:
		Tracks = &CaptionTracks;
		break;

	case EMediaTrackType::Metadata:
		Tracks = &MetadataTracks;
		break;

	case EMediaTrackType::Video:
		Tracks = &VideoTracks;
		break;

	default:
		return false; // unsupported track type
	};

	check(Tracks != nullptr);

	if (!Tracks->IsValidIndex(TrackIndex))
	{
		return false; // invalid track index
	}

	FDShowTrack& Track = (*Tracks)[TrackIndex];

	// TODO: fix this when figure out why selectedFormat keeps becoming 0
	if (Track.SelectedFormat == FormatIndex)
	{
		return true; // format already set
	}

	if (!Track.Formats.IsValidIndex(FormatIndex))
	{
		return false; // invalid format index
	}

	if(!CurrentVideoDevice)
		return false;

 	CurrentState = EMediaState::Stopped;

	{
		FScopeLock Lock(&CriticalSection);
		VideoSampleQueue.RequestFlush();
		AudioSampleQueue.RequestFlush();
	}

	// Device will check redundancies
	if(!CurrentVideoDevice->SetFormatInfo(SourceUrl, Track.Formats[FormatIndex]))
	{
		UE_LOG(LogDirectShowMedia, Error, TEXT("failed to set formatInfo on current video device"));
		Shutdown();
		return false;
	}
	//FScopeLock Lock(&CriticalSection);
	
 	CurrentState = EMediaState::Preparing;
	
	// set track format
	UE_LOG(LogDirectShowMedia, Verbose, TEXT("Tracks %p: Set format %i instead of %i on %s track %i (%i formats)"), this, FormatIndex, Track.SelectedFormat, *MediaUtils::TrackTypeToString(TrackType), TrackIndex, Track.Formats.Num());

	Track.SelectedFormat = FormatIndex;
	SelectionChanged = true;

	return true;
}


bool FDirectShowMediaTracks::SetVideoTrackFrameRate(int32 TrackIndex, int32 FormatIndex, float FrameRate)
{
	UE_LOG(LogDirectShowMedia, Verbose, TEXT("Tracks %p: Setting frame rate on format %i of video track %i to %f"), this, FormatIndex, TrackIndex, FrameRate);

	//FScopeLock Lock(&CriticalSection);
	if (!CurrentVideoDevice)
	{
		return false;
	}

	FDShowFormat* Format = GetVideoFormat(TrackIndex, FormatIndex);

	if (Format == nullptr || !CurrentVideoDevice)
	{
		return false; // format not found
	}
	
	// TODO: call capture to ovveride framerate?
	if(!CurrentVideoDevice->RequestFrameRateChange(TrackIndex, FormatIndex, FrameRate))
	{
		return false;
	}
	VideoTracks[TrackIndex].Formats[FormatIndex].Video.FrameRate = FrameRate;
	
	if(VideoTracks[TrackIndex].SelectedFormat == FormatIndex)
	{
		VideoTracks[TrackIndex].SelectedFormat = -1; // set to something else so update goes trough
		SetTrackFormat(EMediaTrackType::Video, TrackIndex, FormatIndex);
	}

	return  true;
}

bool FDirectShowMediaTracks::CanControl(EMediaControl inControl) const
{
	if (!CurrentVideoDevice)
	{
		return false;
	}

	if (inControl == EMediaControl::Pause)
	{
		return (CurrentState == EMediaState::Playing);
	}

	if (inControl == EMediaControl::Resume)
	{
		return (CurrentState != EMediaState::Playing);
	}

	if ((inControl == EMediaControl::Scrub) || (inControl == EMediaControl::Seek))
	{
		return true;
	}

	return false;
}

float FDirectShowMediaTracks::GetRate() const
{
	return CurrentRate;
}

EMediaState FDirectShowMediaTracks::GetState() const
{
	return CurrentState;
}

EMediaStatus FDirectShowMediaTracks::GetStatus() const
{
	// TODO:?
	return EMediaStatus::None;
}

TRangeSet<float> FDirectShowMediaTracks::GetSupportedRates(EMediaRateThinning Thinning) const
{
	TRangeSet<float> Result;
	Result.Add(TRange<float>::Inclusive(0.0f, 1.0f));

	return Result;
}

FTimespan FDirectShowMediaTracks::GetTime() const
{
	return CurrentTime;
}

bool FDirectShowMediaTracks::IsLooping() const
{
	return ShouldLoop;
}

bool FDirectShowMediaTracks::Seek(const FTimespan& Time)
{
	return false;
}

bool FDirectShowMediaTracks::SetLooping(bool Looping)
{
	ShouldLoop = Looping;   
	return true;
}

bool FDirectShowMediaTracks::SetRate(float Rate)
{
	CurrentRate = Rate;

	if (FMath::IsNearlyZero(Rate))
	{
		CurrentState = EMediaState::Paused;
		DeferredEvents.Enqueue(EMediaEvent::PlaybackSuspended);
	}
	else
	{
		CurrentState = EMediaState::Playing;
		DeferredEvents.Enqueue(EMediaEvent::PlaybackResumed);
	}
	
	return true;
}

const FDShowFormat* FDirectShowMediaTracks::GetAudioFormat(int32 TrackIndex, int32 FormatIndex) const
{
	if (AudioTracks.IsValidIndex(TrackIndex))
	{
		const FDShowTrack& Track = AudioTracks[TrackIndex];

		if (Track.Formats.IsValidIndex(FormatIndex))
		{
			return &Track.Formats[FormatIndex];
		}
	}

	return nullptr;
}


const FDShowTrack* FDirectShowMediaTracks::GetTrack(EMediaTrackType TrackType, int32 TrackIndex) const
{
	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return &AudioTracks[TrackIndex];
		}

	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return &MetadataTracks[TrackIndex];
		}

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return &CaptionTracks[TrackIndex];
		}

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return &VideoTracks[TrackIndex];
		}

	default:
		break; // unsupported track type
	}

	return nullptr;
}


FDShowFormat* FDirectShowMediaTracks::GetVideoFormat(int32 TrackIndex, int32 FormatIndex)
{
	if (VideoTracks.IsValidIndex(TrackIndex))
	{
		FDShowTrack& Track = VideoTracks[TrackIndex];

		if (Track.Formats.IsValidIndex(FormatIndex))
		{
			return &Track.Formats[FormatIndex];
		}
	}

	return nullptr;
}

const FDShowFormat* FDirectShowMediaTracks::GetVideoFormat(int32 TrackIndex, int32 FormatIndex) const
{
	if (VideoTracks.IsValidIndex(TrackIndex))
	{
		const FDShowTrack& Track = VideoTracks[TrackIndex];

		if (Track.Formats.IsValidIndex(FormatIndex))
		{
			return &Track.Formats[FormatIndex];
		}
	}

	return nullptr;
}


/* FDirectShowMediaTracks callbacks
 *****************************************************************************/

void FDirectShowMediaTracks::HandleMediaSamplerClock(EDirectShowMediaSamplerClockEvent Event, EMediaTrackType TrackType)
{
	// IMFSampleGrabberSinkCallback callbacks seem to be broken (always returns Stopped)
	// We handle sink synchronization via SetPaused() as a workaround
}





void FDirectShowMediaTracks::HandleMediaSamplerCaptionSample(const uint8* Buffer, uint32 Size, FTimespan inDuration, FTimespan Time)
{
	if (Buffer == nullptr)
	{
		return;
	}

	FScopeLock Lock(&CriticalSection);

	if (!CaptionTracks.IsValidIndex(SelectedCaptionTrack))
	{
		return; // invalid track index
	}

	if (CaptionSampleQueue.Num() >= FMediaPlayerQueueDepths::MaxCaptionSinkDepth)
	{
		return;
	}

	// create & add sample to queue
	const FDShowTrack& Track = CaptionTracks[SelectedCaptionTrack];
	const auto CaptionSample = MakeShared<FDirectShowMediaOverlaySample, ESPMode::ThreadSafe>();

	if (CaptionSample->Initialize((char*)Buffer, Time, inDuration))
	{
		CaptionSampleQueue.Enqueue(CaptionSample);
	}
}


void FDirectShowMediaTracks::HandleMediaSamplerMetadataSample(const uint8* Buffer, uint32 Size, FTimespan inDuration, FTimespan Time)
{
	if (Buffer == nullptr)
	{
		return;
	}

	FScopeLock Lock(&CriticalSection);

	if (!MetadataTracks.IsValidIndex(SelectedMetadataTrack))
	{
		return; // invalid track index
	}

	if (MetadataSampleQueue.Num() >= FMediaPlayerQueueDepths::MaxMetadataSinkDepth)
	{
		return;
	}

	// create & add sample to queue
	const FDShowTrack& Track = MetadataTracks[SelectedMetadataTrack];
	const auto BinarySample = MakeShared<FDirectShowMediaBinarySample, ESPMode::ThreadSafe>();

	if (BinarySample->Initialize(Buffer, Size, Time, inDuration))
	{
		MetadataSampleQueue.Enqueue(BinarySample);
	}
}

void FDirectShowMediaTracks::HandleMediaSamplerAudioSample(double Time, IMediaSample* Sample)
{
	if (!Sample || !CurrentVideoDevice || !CurrentVideoDevice->bIsInitialized || CurrentState == EMediaState::Stopped)
	{
		return;
	}
	
	BYTE* pBuffer = nullptr;
	HRESULT hr = Sample->GetPointer(&pBuffer);
	if(hr != S_OK)
		return;
	
	long Size = Sample->GetActualDataLength();

	REFERENCE_TIME start, stop;
 // duration in 100-nanosecond units
	hr = Sample->GetTime(&start, &stop);
	if (hr != S_OK) {
		return;
	}
	float diff = (float) (stop - start) / 10000000.0f;
	FTimespan durationTimespan = FTimespan(diff * ETimespan::TicksPerSecond);

	FScopeLock Lock(&CriticalSection);
	
	if (AudioSampleQueue.Num() >= 1)
	{
		AudioSampleQueue.RequestFlush();
		
		return;
	}

	long long startTime, stopTime;
	hr = Sample->GetTime(&startTime, &stopTime);
	if(hr != S_OK)
		return;
	
	FTimespan startTimespan(startTime);
	FTimespan stopTimespan(stopTime);
	FTimespan duration = stopTimespan - startTimespan;
	
	// UE_LOG(LogDirectShowMedia, Warning, TEXT("Audio cbTime: %f startTime: %s, stopTime: %s, Duraition: %s"),Time, *startTimespan.ToString(), *stopTimespan.ToString(), *duration.ToString())

	// create & add sample to queue
	const TSharedRef<FDirectShowMediaAudioSample, ESPMode::ThreadSafe> AudioSample = AudioSamplePool->AcquireShared();

	
	// TODO: finish this
	if (AudioSample->Initialize(pBuffer, Size, CurrentVideoDevice->GetNumChannels(), CurrentVideoDevice->GetSampleRate(), CurrentVideoDevice->GetCurrentAudioSampleFormat(), Time, durationTimespan))
	{
		AudioSampleQueue.Enqueue(AudioSample);
		
	}
}

void FDirectShowMediaTracks::HandleMediaSamplerVideoSample(double Time, IMediaSample* Sample)
{
	if (!Sample || !CurrentVideoDevice|| !CurrentVideoDevice->bIsInitialized || CurrentState == EMediaState::Stopped)
		return;
	
	BYTE* pBuffer = nullptr;
	HRESULT hr = Sample->GetPointer(&pBuffer);
	if(hr != S_OK)
		return;
	
	long Size = Sample->GetActualDataLength();
	void* inBuffer = pBuffer;		
	
	// DirectShow doesn't report durations for some formats
	if (Duration.IsZero())
	{
		float FrameRate = CurrentVideoDevice->GetFramerate();
		if (FrameRate <= 0.0f)
		{
			FrameRate = 30.0f;
		}
	
		Duration = FTimespan((int64)((float)ETimespan::TicksPerSecond / FrameRate));
	}
	
	FIntPoint Dim;
	uint32 Stride = 0;
	EMediaTextureSampleFormat Format;
	FIntPoint Resolution =  CurrentVideoDevice->GetTextureSize();
	GUID subtype = CurrentVideoDevice->GetCurrentSubtype();
	
	
	// Handle decoding of compressed formats (e.g. MJPG)
	if(subtype == MEDIASUBTYPE_MJPG)
	{			
		Dim = Resolution;
		Stride = Resolution.X * 4;
		Format = EMediaTextureSampleFormat::CharBGRA;

	}
	else if(subtype == MEDIASUBTYPE_NV12)
	{
		Dim = FIntPoint(Resolution.X, Resolution.Y * 3 / 2);
		Stride = Resolution.X;
		Format = EMediaTextureSampleFormat::CharNV12;
	}
	else if (subtype == MEDIASUBTYPE_RGB32 || subtype == MEDIASUBTYPE_ARGB32)
	{
		Dim = Resolution;
		Stride = Resolution.X * 4;
		Format = EMediaTextureSampleFormat::CharBGRA;
	}
	else if (subtype == MEDIASUBTYPE_UYVY)
	{
		Dim = FIntPoint(Resolution.X / 2, Resolution.Y);
		Stride = Resolution.X * 2;
		Format = EMediaTextureSampleFormat::CharUYVY;
	}
	else if (subtype == MEDIASUBTYPE_H264)
	{
		Dim = Resolution;
		Stride = Resolution.X * 4;
		Format = EMediaTextureSampleFormat::CharBGRA;
	}
	else if (subtype == MEDIASUBTYPE_YUY2)
	{
		int32 AlignedOutputX = Resolution.X;

		int32 SampleStride = AlignedOutputX * 2; // 2 bytes per pixel

		if (SampleStride < 0)
		{
			SampleStride = -SampleStride;
		}
		
		Dim = FIntPoint(AlignedOutputX / 2, Resolution.Y);
		Stride = SampleStride;
		Format = EMediaTextureSampleFormat::CharYUY2;
	}
	else
	{
		// Don't process any unsupported formats, unexpected bahaviors can come
		return;
	}
	
	FTimespan inTime(ETimespan::TicksPerSecond * Time);

	long long startTime, stopTime;
	hr = Sample->GetTime(&startTime, &stopTime);
	if(hr != S_OK)
		return;
	
	 FTimespan startTimespan(startTime);
	 FTimespan stopTimespan(stopTime);
	 FTimespan duration = stopTimespan - startTimespan;
	
	// UE_LOG(LogDirectShowMedia, Warning, TEXT("Video cbTime: %f startTime: %s, stopTime: %s, Duraition: %s"),Time, *startTimespan.ToString(), *stopTimespan.ToString(), *duration.ToString())
	
	//UE_LOG(LogDirectShowMedia, Warning, TEXT("Handle incoming sample:\nstartTime: %s    EndTime: %s    Duration: %s\nResolution:%s\nSize: %d\nDim: %s\nStride: %d\n %d * %d > %d\nType: %s"), *startTimespan.ToString(), *stopTimespan.ToString(), *duration.ToString(), *Resolution.ToString(), Size, *Dim.ToString(), Stride, Stride, Dim.Y, Size, *CurrentVideoDevice->GetFormatTypeFromGUID(subtype))
	
	FScopeLock Lock(&CriticalSection);

	CurrentTime = FTimespan((int64)((float)ETimespan::TicksPerSecond * Time));
	
	if(VideoSampleQueue.Num() >= FMediaPlayerQueueDepths::MaxVideoSinkDepth)
	{
		VideoSampleQueue.RequestFlush();
		return;
	}
	
	const TSharedRef<FDirectShowMediaTextureSample, ESPMode::ThreadSafe> TextureSample = VideoSamplePool->AcquireShared();
	if (TextureSample->Initialize(
		inBuffer,
		Size,
		Dim,
		Resolution,
		Format,
		Stride,
		inTime, 
		Duration))
	{
		VideoSampleQueue.Enqueue(TextureSample);
	} 	
}

#undef LOCTEXT_NAMESPACE

