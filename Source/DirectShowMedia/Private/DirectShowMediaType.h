#pragma once

#include <dshow.h>
#include "DirectShow/DirectShow-1.0.0/src/Public/mtype.h"


class DShowMediaTypePtr
{
public:
	// Constructor
	DShowMediaTypePtr() : pMediaType_(nullptr) {}

	// Destructor
	~DShowMediaTypePtr() { Free(); }

	// Get the raw pointer
	AM_MEDIA_TYPE* Get() const { return pMediaType_; }
	AM_MEDIA_TYPE** GetPtr() { return &pMediaType_; }

	bool IsValid() const { return pMediaType_ != nullptr; }

	operator AM_MEDIA_TYPE**()  { return &pMediaType_; }
	operator AM_MEDIA_TYPE*() const { return pMediaType_; }
	operator AM_MEDIA_TYPE&() const { return *pMediaType_; }
	
	operator const AM_MEDIA_TYPE*() const { return pMediaType_; }
	operator const AM_MEDIA_TYPE&() const { return *pMediaType_; }
	operator  AM_MEDIA_TYPE*()  { return pMediaType_; }
	operator  AM_MEDIA_TYPE&()  { return *pMediaType_; }
	AM_MEDIA_TYPE* Ptr() const { return pMediaType_; }
    
	AM_MEDIA_TYPE* operator->() { return pMediaType_; }
	const AM_MEDIA_TYPE* operator->() const { return pMediaType_; }

	inline void operator=(AM_MEDIA_TYPE *ptr_)
	{
		Reset(ptr_);
	}

	inline bool operator==(const AM_MEDIA_TYPE *ptr_) const
	{
		return pMediaType_ == ptr_;
	}
	
	// Release the current pointer and set a new one
	void Reset(AM_MEDIA_TYPE* pMediaType = nullptr)
	{
		Free();
		pMediaType_ = pMediaType;
	}

	// Release the current pointer
	void Free()
	{
		if (pMediaType_)
		{
			DeleteMediaType(pMediaType_);
			pMediaType_ = nullptr;
		}
	}

private:
	AM_MEDIA_TYPE* pMediaType_;
};

class DShowMediaType
{
public:
	// Constructor
	DShowMediaType() 
	{
		memset(&mediaType_, 0, sizeof(mediaType_));
	}

	DShowMediaType(const DShowMediaType &mt)
	{
		CopyMediaType(&mediaType_, &mt.mediaType_);
	}

	DShowMediaType(const AM_MEDIA_TYPE &type_)
	{
		CopyMediaType(&mediaType_, &type_);
	}

	// Destructor
	~DShowMediaType() 
	{
		Free();
	}

	// Get the raw AM_MEDIA_TYPE
	AM_MEDIA_TYPE& Get() 
	{
		return mediaType_;
	}

	// Free the current media type
	void Free()
	{
		FreeMediaType(mediaType_);
	}

	operator AM_MEDIA_TYPE*() { return &mediaType_; }
	operator AM_MEDIA_TYPE&() { return mediaType_; }
	operator const AM_MEDIA_TYPE*() const { return &mediaType_; }
	operator const AM_MEDIA_TYPE&() const { return mediaType_; }
	AM_MEDIA_TYPE* Ptr() { return &mediaType_; }
    
	AM_MEDIA_TYPE* operator->() { return &mediaType_; }
	const AM_MEDIA_TYPE* operator->() const { return &mediaType_; }

private:
	AM_MEDIA_TYPE mediaType_;
};