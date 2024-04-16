#include "DirectShowCallbackHandler.h"

#include <windows.h>
#include <dshow.h>
//#include <comdef.h>




FDirectShowCallbackHandler::FDirectShowCallbackHandler()
{
}

FDirectShowCallbackHandler::~FDirectShowCallbackHandler()
{
}

HRESULT FDirectShowCallbackHandler::SampleCB(double Time, IMediaSample* Sample)
{
	HRESULT HResult;
	unsigned char* Buffer;

	HResult = Sample->GetPointer((BYTE**)&Buffer);
	if (HResult != S_OK)
	{
		return S_OK;
	}

	OnSampleCB.ExecuteIfBound(Time, Sample);

	return S_OK;
}

HRESULT FDirectShowCallbackHandler::BufferCB(double Time, BYTE* Buffer, long Length)
{
	if(Buffer)
	{
		OnBufferCB.ExecuteIfBound(Time, (char*)Buffer, Length);
	}
	
	return S_OK;
}

HRESULT FDirectShowCallbackHandler::QueryInterface(const IID& Iid, LPVOID *VoidPtrPtr)
{
	if (Iid == IID_ISampleGrabberCB || Iid == IID_IUnknown)
	{
		*VoidPtrPtr = (void*) static_cast<ISampleGrabberCB*>(this);
		return S_OK;
	}

	return E_NOINTERFACE;
}

ULONG FDirectShowCallbackHandler::AddRef()
{
	return 1;
}

ULONG FDirectShowCallbackHandler::Release()
{
	return 2;
}