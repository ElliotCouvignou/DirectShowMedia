// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <dsound.h>

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"
#include "IMediaControls.h"
#include "IMediaSamples.h"
#include "IMediaTracks.h"
#include "Math/IntPoint.h"
#include "MediaSampleQueue.h"
#include "Microsoft/COMPointer.h"
#include "Templates/SharedPointer.h"
  #include "Windows/AllowWindowsPlatformTypes.h"
  #include "Windows/WindowsHWrapper.h"
  #include "Windows/HideWindowsPlatformTypes.h"

class FDirectShowVideoDevice;
class FDirectShowAudioDevice;
enum class EMediaEvent;
class FDirectShowMediaAudioSamplePool;
class FDirectShowMediaSampler;
class FDirectShowMediaTextureSamplePool;
class IMediaAudioSample;
class IMediaBinarySample;
class IMediaOverlaySample;
class IMediaTextureSample;
struct IMpeg2Demultiplexer;
class FDirectShowMediaTextureSample;
class FMediaTimeStamp;
// class FFMPEGDecoder;
// class FFMPEGFrame;


enum class EMediaTextureSampleFormat;
enum class EMediaTrackType;
enum class EDirectShowMediaSamplerClockEvent;

struct FMediaPlayerOptions;
struct FDShowFormat;
struct FDShowTrack;
struct IMediaSample;
// struct AVCodecContext;
// struct AVFrame;


/**
 * Track collection for Windows Media Foundation based media players.
 */
