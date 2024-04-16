// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMediaCaptureSupport.h"
#include "IMediaEventSink.h"
#include "Modules/ModuleInterface.h"

class IMediaPlayer;


class IDirectShowMediaModule 
	:  public IMediaCaptureSupport,
	public IModuleInterface
{
public:

	/** Virtual destructor. */
	virtual ~IDirectShowMediaModule() { }

	/**
	* Enumerate available audio capture devices.
	*
	* @param OutDeviceInfos Will contain information about the devices.
	 * @see EnumerateVideoCaptureDevices
	*/
	virtual void EnumerateAudioCaptureDevices(TArray<FMediaCaptureDeviceInfo>& OutDeviceInfos) = 0;

	/**
	 * Enumerate available video capture devices.
	 *
	 * @param OutDeviceInfos Will contain information about the devices.
	 * @see EnumerateAudioCaptureDevices
	 */
	virtual void EnumerateVideoCaptureDevices(TArray<FMediaCaptureDeviceInfo>& OutDeviceInfos) = 0;
	
	/**
	 * Creates a Direct Show based media player.
	 *
	 * @param EventSink The object that receives media events from the player.
	 * @return A new media player, or nullptr if a player couldn't be created.
	 */
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(ISinkMediaEvent& EventSink) = 0;
	
};
