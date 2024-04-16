#pragma once

#include "IDirectShowMedia.h"
#include "IMediaCaptureSupport.h"
#include "Runtime/Engine/Classes/Engine/Texture2D.h"

#ifndef MAX_DEVICES
#define MAX_DEVICES 16
#endif

#ifndef MAX_DEVICE_NAME
#define MAX_DEVICE_NAME 80
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogDirectShowMedia, Log, All);

class FDirectShowVideoDevice;

class FDirectShowMediaModule
	: public IDirectShowMediaModule
{

	/************************************************************************/
	/* IWebcamera                                                           */
	/************************************************************************/
	public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	* Enumerate available audio capture devices.
	*
	* @param OutDeviceInfos Will contain information about the devices.
	* @see EnumerateVideoCaptureDevices
	*/
	virtual void EnumerateAudioCaptureDevices(TArray<FMediaCaptureDeviceInfo>& OutDeviceInfos) override;

	/**
	 * Enumerate available video capture devices.
	 *
	 * @param OutDeviceInfos Will contain information about the devices.
	 * @see EnumerateAudioCaptureDevices
	 */
	virtual void EnumerateVideoCaptureDevices(TArray<FMediaCaptureDeviceInfo>& OutDeviceInfos) override;
	
	/**
	 * Creates a Direct Show based media player.
	 *
	 * @param EventSink The object that receives media events from the player.
	 * @return A new media player, or nullptr if a player couldn't be created.
	 */
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(ISinkMediaEvent& EventSink) override;

private:

	bool Initialized = false;
};