#pragma once
#include "WRError.h"
#include "WRMemory.h"
#include "WRHashMap.h"

typedef struct WRBufferPoolStruct
{
    HashMap _entries;
} WRBufferPool;


// Functions.
Error BufferPool_Construct1(WRBufferPool* self);

Error BufferPool_Deconstruct(WRBufferPool* self);

Error BufferPool_Borrow(WRBufferPool* self, size_t elementSize, GenericBuffer** outBuffer);

Error BufferPool_Return(WRBufferPool* self, GenericBuffer* bufferToReturn);