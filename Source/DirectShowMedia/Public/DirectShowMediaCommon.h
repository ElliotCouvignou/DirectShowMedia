#pragma once

#include "Logging/LogMacros.h"
#include "Windows/AllowWindowsPlatformTypes.h" 
//#include <windows.h>
#include <dshow.h>
//#include <comdef.h>

#include "IMediaAudioSample.h"
#include "Microsoft/COMPointer.h"
#include "Windows/HideWindowsPlatformTypes.h"

#define SKIP_DXTRANS
#define _CRT_SECURE_NO_WARNINGS


// DShow COM types
interface ISampleGrabberCB : public IUnknown
{
	virtual STDMETHODIMP SampleCB(double SampleTime, IMediaSample *pSample) = 0;
	virtual STDMETHODIMP BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen) = 0;
};

interface ISampleGrabber : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE *pType) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE *pType) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long *pBufferSize, long *pBuffer) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample **ppSample) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB *pCallback, long WhichMethodToCallback) = 0;
};

static const IID IID_ISampleGrabberCB = { 0x0579154A, 0x2B53, 0x4994,{ 0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85 } };
static const IID IID_ISampleGrabber = { 0x6B652FFF, 0x11FE, 0x4fce,{ 0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F } };
static const CLSID CLSID_SampleGrabber = { 0xC1F400A0, 0x3F08, 0x11d3,{ 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const CLSID CLSID_NullRenderer = { 0xC1F400A4, 0x3F08, 0x11d3,{ 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const CLSID CLSID_VideoEffects1Category = { 0xcc7bfb42, 0xf175, 0x11d1,{ 0xa3, 0x92, 0x0, 0xe0, 0x29, 0x1f, 0x39, 0x59 } };
static const CLSID CLSID_VideoEffects2Category = { 0xcc7bfb43, 0xf175, 0x11d1,{ 0xa3, 0x92, 0x0, 0xe0, 0x29, 0x1f, 0x39, 0x59 } };
static const CLSID CLSID_AudioEffects1Category = { 0xcc7bfb44, 0xf175, 0x11d1,{ 0xa3, 0x92, 0x0, 0xe0, 0x29, 0x1f, 0x39, 0x59 } };
static const CLSID CLSID_AudioEffects2Category = { 0xcc7bfb45, 0xf175, 0x11d1,{ 0xa3, 0x92, 0x0, 0xe0, 0x29, 0x1f, 0x39, 0x59 } };

FString GUIDToUEString(const GUID& guid);
FString CompressionToUEString(DWORD compression);
void LogAudioMediaType(const AM_MEDIA_TYPE& mt);
void LogVideoMediaType(const AM_MEDIA_TYPE& mt);


HRESULT SetPinMediaType(IPin* pPin, const GUID& subtype);

bool IsPinConnected(IPin* Pin);
bool GetPin(TComPtr<IBaseFilter> pFilter, PIN_DIRECTION PinDir, IPin** Pin, bool RequestUnconnected = false);
bool GetPin(TComPtr<IBaseFilter> pFilter, PIN_DIRECTION PinDir, GUID MajorType, GUID Category, IPin** Pin);
bool GetPin(const FString& Url, const IID& clsidDeviceClass, const GUID& MajorType, PIN_DIRECTION PinDir, IPin** Pin);
bool TryGetAudioPinByFriendlyName(const FString& InFriendlyNam, IPin** Pin);

bool GetDeviceFriendlyName(const FString& Url, const IID& clsidDeviceClass, FString& OutFriendlyName);

// Audio things
EMediaAudioSampleFormat GetAudioSampleFormatBits(const WAVEFORMATEX* wfex);



// #if DIRECTSHOWMEDIA_SUPPORTED_PLATFORM
// 	//#include "DirectShowMediaSettings.h"
//
// 	#include "Windows/WindowsHWrapper.h"
// 	#include "Windows/AllowWindowsPlatformTypes.h"
//
// 	THIRD_PARTY_INCLUDES_START
//
// 	#include <windows.h>
// 	
// 	#include <D3D9.h>
// 	#include <mfapi.h>
// 	#include <mferror.h>
// 	#include <mfidl.h>
// 	#include <mmdeviceapi.h>
// 	#include <mmeapi.h>
// 	#include <nserror.h>
// 	#include <shlwapi.h>
//
// 	THIRD_PARTY_INCLUDES_END
//
// 	const GUID FORMAT_525WSS = { 0xc7ecf04d, 0x4582, 0x4869, { 0x9a, 0xbb, 0xbf, 0xb5, 0x23, 0xb6, 0x2e, 0xdf } };
// 	const GUID FORMAT_DvInfo = { 0x05589f84, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
// 	const GUID FORMAT_MPEG2Video = { 0xe06d80e3, 0xdb46, 0x11cf, { 0xb4, 0xd1, 0x00, 0x80, 0x05f, 0x6c, 0xbb, 0xea } };
// 	const GUID FORMAT_MPEGStreams = { 0x05589f83, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
// 	const GUID FORMAT_MPEGVideo = { 0x05589f82, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
// 	const GUID FORMAT_None = { 0x0F6417D6, 0xc318, 0x11d0, { 0xa4, 0x3f, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
// 	const GUID FORMAT_VideoInfo = { 0x05589f80, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
// 	const GUID FORMAT_VideoInfo2 = { 0xf72a76A0, 0xeb0a, 0x11d0, { 0xac, 0xe4, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
// 	const GUID FORMAT_WaveFormatEx = { 0x05589f81, 0xc356, 0x11ce,{ 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
// 	
// 	const GUID MR_VIDEO_ACCELERATION_SERVICE = { 0xefef5175, 0x5c7d, 0x4ce2, { 0xbb, 0xbd, 0x34, 0xff, 0x8b, 0xca, 0x65, 0x54 } };
//
// 	#if (WINVER < _WIN32_WINNT_WIN8)
// 		const GUID MF_LOW_LATENCY = { 0x9c27891a, 0xed7a, 0x40e1, { 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee } };
// 	#endif
//
// 	#include "Microsoft/COMPointer.h"
// 	#include "Windows/HideWindowsPlatformTypes.h"
//
// #elif PLATFORM_WINDOWS && !UE_SERVER
// 	#pragma message("Skipping DirectShowMedia (requires WINVER >= 0x0600, but WINVER is " PREPROCESSOR_TO_STRING(WINVER) ")")
//
// #endif //DIRECTSHOWMEDIA_SUPPORTED_PLATFORM


/** Log category for the DirectShowMedia module. */
