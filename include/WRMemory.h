#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRComparator.h"


#define GENERIC_BUFFER_INDEX_INVALID (~((size_t)0))


// Types.
typedef struct GenericBufferStruct GenericBuffer;

typedef bool (*GenericBufferAllocateCallback)(GenericBuffer* destination, size_t requestedCapacity);

typedef enum GenericBufferFlagsEnum
{
    GenericBufferFlags_None = 0,
    GenericBufferFlags_FixedCapacity = (1 << 0),
    GenericBufferFlags_ReadOnly = (1 << 1),
} GenericBufferFlags;

struct GenericBufferStruct
{
    unsigned char* _data;
    size_t _capacity;
    size_t _count;
    size_t _elementSize;
    void* _userData;
    GenericBufferAllocateCallback _requestMoreSpaceCallback;
    GenericBufferFlags _flags;
};

typedef struct GenericBufferElementDataStruct
{
    void* _element;
    size_t _index;
} GenericBufferElementData;

typedef bool (*GenericBufferPredicate)(GenericBuffer* buffer, GenericBufferElementData element, void* userData);

typedef ComparisonResult (*GenericBufferComparator)(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, void* userData);

typedef void (*GenericBufferMapper)(GenericBuffer* buffer, GenericBufferElementData sourceElement, void* destinationElement, void* userData);

typedef int64_t (*GenericBufferIntExtractor)(GenericBuffer* buffer, GenericBufferElementData sourceElement, void* userData);

typedef double (*GenericBufferDoubleExtractor)(GenericBuffer* buffer, GenericBufferElementData sourceElement, void* userData);


// Functions.
void GenericBuffer_CreateVariable(GenericBuffer* buffer, 
    void* destination,
    size_t bufferCapacity,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback);

void GenericBuffer_AllocateVariable(GenericBuffer* buffer, 
     size_t initialCapacity,
     size_t elementSize);

/* Creates a fixed-capacity buffer over existing storage. The buffer is still writable in place
 * (its element count can change up to the capacity); it simply cannot grow. For a buffer that
 * also rejects all mutation, use GenericBuffer_CreateReadOnly. */
void GenericBuffer_CreateConstant(GenericBuffer* buffer, void* destination, size_t bufferCapacity, size_t elementSize, size_t elementCount);

/* Creates a read-only, fixed-capacity view over existing storage. Every mutating operation is
 * rejected; reading and pointer access remain valid. */
void GenericBuffer_CreateReadOnly(GenericBuffer* buffer, void* destination, size_t bufferCapacity, size_t elementSize, size_t elementCount);

void GenericBuffer_SetCallback(GenericBuffer* buffer, GenericBufferAllocateCallback callback, void* userData);

void GenericBuffer_ClearCallback(GenericBuffer* buffer);

bool GenericBuffer_EnsureTotalCapacity(GenericBuffer* buffer, size_t requiredSize);

bool GenericBuffer_ReserveMoreCapacity(GenericBuffer* buffer, size_t requiredSize);

size_t GenericBuffer_GetCapacityRemaining(GenericBuffer* buffer);

static inline bool GenericBuffer_IsReadOnly(GenericBuffer* buffer)
{
    return buffer->_flags & GenericBufferFlags_ReadOnly;
}

static inline bool GenericBuffer_IsFixedCapacity(GenericBuffer* buffer)
{
    return buffer->_flags & GenericBufferFlags_FixedCapacity;
}



bool GenericBuffer_AddLast(GenericBuffer* buffer, void* item);

bool GenericBuffer_AddFirst(GenericBuffer* buffer, void* item);

bool GenericBuffer_Insert(GenericBuffer* buffer, void* item, size_t index);

bool GenericBuffer_Replace(GenericBuffer* buffer, void* item, size_t index);

bool GenericBuffer_RemoveAt(GenericBuffer* buffer, size_t index);

bool GenericBuffer_RemoveFirst(GenericBuffer* buffer);

bool GenericBuffer_RemoveLast(GenericBuffer* buffer);

bool GenericBuffer_AddLastRange(GenericBuffer* buffer, void* items, size_t itemCount);

bool GenericBuffer_AddFirstRange(GenericBuffer* buffer, void* items, size_t itemCount);

bool GenericBuffer_InsertRange(GenericBuffer* buffer, void* items, size_t itemCount, size_t index);

bool GenericBuffer_ReplaceRange(GenericBuffer* buffer, void* items, size_t itemCount, size_t index);

bool GenericBuffer_RemoveRange(GenericBuffer* buffer, size_t startIndexInclusive, size_t endIndexExclusive);


void* GenericBuffer_GetPointerToElement(GenericBuffer* buffer, size_t index);

void* GenericBuffer_GetPointerToFirst(GenericBuffer* buffer);

void* GenericBuffer_GetPointerToLast(GenericBuffer* buffer);

bool GenericBuffer_GetAt(GenericBuffer* buffer, size_t index, void* out);

bool GenericBuffer_GetFirst(GenericBuffer* buffer, void* out);

bool GenericBuffer_GetLast(GenericBuffer* buffer, void* out);

bool GenericBuffer_Clear(GenericBuffer* buffer);


bool GenericBuffer_Contains(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);

