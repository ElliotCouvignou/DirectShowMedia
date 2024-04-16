// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

//#include "DirectShowMediaCommon.h"
#include "IMediaControls.h"

#include "IMediaCache.h"
#include "IMediaPlayer.h"
#include "IMediaView.h"
#include "IMediaEventSink.h"

class FDirectShowMediaTracks;


struct FMediaPlayerOptions;

/**
 * Implements a media player using the Windows Media Foundation framework.
 */
class FDirectShowMediaPlayer
	: public IMediaPlayer
	, protected IMediaCache
	, protected IMediaView
{
public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param InEventSink The object that receives media events from this player.
	 */
	FDirectShowMediaPlayer(ISinkMediaEvent& InEventSink);

	/** Virtual destructor. */
	virtual ~FDirectShowMediaPlayer();

public:

	//~ IMediaPlayer interface

	virtual void Close() override;
	virtual IMediaCache& GetCache() override;
	virtual IMediaControls& GetControls() override;
	virtual FString GetInfo() const override;
	virtual FName GetPlayerName() const;
	
	virtual FGuid GetPlayerPluginGUID() const override;
	virtual IMediaSamples& GetSamples() override;
	virtual FString GetStats() const override;
	virtual IMediaTracks& GetTracks() override;
	virtual FString GetUrl() const override;
	virtual IMediaView& GetView() override;
	virtual bool Open(const FString& Url, const IMediaOptions* Options) override;
	virtual bool Open(const FString& Url, const IMediaOptions* Options, const FMediaPlayerOptions* PlayerOptions) override;
	virtual bool Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options) override;

	
	virtual void TickFetch(FTimespan DeltaTime, FTimespan Timecode) override;
	virtual void TickInput(FTimespan DeltaTime, FTimespan Timecode) override;
	

	// virtual bool FlushOnSeekStarted() const override;
	// virtual bool FlushOnSeekCompleted() const override;
	// virtual bool GetPlayerFeatureFlag(EFeatureFlag flag) const override;


protected:

	/**
	 * Initialize the native AvPlayer instance.
	 *
	 * @param Archive The archive being used as a media source (optional).
	 * @param Url The media URL being opened.
	 * @param Precache Whether to precache media into RAM if InURL is a local file.
	 * @return true on success, false otherwise.
	 */
	bool InitializePlayer(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive, const FString& Url, bool Precache, const FMediaPlayerOptions* PlayerOptions, const IMediaOptions* Options);

	//ICaptureGraphBuilder2* InitDirectShowURL(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive, const FString& Url, bool Precache);
	
private:

	///** Tick the player. */
	//void Tick();


	/** The duration of the currently loaded media. */
	FTimespan Duration;

	/** The media event handler. */
	ISinkMediaEvent& EventSink;

	/** Tasks to be executed on the player thread. */
	TQueue<TFunction<void()>> PlayerTasks;

	/** The URL of the currently opened media. */
	FString MediaUrl;
	
	// IFilterGraph2* Graph;
	// ICaptureGraphBuilder2* Capture;
	// IMediaControl* Control;


	/** Media streams collection. */
	TSharedPtr<FDirectShowMediaTracks, ESPMode::ThreadSafe> Tracks;
};