class FDirectShowMediaTracks
	: public IMediaSamples
	, public IMediaTracks
	, public IMediaControls
{
	
public:

	/** Default constructor. */
	FDirectShowMediaTracks();

	/** Virtual destructor. */
	virtual ~FDirectShowMediaTracks();

public:

	/**
	 * Append track statistics information to the given string.
	 *
	 * @param OutStats The string to append the statistics to.
	 */
	void AppendStats(FString &OutStats) const;

	/**
	 * Clear the streams flags.
	 *
	 * @see GetFlags
	 */
	void ClearFlags();

	/**
	* Gets all deferred player events.
	*
	* @param OutEvents Will contain the events.
	* @see GetCapabilities
	*/
	void GetEvents(TArray<EMediaEvent>& OutEvents);

	/**
	 * Get the current flags.
	 *
	 * @param OutMediaSourceChanged Will indicate whether the media source changed.
	 * @param OutSelectionChanged Will indicate whether the track selection changed.
	 * @see ClearFlags
	 */
	void GetFlags(bool& OutMediaSourceChanged, bool& OutSelectionChanged) const;
	
	/**
	 * Get the information string for the currently loaded media source.
	 *
	 * @return Info string.
	 * @see GetDuration, GetSamples
	*/
	const FString& GetInfo() const
	{
		return Info;
	}
	
	/**
	 * Initialize the track collection.
	 *
	 * @param InMediaSource The media source object.
	 * @param Url The media source URL.
	 * @see IsInitialized, Shutdown
	 */
	void Initialize(const FString& Url, const class IMediaOptions* Options);

	bool IsDuplicateInitialize(const FString& Url, const class IMediaOptions* Options);
	

	/**
	 * Whether this object has been initialized.
	 *
	 * @return true if initialized, false otherwise.
	 * @see Initialize, Shutdown
	 */
	bool IsInitialized() const
	{
		return (CurrentVideoDevice != nullptr);
	}

	/**
	 * Shut down the track collection.
	 *
	 * @see Initialize, IsInitialized
	 */
	void Shutdown();

	/**
	 * Call this to pass on the session state to us.
	 *
	 * @param InState State of session.
	 */
	void SetSessionState(EMediaState InState);

	/**
	*
	*
	*/
	void TickInput(FTimespan DeltaTime, FTimespan Timecode);
public:

	//~ IMediaSamples interface

	virtual bool FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchCaption(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchMetadata(TRange<FTimespan> TimeRange, TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample) override;
	virtual void FlushSamples() override;
	virtual bool PeekVideoSampleTime(FMediaTimeStamp & TimeStamp) override;

public:

	//~ IMediaTracks interface

	virtual bool GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const override;
	virtual int32 GetNumTracks(EMediaTrackType TrackType) const override;
	virtual int32 GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual int32 GetSelectedTrack(EMediaTrackType TrackType) const override;
	virtual FText GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual int32 GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual FString GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual FString GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual bool GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const override;
	virtual bool SelectTrack(EMediaTrackType TrackType, int32 TrackIndex) override;
	virtual bool SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex) override;
	virtual bool SetVideoTrackFrameRate(int32 TrackIndex, int32 FormatIndex, float FrameRate) override;

	//~ IMediaControls interface

	virtual bool CanControl(EMediaControl Control) const override;
	virtual FTimespan GetDuration() const override;
	virtual float GetRate() const override;
	virtual EMediaState GetState() const override;
	virtual EMediaStatus GetStatus() const override;
	virtual TRangeSet<float> GetSupportedRates(EMediaRateThinning Thinning) const override;
	virtual FTimespan GetTime() const override;
	virtual bool IsLooping() const override;
	virtual bool Seek(const FTimespan& Time) override;
	virtual bool SetLooping(bool Looping) override;
	virtual bool SetRate(float Rate) override;

private:

	/**
	 * Get the specified audio format.
	 *
	 * @param TrackIndex Index of the audio track that contains the format.
	 * @param FormatIndex Index of the format to return.
	 * @return Pointer to format, or nullptr if not found.
	 * @see GetVideoFormat
	 */
	const FDShowFormat* GetAudioFormat(int32 TrackIndex, int32 FormatIndex) const;

	/**
	 * Get the specified track information.
	 *
	 * @param TrackType The type of track.
	 * @param TrackIndex Index of the track to return.
	 * @return Pointer to track, or nullptr if not found.
	 */
	const FDShowTrack* GetTrack(EMediaTrackType TrackType, int32 TrackIndex) const;

	/**
	 * Get the specified video format.
	 *
	 * @param TrackIndex Index of the video track that contains the format.
	 * @param FormatIndex Index of the format to return.
	 * @return Pointer to format, or nullptr if not found.
	 * @see GetAudioFormat
	 */
	FDShowFormat* GetVideoFormat(int32 TrackIndex, int32 FormatIndex);
	const FDShowFormat* GetVideoFormat(int32 TrackIndex, int32 FormatIndex) const;

private:

	/** Callback for handling media sampler pauses. */
	void HandleMediaSamplerClock(EDirectShowMediaSamplerClockEvent Event, EMediaTrackType TrackType);

	/** Callback for handling new samples from the streams' media sample buffers. */
	void HandleMediaSamplerAudioSample(double Time, IMediaSample* Sample);

	/** Callback for handling new caption samples. */
	void HandleMediaSamplerCaptionSample(const uint8* Buffer, uint32 Size, FTimespan inDuration, FTimespan Time);

	/** Callback for handling new metadata samples. */
	void HandleMediaSamplerMetadataSample(const uint8* Buffer, uint32 Size, FTimespan inDuration, FTimespan Time);

	
	/** Callback for handling new video samples. */
	void HandleMediaSamplerVideoSample(double Time, IMediaSample* Sample);
	
	void OnVideoTracksUpdated(uint32 SelectedIndex);
	void OnAudioTracksUpdated(uint32 SelectedIndex);


private:

	/** Synchronizes write access to track arrays, selections & sinks. */
	mutable FCriticalSection CriticalSection;

	/** Whether the media source / URL has changed. */
	bool MediaSourceChanged;
	
	/** The current playback rate. */
	float CurrentRate;
	
	/** Media information string. */
	FString Info;

	/** The initial media url. */
	FString SourceUrl;

	/** The initial media url. */
	FString DesiredAudioDevice = "";

	/** Whether the track selection changed. */
	bool SelectionChanged;

	/** Audio sample object pool. */
	FDirectShowMediaAudioSamplePool* AudioSamplePool;

	/** Audio sample queue. */
	TMediaSampleQueue<IMediaAudioSample> AudioSampleQueue;

	/** Overlay sample queue. */
	TMediaSampleQueue<IMediaOverlaySample> CaptionSampleQueue;

	/** Metadata sample queue. */
	TMediaSampleQueue<IMediaBinarySample> MetadataSampleQueue;
	
	/** Video sample object pool. */
	FDirectShowMediaTextureSamplePool* VideoSamplePool;

	/** Video sample queue. */
	TMediaSampleQueue<IMediaTextureSample> VideoSampleQueue;

	/** Index of the selected audio track. */
	int32 SelectedAudioTrack;

	/** Index of the selected caption track. */
	int32 SelectedCaptionTrack;

	/** Index of the selected binary track. */
	int32 SelectedMetadataTrack;

	/** Index of the selected video track. */
	int32 SelectedVideoTrack;

	/** The available audio tracks. */
	TArray<FDShowTrack> AudioTracks;
	/** The available caption tracks. */
	TArray<FDShowTrack> CaptionTracks;
	/** The available metadata tracks. */
	TArray<FDShowTrack> MetadataTracks;
	/** The available video tracks. */
	TArray<FDShowTrack> VideoTracks;


	/** Media events to be forwarded to main thread. */
	TQueue<EMediaEvent> DeferredEvents;

	/** Media playback state. */
	EMediaState CurrentState;

	EMediaState LastState;

	/** The current time of the playback. */
	FTimespan CurrentTime;
	
	/** Should the video loop to the beginning at completion */
	bool ShouldLoop;
	
	/** The duration of the media. */
	FTimespan Duration;

	FTimespan TargetTime;
	
	FDirectShowVideoDevice* CurrentVideoDevice;

	FThreadSafeBool bShuttingDown = false;
	FThreadSafeBool bIsInitializing = false;
	//FDirectShowAudioDevice* CurrentAudioDevice;

	//AVCodecContext* CodecContext;
	
	
};