size_t GenericBuffer_FirstIndexOf(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);

size_t GenericBuffer_LastIndexOf(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);

/* Bytes of scratch storage required by the sort and reverse operations that take a scratch buffer.
 * Allocate at least this many bytes (sized for the largest case) and reuse it across calls. */
static inline size_t GenericBuffer_GetSortScratchSize(GenericBuffer* buffer)
{
    return buffer->_elementSize * 2;
}

/* Reverses the buffer in place. The plain form takes a caller-owned scratch buffer of at least
 * GenericBuffer_GetSortScratchSize(buffer) bytes (reuse it to avoid per-call allocation); passing
 * NULL allocates internally. The ...Allocating form always allocates and frees the scratch. */
bool GenericBuffer_Reverse(GenericBuffer* buffer, void* scratch);

bool GenericBuffer_ReverseAllocating(GenericBuffer* buffer);


/* Sort operations: prefer the plain form with a reusable caller-owned scratch buffer of at least
 * GenericBuffer_GetSortScratchSize(buffer) bytes (NULL allocates internally). The ...Allocating
 * forms always allocate and free the scratch for you. */
bool GenericBuffer_SortAscending(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData, void* scratch);

bool GenericBuffer_SortAscendingAllocating(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData);

bool GenericBuffer_SortDescending(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData, void* scratch);

bool GenericBuffer_SortDescendingAllocating(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData);

bool GenericBuffer_Filter(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);

bool GenericBuffer_Map(GenericBuffer* buffer, GenericBuffer* destination, GenericBufferMapper mapper, void* userData);

bool GenericBuffer_SumInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue);

bool GenericBuffer_SumDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue);

bool GenericBuffer_MaxInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue);

bool GenericBuffer_MaxDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue);

bool GenericBuffer_MinInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue);

bool GenericBuffer_MinDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue);

size_t GenericBuffer_CountWhere(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);


bool GenericBuffer_AppendByte(GenericBuffer* buffer, unsigned char byte);

bool GenericBuffer_AppendRangeBytes(GenericBuffer* buffer, unsigned char* bytes, size_t size);

bool GenericBuffer_AppendString(GenericBuffer* buffer, const unsigned char* str);

bool GenericBuffer_NullTerminate(GenericBuffer* buffer);

bool GenericBuffer_SetByte(GenericBuffer* buffer, size_t index, unsigned char byte);

bool GenericBuffer_TryPrepareForManualMutation(GenericBuffer* buffer, size_t addedElementCount);

/* Sets the element count directly, after the caller has written into reserved capacity.
 * Fails if the buffer is read-only or if newCount exceeds the current capacity (reserve first).
 * This is the supported way to update the count after a manual write; never assign _count directly. */
bool GenericBuffer_SetCount(GenericBuffer* buffer, size_t newCount);

/* Adds addedCount to the current element count (the common "I just appended N elements" case).
 * Fails if the buffer is read-only or if the resulting count would exceed the current capacity. */
bool GenericBuffer_CommitCount(GenericBuffer* buffer, size_t addedCount);

/* Reserves room for requestedCount more elements and returns a writable pointer to the tail
 * (the address one past the last current element). Pair with GenericBuffer_CommitCount/SetCount
 * after writing. Returns false (and sets *outTail to NULL) if the buffer cannot be grown or is read-only. */
bool GenericBuffer_GetWritableTail(GenericBuffer* buffer, size_t requestedCount, void** outTail);



void* Memory_Allocate(size_t size);

void* Memory_Reallocate(void* ptr, size_t size);

void Memory_Free(void* ptr);

void Memory_Set(void* ptr, int8_t value, size_t size);

void Memory_Zero(void* ptr, size_t size);

bool Memory_IsEqual(const void* regionA, const void* regionB, size_t size);

void Memory_Copy(const void* source, void* destination, size_t size);

void Memory_Move(void* source, void* destination, size_t size);

/* Multiplies two sizes, writing the product to *outResult. Returns false on overflow (and leaves
 * *outResult untouched). Use this before any allocation whose size is a product of two values. */
bool Memory_TryMultiplySize(size_t a, size_t b, size_t* outResult);

/* Adds two sizes, writing the sum to *outResult. Returns false on overflow. */
bool Memory_TryAddSize(size_t a, size_t b, size_t* outResult);

/* Computes a geometric growth capacity: starting from startCapacity (floored to 1), repeatedly
 * multiplies by growthMultiplier until it reaches requiredCapacity, never letting the resulting
 * byte size (capacity * elementSize) overflow. If a further multiply would overflow it clamps to
 * exactly requiredCapacity. Returns false if requiredCapacity itself cannot fit, or on bad input
 * (elementSize 0, multiplier < 2). Shared by every growable allocation callback. */
bool Memory_TryGrowCapacity(size_t startCapacity,
    size_t requiredCapacity,
    size_t growthMultiplier,
    size_t elementSize,
    size_t* outCapacity);

size_t Memory_GetTotalAllocationCount(void);

size_t Memory_GetTotalReallocationCount(void);

size_t Memory_GetTotalFreeCount(void);

size_t Memory_GetCurrentAllocationCount(void);
