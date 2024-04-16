#pragma once

#include "Delegates/Delegate.h"
#include <guiddef.h>

#include "DirectShowMediaCommon.h"

#include "Runtime/Engine/Classes/Engine/Texture2D.h"
#include "Runtime/Core/Public/UObject/WeakObjectPtrTemplates.h"


DECLARE_DELEGATE_TwoParams(FOnSampleCB, double Time, IMediaSample* Sample);
DECLARE_DELEGATE_ThreeParams(FOnBufferCB, double Time, char* Buffer, long Length);

class FDirectShowVideoDevice;
typedef void(*FVideoCaptureCallback)(unsigned char* Data, int Length, int BitsPerPixel, FDirectShowVideoDevice* Device);


class FDirectShowCallbackHandler : public ISampleGrabberCB
{	
public:
	FDirectShowCallbackHandler();
	virtual ~FDirectShowCallbackHandler();

	virtual HRESULT __stdcall SampleCB(double Time, IMediaSample* Sample);
	virtual HRESULT __stdcall BufferCB(double Time, BYTE* Buffer, long Length);
	virtual HRESULT __stdcall QueryInterface(REFIID Iid, LPVOID *VoidPtrPtr);
	virtual ULONG __stdcall AddRef();
	virtual ULONG __stdcall Release();
	
	FOnSampleCB OnSampleCB;
	FOnBufferCB OnBufferCB;
	bool bflipVertically = false;
};
