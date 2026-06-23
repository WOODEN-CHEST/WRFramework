#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WRError.h"
#include "WRMemory.h"


// Types.
typedef enum EnumeratorFlagsEnum
{
    EnumeratorFlags_None = 0,
    EnumeratorFlags_CanReturnByReference = (1 << 0)
} EnumeratorFlags;

typedef struct CollectionEnumeratorVTableStruct
{
    void* Self;
    Error (*_hasNext)(void* self, bool* outHasNext);
    Error (*_nextByValue)(void* self, void* outEntryValue);
    Error (*_nextByReference)(void* self, void** outPointer);
    void (*_deconstruct)(void* self);
} CollectionEnumeratorVTable;

typedef struct CollectionEnumeratorStruct
{
    size_t _singleElementSize;
    EnumeratorFlags _flags;
    CollectionEnumeratorVTable _vtable;
} CollectionEnumerator;

typedef struct ICollectionVTable
{
    void* Self;
    size_t (*_getEnumeratorSize)(void* self);
    CollectionEnumerator* (*_initEnumerator)(void* self, void* buffer);
} ICollectionVtable;

typedef struct ICollectionStruct
{
    ICollectionVtable _vtable;
} ICollection;


// Functions.

/* Enumerators are caller-owned. The preferred form queries the required byte size and constructs the
 * enumerator into a caller-provided buffer (which may be reused or stack-allocated to avoid per-
 * iteration allocation). The buffer must be at least ICollection_GetEnumeratorSize(self) bytes and
 * must outlive the enumerator. CollectionEnumerator_Deconstruct releases any internal resources but
 * does NOT free the buffer. */
static inline size_t ICollection_GetEnumeratorSize(ICollection* self)
{
    return (*self->_vtable._getEnumeratorSize)(self->_vtable.Self);
}

static inline CollectionEnumerator* ICollection_InitEnumerator(ICollection* self, void* buffer)
{
    return (*self->_vtable._initEnumerator)(self->_vtable.Self, buffer);
}

/* Convenience that allocates a correctly-sized enumerator buffer and constructs into it. The caller
 * must Memory_Free the returned pointer after CollectionEnumerator_Deconstruct. Prefer the caller-
 * owned form above on hot paths. */
static inline CollectionEnumerator* ICollection_CreateEnumerator(ICollection* self)
{
    return ICollection_InitEnumerator(self, Memory_Allocate(ICollection_GetEnumeratorSize(self)));
}

Error ICollection_WriteToBufferByValue(ICollection* self, GenericBuffer* buffer);

Error ICollection_WriteToBufferByReference(ICollection* self, GenericBuffer* buffer);


static inline Error CollectionEnumerator_HasNext(CollectionEnumerator* self, bool* outValue)
{
    return (*self->_vtable._hasNext)(self->_vtable.Self, outValue);
}

static inline size_t CollectionEnumerator_GetSingleElementSize(CollectionEnumerator* self)
{
    return self->_singleElementSize;
}


static inline Error CollectionEnumerator_NextByValue(CollectionEnumerator* self, void* outValue)
{
    return (*self->_vtable._nextByValue)(self->_vtable.Self, outValue);
}

Error CollectionEnumerator_NextByReference(CollectionEnumerator* self, void** outPointer);

static inline bool CollectionEnumerator_IsReferenceReturningSupported(CollectionEnumerator* self)
{
    return ((self ->_flags) & EnumeratorFlags_CanReturnByReference) != 0;
}

static inline void CollectionEnumerator_Deconstruct(CollectionEnumerator* self)
{
    (*self->_vtable._deconstruct)(self->_vtable.Self);
}

/* Deconstructs the enumerator and frees its buffer. Use this only for enumerators obtained from
 * ICollection_CreateEnumerator (which allocated the buffer); for a caller-owned buffer use
 * CollectionEnumerator_Deconstruct and free/reuse the buffer yourself. */
static inline void CollectionEnumerator_Destroy(CollectionEnumerator* self)
{
    CollectionEnumerator_Deconstruct(self);
    Memory_Free(self);
}