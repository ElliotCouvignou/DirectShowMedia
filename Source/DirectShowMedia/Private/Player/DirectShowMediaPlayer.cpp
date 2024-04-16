// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "DirectShowMediaPlayer.h"


#include "Async/Async.h"
#include "IMediaOptions.h"
#include "DirectShowMedia.h"

#include "DirectShowMediaTracks.h"
#include "IMediaEventSink.h"
//#include "DirectShowMediaSettings.h"


/* FWmfVideoPlayer structors
 *****************************************************************************/

FDirectShowMediaPlayer::FDirectShowMediaPlayer(ISinkMediaEvent& InEventSink)
	:
      EventSink(InEventSink)
	 , Tracks(MakeShared<FDirectShowMediaTracks, ESPMode::ThreadSafe>())
{
	check(Tracks.IsValid());
}


FDirectShowMediaPlayer::~FDirectShowMediaPlayer()
{
	Close();
}


/* IMediaPlayer interface
 *****************************************************************************/

void FDirectShowMediaPlayer::Close()
{
	// if (Tracks->GetState() == EMediaState::Closed)
	// {
	// 	return;
	// }
    
	// reset player
	MediaUrl = FString();
	Tracks->Shutdown();

	// notify listeners
 	EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
	EventSink.ReceiveMediaEvent(EMediaEvent::MediaClosed);
}


IMediaCache& FDirectShowMediaPlayer::GetCache()
{
	return *this;
}


IMediaControls& FDirectShowMediaPlayer::GetControls()
{
	return *Tracks;
}


FString FDirectShowMediaPlayer::GetInfo() const
{
	return Tracks->GetInfo();
}

FName FDirectShowMediaPlayer::GetPlayerName() const
{
	static FName PlayerName("DirectShowMedia");
	return PlayerName;
}


FGuid FDirectShowMediaPlayer::GetPlayerPluginGUID() const 
{
	// {D4A63E8F-83AB-4BCE-BEBA-7311A28D42A8}
	static FGuid PlayerPluginGUID(0xD4A63E8F, 0x83AB4BCE, 0xBEBB7311, 0xA28D42A8);
	return PlayerPluginGUID;
}


IMediaSamples& FDirectShowMediaPlayer::GetSamples()
{
	return *Tracks;
}


FString FDirectShowMediaPlayer::GetStats() const
{
	FString Result;
	Tracks->AppendStats(Result);

	return Result;
}


IMediaTracks& FDirectShowMediaPlayer::GetTracks()
{
	return *Tracks;
}


FString FDirectShowMediaPlayer::GetUrl() const
{
	return MediaUrl;
}


IMediaView& FDirectShowMediaPlayer::GetView()
{
	return *this;
}

bool FDirectShowMediaPlayer::Open(const FString& Url, const IMediaOptions* Options, const FMediaPlayerOptions* PlayerOptions) {
    Close();

    if (Url.IsEmpty())
    {
        return false;
    }

    //const bool Precache = (Options != nullptr) ? Options->GetMediaOption("PrecacheFile", false) : false;

    return InitializePlayer(nullptr, Url, false, PlayerOptions, Options);
}


bool FDirectShowMediaPlayer::Open(const FString& Url, const IMediaOptions* Options)
{
    return Open(Url, Options, nullptr);
}


bool FDirectShowMediaPlayer::Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options)
{
	Close();

    if (Archive->TotalSize() == 0)
    {
        UE_LOG(LogDirectShowMedia, Verbose, TEXT("Player %p: Cannot open media from archive (archive is empty)"), this);
        return false;
    }

    if (OriginalUrl.IsEmpty())
    {
        UE_LOG(LogDirectShowMedia, Verbose, TEXT("Player %p: Cannot open media from archive (no original URL provided)"), this);
        return false;
    }

    return InitializePlayer(Archive, OriginalUrl, false, nullptr, Options);
}


void FDirectShowMediaPlayer::TickFetch(FTimespan DeltaTime, FTimespan Timecode)
{
	bool MediaSourceChanged = false;
	bool TrackSelectionChanged = false;

	Tracks->GetFlags(MediaSourceChanged, TrackSelectionChanged);

	if (MediaSourceChanged)
	{
		EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
	}

	if (TrackSelectionChanged)
	{
		
	}

	if (MediaSourceChanged || TrackSelectionChanged)
	{
		Tracks->ClearFlags();
	}
}


void FDirectShowMediaPlayer::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
    Tracks->TickInput(DeltaTime, Timecode);
	
    // forward session events
    TArray<EMediaEvent> OutEvents;
    Tracks->GetEvents(OutEvents);

    for (const auto& Event : OutEvents)
    {
        EventSink.ReceiveMediaEvent(Event);
    }

    // process deferred tasks
    TFunction<void()> Task;

    while (PlayerTasks.Dequeue(Task))
    {
        Task();
    }
}



/* FDirectShowMediaPlayer implementation
 *****************************************************************************/

bool FDirectShowMediaPlayer::InitializePlayer(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive, const FString& Url, bool Precache, const FMediaPlayerOptions* PlayerOptions, const IMediaOptions* Options)
{
	UE_LOG(LogDirectShowMedia, Verbose, TEXT("Player %llx: Initializing %s (archive = %s, precache = %s)"), this, *Url, Archive.IsValid() ? TEXT("yes") : TEXT("no"), Precache ? TEXT("yes") : TEXT("no"));

	// const auto Settings = GetDefault<UDirectShowMediaSettings>();
	// check(Settings != nullptr);
    if (Tracks.IsValid())
    {
        if (Tracks->IsDuplicateInitialize(Url, Options))
        {
            return true;
        }
        else
        {
            Tracks->SetSessionState(EMediaState::Preparing);
        }
    }
	MediaUrl = Url;
     
     TFunction <void()>  Task =  [Archive, Url, Precache, PlayerOptions, Options, TracksPtr = TWeakPtr<FDirectShowMediaTracks, ESPMode::ThreadSafe>(Tracks), ThisPtr=this]()
     {
         //FScopeLock LoadLock(&ThisPtr->LoadFileSection);
         TSharedPtr<FDirectShowMediaTracks, ESPMode::ThreadSafe> PinnedTracks = TracksPtr.Pin();
         
         if (PinnedTracks.IsValid())
         {
             //ICaptureGraphBuilder2* Capture = ThisPtr->InitDirectShowURL(Archive, Url, Precache);
             //if (Capture) {
                 PinnedTracks->Initialize(Url, Options);
             //}
         }
     };
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, Task);
	return true;
}

// ICaptureGraphBuilder2* FDirectShowMediaPlayer::InitDirectShowURL(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive,
//     const FString& Url, bool Precache)
// {
// 	HRESULT HResult;
//
// 	HResult = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IFilterGraph2, (void**)&Graph);
// 	if (HResult < 0)
// 	{
// 		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to create filter graph: %x"), HResult);
// 		return nullptr;
// 	}
//
// 	HResult = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void**)&Capture);
// 	if (HResult < 0)
// 	{
// 		UE_LOG(LogDirectShowMedia, Error, TEXT("Failed to set filter graph: %x"), HResult);
//         return nullptr;
// 	}
//
// 	HResult = Graph->QueryInterface(IID_IMediaControl, (void**)&Control);
// 	if (HResult < 0)
// 	{
// 		throw HResult;
// 	}
//
// 	Capture->SetFiltergraph(Graph);
//
//     return Capture;
// }


