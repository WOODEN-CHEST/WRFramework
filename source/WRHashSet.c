#include "WRHashSet.h"
#include <stdint.h>
#include "WRCollection.h"
#include "WRCompile.h"
#include "WRMemory.h"


// Macros.
#define HASH_SET_INDEX_INVALID (~((size_t)0))
#define HASH_SET_MINIMUM_CAPACITY ((size_t)8)


// Types.
typedef enum HashSetBucketStateEnum
{
    HashSetBucketState_Empty = 0,
    HashSetBucketState_Occupied = 1,
    HashSetBucketState_Tombstone = 2,
} HashSetBucketState;

typedef struct HashSetBucketMetadataStruct
{
    HashCode Hash;
    uint8_t State;
} HashSetBucketMetadata;

typedef struct HashSetEnumeratorStruct
{
    CollectionEnumerator Base;
    HashSet* _hashSet;
    size_t _currentIndex;
} HashSetEnumerator;

typedef struct HashSetFindSlotResultStruct
{
    bool WasFound;
    bool CanInsert;
    size_t FoundIndex;
    size_t InsertionIndex;
} HashSetFindSlotResult;


// Static functions.
static size_t HashSet_SetGetElementCount(void* self);

static Error HashSet_SetAdd(void* self, const void* element, bool* outWasAdded);

static Error HashSet_SetRemove(void* self, const void* element, bool* outWasRemoved);

static Error HashSet_SetClear(void* self);

static Error HashSet_SetContains(void* self, const void* element, bool* outContains);

static Error HashSet_SetDeconstruct(void* self);

static size_t HashSet_GetEnumeratorSize(void* self);

static CollectionEnumerator* HashSet_ElementCollectionInitEnumerator(void* self, void* buffer);

static Error HashSetEnumerator_HasNext(void* self, bool* outHasNext);

static Error HashSetEnumerator_NextByValue(void* self, void* outEntryValue);

static Error HashSetEnumerator_NextByReference(void* self, void** outPointer);

static void HashSetEnumerator_Deconstruct(void* self);

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Hash set argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateInvalidOptionError(const unsigned char* optionName, const unsigned char* message)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Hash set option \"%s\" is invalid: %s.",
        optionName,
        message);
}

static Error CreateCapacityError(size_t requestedCapacity)
{
    return Error_Construct3(ErrorCode_BufferTooLarge,
        u8"Could not allocate hash set storage for %zu buckets.",
        requestedCapacity);
}

static Error CreateEnumerationCompletedError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The collection enumerator has no more elements.");
}

static void InitializeEmptyBuffer(GenericBuffer* buffer)
{
    if (buffer == NULL)
    {
        return;
    }

    GenericBuffer_CreateVariable(buffer, NULL, 0, sizeof(unsigned char), 0, NULL, NULL);
}

static bool HashSet_HasStorage(HashSet* self)
{
    return (self != NULL) && (self->_capacity > 0) && (self->_dataBuffer._data != NULL);
}

static size_t HashSet_GetMetadataByteCountForCapacity(size_t capacity)
{
    return capacity * sizeof(HashSetBucketMetadata);
}

static bool HashSet_TryGetStorageByteCount(HashSet* self, size_t capacity, size_t* outByteCount)
{
    size_t MetadataBytes = 0;
    size_t ElementBytes = 0;

    if ((self == NULL) || (outByteCount == NULL))
    {
        return false;
    }
    if (!Memory_TryMultiplySize(capacity, sizeof(HashSetBucketMetadata), &MetadataBytes))
    {
        return false;
    }
    if (!Memory_TryMultiplySize(capacity, ISet_GetElementSize(HashSet_AsSet(self)), &ElementBytes))
    {
        return false;
    }

    return Memory_TryAddSize(MetadataBytes, ElementBytes, outByteCount);
}

static HashSetBucketMetadata* HashSet_GetBucketsFromBlock(unsigned char* block)
{
    return (HashSetBucketMetadata*)block;
}

static unsigned char* HashSet_GetElementsFromBlock(unsigned char* block, size_t capacity)
{
    return block + HashSet_GetMetadataByteCountForCapacity(capacity);
}

static HashSetBucketMetadata* HashSet_GetBuckets(HashSet* self)
{
    return HashSet_GetBucketsFromBlock(self->_dataBuffer._data);
}

static unsigned char* HashSet_GetElementBlock(HashSet* self)
{
    return HashSet_GetElementsFromBlock(self->_dataBuffer._data, self->_capacity);
}

static unsigned char* HashSet_GetElementPointerFromBlock(HashSet* self, unsigned char* elementBlock, size_t index)
{
    return elementBlock + (index * ISet_GetElementSize(HashSet_AsSet(self)));
}

static unsigned char* HashSet_GetElementPointerAt(HashSet* self, size_t index)
{
    return HashSet_GetElementPointerFromBlock(self, HashSet_GetElementBlock(self), index);
}

static bool HashSet_ElementsAreEqual(HashSet* self, const void* storedElement, const void* requestedElement)
{
    return self->_elementComparator(HashSet_AsSet(self),
        storedElement,
        requestedElement,
        &self->_elementComparatorUserData);
}

static HashCode HashSet_HashElement(HashSet* self, const void* element)
{
    return self->_elementHashFunction(HashSet_AsSet(self), element, &self->_elementHashFunctionUserData);
}

static bool HashSet_IsBucketOccupied(HashSetBucketMetadata bucket)
{
    return bucket.State == HashSetBucketState_Occupied;
}

static HashSetFindSlotResult HashSet_FindSlotInStorage(HashSet* self,
    size_t capacity,
    HashSetBucketMetadata* buckets,
    unsigned char* elementBlock,
    HashCode hash,
    const void* element)
{
    HashSetFindSlotResult Result =
    {
        .WasFound = false,
        .CanInsert = false,
        .FoundIndex = HASH_SET_INDEX_INVALID,
        .InsertionIndex = HASH_SET_INDEX_INVALID,
    };
    size_t FirstTombstoneIndex = HASH_SET_INDEX_INVALID;
    size_t Index = 0;

    if ((self == NULL) || (capacity == 0) || (buckets == NULL) || (elementBlock == NULL))
    {
        return Result;
    }

    Index = (size_t)(hash % (HashCode)capacity);
    for (size_t ProbeIndex = 0; ProbeIndex < capacity; ProbeIndex++)
    {
        HashSetBucketMetadata Bucket = buckets[Index];

        if (Bucket.State == HashSetBucketState_Empty)
        {
            Result.CanInsert = true;
            Result.InsertionIndex = (FirstTombstoneIndex != HASH_SET_INDEX_INVALID) ? FirstTombstoneIndex : Index;
            return Result;
        }
        if (Bucket.State == HashSetBucketState_Tombstone)
        {
            if (FirstTombstoneIndex == HASH_SET_INDEX_INVALID)
            {
                FirstTombstoneIndex = Index;
            }
        }
        else if ((Bucket.Hash == hash)
            && HashSet_ElementsAreEqual(self, HashSet_GetElementPointerFromBlock(self, elementBlock, Index), element))
        {
            Result.WasFound = true;
            Result.CanInsert = true;
            Result.FoundIndex = Index;
            Result.InsertionIndex = Index;
            return Result;
        }

        Index++;
        if (Index == capacity)
        {
            Index = 0;
        }
    }

    if (FirstTombstoneIndex != HASH_SET_INDEX_INVALID)
    {
        Result.CanInsert = true;
        Result.InsertionIndex = FirstTombstoneIndex;
    }

    return Result;
}

static HashSetFindSlotResult HashSet_FindSlot(HashSet* self, HashCode hash, const void* element)
{
    return HashSet_FindSlotInStorage(self,
        self->_capacity,
        HashSet_GetBuckets(self),
        HashSet_GetElementBlock(self),
        hash,
        element);
}

static bool HashSet_TryFindNextOccupiedIndex(HashSet* self, size_t startIndex, size_t* outIndex)
{
    HashSetBucketMetadata* Buckets = NULL;

    if (outIndex == NULL)
    {
        return false;
    }

    *outIndex = HASH_SET_INDEX_INVALID;
    if (!HashSet_HasStorage(self))
    {
        return false;
    }

    Buckets = HashSet_GetBuckets(self);
    for (size_t Index = startIndex; Index < self->_capacity; Index++)
    {
        if (HashSet_IsBucketOccupied(Buckets[Index]))
        {
            *outIndex = Index;
            return true;
        }
    }

    return false;
}

static bool HashSet_ShouldGrowForInsertion(HashSet* self)
{
    size_t OccupiedAndTombstones = 0;
    size_t GrowthThreshold = 0;

    if (self == NULL)
    {
        return false;
    }
    if (self->_capacity == 0)
    {
        return true;
    }

    OccupiedAndTombstones = self->_elementCount + self->_tombstoneCount;
    GrowthThreshold = self->_capacity - (self->_capacity / 4);
    return OccupiedAndTombstones >= GrowthThreshold;
}

static bool HashSet_ShouldRebuildForTombstones(HashSet* self)
{
    if ((self == NULL) || (self->_capacity == 0))
    {
        return false;
    }

    return self->_tombstoneCount > (self->_capacity / 4);
}

static Error HashSet_NormalizeCapacity(size_t requestedCapacity, size_t* outCapacity)
{
    size_t Capacity = HASH_SET_MINIMUM_CAPACITY;

    if (outCapacity == NULL)
    {
        return CreateNullArgumentError(u8"outCapacity");
    }

    if (requestedCapacity <= HASH_SET_MINIMUM_CAPACITY)
    {
        *outCapacity = HASH_SET_MINIMUM_CAPACITY;
        return Error_CreateSuccess();
    }

    while (Capacity < requestedCapacity)
    {
        if (Capacity > (SIZE_MAX / 2))
        {
            return CreateCapacityError(requestedCapacity);
        }

        Capacity *= 2;
    }

    *outCapacity = Capacity;
    return Error_CreateSuccess();
}

static Error HashSet_CalculateGrowthCapacity(HashSet* self, size_t minimumLiveElements, size_t* outCapacity)
{
    size_t Capacity = 0;
    Error Result = Error_CreateSuccess();

    if ((self == NULL) || (outCapacity == NULL))
    {
        return CreateNullArgumentError(u8"outCapacity");
    }

    Result = HashSet_NormalizeCapacity((self->_capacity == 0) ? HASH_SET_MINIMUM_CAPACITY : self->_capacity, &Capacity);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    while (minimumLiveElements >= (Capacity - (Capacity / 4)))
    {
        if (Capacity > (SIZE_MAX / 2))
        {
            return CreateCapacityError(minimumLiveElements);
        }

        Capacity *= 2;
    }

    *outCapacity = Capacity;
    return Error_CreateSuccess();
}

static void HashSet_AdoptStorage(HashSet* self, unsigned char* storage, size_t capacity, size_t byteCount)
{
    GenericBuffer_CreateVariable(&self->_dataBuffer, storage, byteCount, sizeof(unsigned char), byteCount, NULL, NULL);
    self->_capacity = capacity;
    self->_isActiveBufferOwned = (storage != NULL);
}

static Error HashSet_RebuildStorage(HashSet* self, size_t requestedCapacity)
{
    unsigned char* NewStorage = NULL;
    size_t NewCapacity = 0;
    size_t NewByteCount = 0;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = HashSet_NormalizeCapacity(requestedCapacity, &NewCapacity);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HashSet_TryGetStorageByteCount(self, NewCapacity, &NewByteCount))
    {
        return CreateCapacityError(NewCapacity);
    }

    NewStorage = Memory_Allocate(NewByteCount);
    Memory_Zero(NewStorage, NewByteCount);

    if (HashSet_HasStorage(self))
    {
        unsigned char* OldStorage = self->_dataBuffer._data;
        HashSetBucketMetadata* OldBuckets = HashSet_GetBuckets(self);
        unsigned char* OldElements = HashSet_GetElementBlock(self);
        size_t OldCapacity = self->_capacity;
        HashSetBucketMetadata* NewBuckets = HashSet_GetBucketsFromBlock(NewStorage);
        unsigned char* NewElements = HashSet_GetElementsFromBlock(NewStorage, NewCapacity);

        for (size_t Index = 0; Index < OldCapacity; Index++)
        {
            HashSetBucketMetadata OldBucket = OldBuckets[Index];

            if (!HashSet_IsBucketOccupied(OldBucket))
            {
                continue;
            }

            HashSetFindSlotResult SlotResult = HashSet_FindSlotInStorage(self,
                NewCapacity,
                NewBuckets,
                NewElements,
                OldBucket.Hash,
                HashSet_GetElementPointerFromBlock(self, OldElements, Index));

            if (!SlotResult.CanInsert)
            {
                Memory_Free(NewStorage);
                return CreateCapacityError(NewCapacity);
            }

            NewBuckets[SlotResult.InsertionIndex].Hash = OldBucket.Hash;
            NewBuckets[SlotResult.InsertionIndex].State = HashSetBucketState_Occupied;
            Memory_Copy(HashSet_GetElementPointerFromBlock(self, OldElements, Index),
                HashSet_GetElementPointerFromBlock(self, NewElements, SlotResult.InsertionIndex),
                ISet_GetElementSize(HashSet_AsSet(self)));
        }

        Memory_Free(OldStorage);
    }

    HashSet_AdoptStorage(self, NewStorage, NewCapacity, NewByteCount);
    self->_tombstoneCount = 0;
    return Error_CreateSuccess();
}

static Error HashSet_EnsureInsertCapacity(HashSet* self)
{
    size_t RequiredLiveCount = 0;
    size_t GrowthCapacity = 0;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    if (HashSet_ShouldGrowForInsertion(self))
    {
        RequiredLiveCount = self->_elementCount + 1;
        Result = HashSet_CalculateGrowthCapacity(self, RequiredLiveCount, &GrowthCapacity);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        return HashSet_RebuildStorage(self, GrowthCapacity);
    }
    if (HashSet_ShouldRebuildForTombstones(self))
    {
        return HashSet_RebuildStorage(self, self->_capacity);
    }

    return Error_CreateSuccess();
}

static void InitializeInterfaces(HashSet* self)
{
    static const SetVTable SetVTableTemplate =
    {
        .Self = NULL,
        ._add = HashSet_SetAdd,
        ._remove = HashSet_SetRemove,
        ._clear = HashSet_SetClear,
        ._contains = HashSet_SetContains,
        ._getElementCount = HashSet_SetGetElementCount,
        ._deconstruct = HashSet_SetDeconstruct,
    };
    static const ICollectionVtable ElementCollectionTemplate =
    {
        .Self = NULL,
        ._getEnumeratorSize = HashSet_GetEnumeratorSize,
        ._initEnumerator = HashSet_ElementCollectionInitEnumerator,
    };

    if (self == NULL)
    {
        return;
    }

    self->_set._flags = SetFlags_None;
    self->_set._vtable = SetVTableTemplate;
    self->_set._vtable.Self = self;
    self->_set._elementCollection._vtable = ElementCollectionTemplate;
    self->_set._elementCollection._vtable.Self = self;
}

static void InitializeEmptyHashSet(HashSet* self)
{
    if (self == NULL)
    {
        return;
    }

    self->_set._elementSize = 0;
    self->_elementHashFunction = NULL;
    self->_elementHashFunctionUserData = UserData_CreateEmpty();
    self->_elementComparator = SetElementComparator_Default;
    self->_elementComparatorUserData = UserData_CreateEmpty();
    InitializeEmptyBuffer(&self->_dataBuffer);
    self->_isActiveBufferOwned = false;
    self->_elementCount = 0;
    self->_tombstoneCount = 0;
    self->_capacity = 0;
    InitializeInterfaces(self);
}

static Error ValidateHashSet(HashSet* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Error_CreateSuccess();
}

static Error ValidateHashSetOutput(HashSet* self, void* output, const unsigned char* outputName)
{
    Error Result = ValidateHashSet(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (output == NULL)
    {
        return CreateNullArgumentError(outputName);
    }

    return Error_CreateSuccess();
}

static size_t HashSet_SetGetElementCount(void* self)
{
    HashSet* HashSetSelf = self;

    if (HashSetSelf == NULL)
    {
        return 0;
    }

    return HashSetSelf->_elementCount;
}

static Error HashSet_SetAdd(void* self, const void* element, bool* outWasAdded)
{
    HashSet* HashSetSelf = self;
    HashCode Hash = 0;
    HashSetFindSlotResult SlotResult;
    Error Result = ValidateHashSetOutput(HashSetSelf, outWasAdded, u8"outWasAdded");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (element == NULL)
    {
        return CreateNullArgumentError(u8"element");
    }

    Result = HashSet_EnsureInsertCapacity(HashSetSelf);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Hash = HashSet_HashElement(HashSetSelf, element);
    SlotResult = HashSet_FindSlot(HashSetSelf, Hash, element);
    if (!SlotResult.CanInsert)
    {
        return CreateCapacityError(HashSetSelf->_capacity);
    }

    if (SlotResult.WasFound)
    {
        // An equal element is already stored; the stored copy is left untouched.
        *outWasAdded = false;
        return Error_CreateSuccess();
    }

    if (HashSet_GetBuckets(HashSetSelf)[SlotResult.InsertionIndex].State == HashSetBucketState_Tombstone)
    {
        HashSetSelf->_tombstoneCount--;
    }

    HashSet_GetBuckets(HashSetSelf)[SlotResult.InsertionIndex].Hash = Hash;
    HashSet_GetBuckets(HashSetSelf)[SlotResult.InsertionIndex].State = HashSetBucketState_Occupied;
    Memory_Copy(element,
        HashSet_GetElementPointerAt(HashSetSelf, SlotResult.InsertionIndex),
        ISet_GetElementSize(HashSet_AsSet(HashSetSelf)));
    HashSetSelf->_elementCount++;
    *outWasAdded = true;
    return Error_CreateSuccess();
}

static Error HashSet_SetRemove(void* self, const void* element, bool* outWasRemoved)
{
    HashSet* HashSetSelf = self;
    HashCode Hash = 0;
    HashSetFindSlotResult SlotResult;
    Error Result = ValidateHashSetOutput(HashSetSelf, outWasRemoved, u8"outWasRemoved");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (element == NULL)
    {
        return CreateNullArgumentError(u8"element");
    }

    *outWasRemoved = false;
    if (!HashSet_HasStorage(HashSetSelf))
    {
        return Error_CreateSuccess();
    }

    Hash = HashSet_HashElement(HashSetSelf, element);
    SlotResult = HashSet_FindSlot(HashSetSelf, Hash, element);
    if (!SlotResult.WasFound)
    {
        return Error_CreateSuccess();
    }

    HashSet_GetBuckets(HashSetSelf)[SlotResult.FoundIndex].State = HashSetBucketState_Tombstone;
    HashSetSelf->_elementCount--;
    HashSetSelf->_tombstoneCount++;
    *outWasRemoved = true;

    if (HashSet_ShouldRebuildForTombstones(HashSetSelf) && (HashSetSelf->_elementCount > 0))
    {
        Result = HashSet_RebuildStorage(HashSetSelf, HashSetSelf->_capacity);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error HashSet_SetClear(void* self)
{
    HashSet* HashSetSelf = self;
    Error Result = ValidateHashSet(HashSetSelf);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HashSet_HasStorage(HashSetSelf))
    {
        return Error_CreateSuccess();
    }

    Memory_Zero(HashSet_GetBuckets(HashSetSelf),
        HashSet_GetMetadataByteCountForCapacity(HashSetSelf->_capacity));
    HashSetSelf->_elementCount = 0;
    HashSetSelf->_tombstoneCount = 0;
    return Error_CreateSuccess();
}

static Error HashSet_SetContains(void* self, const void* element, bool* outContains)
{
    HashSet* HashSetSelf = self;
    HashCode Hash = 0;
    HashSetFindSlotResult SlotResult;
    Error Result = ValidateHashSetOutput(HashSetSelf, outContains, u8"outContains");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (element == NULL)
    {
        return CreateNullArgumentError(u8"element");
    }

    *outContains = false;
    if (!HashSet_HasStorage(HashSetSelf))
    {
        return Error_CreateSuccess();
    }

    Hash = HashSet_HashElement(HashSetSelf, element);
    SlotResult = HashSet_FindSlot(HashSetSelf, Hash, element);
    *outContains = SlotResult.WasFound;
    return Error_CreateSuccess();
}

static Error HashSet_SetDeconstruct(void* self)
{
    return HashSet_Deconstruct(self);
}

static size_t HashSet_GetEnumeratorSize(void* self)
{
    UNUSED(self);
    return sizeof(HashSetEnumerator);
}

static CollectionEnumerator* HashSet_ElementCollectionInitEnumerator(void* self, void* buffer)
{
    static const CollectionEnumeratorVTable EnumeratorTemplate =
    {
        .Self = NULL,
        ._hasNext = HashSetEnumerator_HasNext,
        ._nextByValue = HashSetEnumerator_NextByValue,
        ._nextByReference = HashSetEnumerator_NextByReference,
        ._deconstruct = HashSetEnumerator_Deconstruct,
    };
    HashSet* HashSetSelf = self;
    HashSetEnumerator* Enumerator = buffer;

    if ((HashSetSelf == NULL) || (Enumerator == NULL))
    {
        return NULL;
    }

    Enumerator->Base._singleElementSize = ISet_GetElementSize(HashSet_AsSet(HashSetSelf));
    Enumerator->Base._flags = EnumeratorFlags_CanReturnByReference;
    Enumerator->Base._vtable = EnumeratorTemplate;
    Enumerator->Base._vtable.Self = Enumerator;
    Enumerator->_hashSet = HashSetSelf;
    Enumerator->_currentIndex = 0;
    return &Enumerator->Base;
}

static Error HashSetEnumerator_HasNext(void* self, bool* outHasNext)
{
    HashSetEnumerator* EnumeratorSelf = self;
    size_t NextIndex = HASH_SET_INDEX_INVALID;

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outHasNext == NULL)
    {
        return CreateNullArgumentError(u8"outHasNext");
    }

    *outHasNext = HashSet_TryFindNextOccupiedIndex(EnumeratorSelf->_hashSet, EnumeratorSelf->_currentIndex, &NextIndex);
    return Error_CreateSuccess();
}

static Error HashSetEnumerator_NextByValue(void* self, void* outEntryValue)
{
    HashSetEnumerator* EnumeratorSelf = self;
    size_t NextIndex = HASH_SET_INDEX_INVALID;

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outEntryValue == NULL)
    {
        return CreateNullArgumentError(u8"outEntryValue");
    }
    if (!HashSet_TryFindNextOccupiedIndex(EnumeratorSelf->_hashSet, EnumeratorSelf->_currentIndex, &NextIndex))
    {
        return CreateEnumerationCompletedError();
    }

    Memory_Copy(HashSet_GetElementPointerAt(EnumeratorSelf->_hashSet, NextIndex),
        outEntryValue,
        ISet_GetElementSize(HashSet_AsSet(EnumeratorSelf->_hashSet)));
    EnumeratorSelf->_currentIndex = NextIndex + 1;
    return Error_CreateSuccess();
}

static Error HashSetEnumerator_NextByReference(void* self, void** outPointer)
{
    HashSetEnumerator* EnumeratorSelf = self;
    size_t NextIndex = HASH_SET_INDEX_INVALID;

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outPointer == NULL)
    {
        return CreateNullArgumentError(u8"outPointer");
    }
    if (!HashSet_TryFindNextOccupiedIndex(EnumeratorSelf->_hashSet, EnumeratorSelf->_currentIndex, &NextIndex))
    {
        *outPointer = NULL;
        return CreateEnumerationCompletedError();
    }

    *outPointer = HashSet_GetElementPointerAt(EnumeratorSelf->_hashSet, NextIndex);
    EnumeratorSelf->_currentIndex = NextIndex + 1;
    return Error_CreateSuccess();
}

static void HashSetEnumerator_Deconstruct(void* self)
{
    // The enumerator buffer is caller-owned; there are no internal resources to release.
    UNUSED(self);
}


// Public functions.
Error HashSet_Construct1(HashSet* self, HashSetConstructOptions options)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (options.ElementSize == 0)
    {
        return CreateInvalidOptionError(u8"ElementSize", u8"must be greater than zero");
    }
    if (options.ElementHashFunction == NULL)
    {
        return CreateInvalidOptionError(u8"ElementHashFunction", u8"must not be null");
    }

    InitializeEmptyHashSet(self);
    self->_set._elementSize = options.ElementSize;
    self->_elementHashFunction = options.ElementHashFunction;
    self->_elementHashFunctionUserData = options.ElementHashFunctionUserData;
    self->_elementComparator = (options.ElementComparator == NULL) ? SetElementComparator_Default : options.ElementComparator;
    self->_elementComparatorUserData = options.ElementComparatorUserData;
    InitializeInterfaces(self);

    if (options.InitialCapacity > 0)
    {
        Result = HashSet_RebuildStorage(self, options.InitialCapacity);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

Error HashSet_Deconstruct(HashSet* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    if (self->_isActiveBufferOwned && (self->_dataBuffer._data != NULL))
    {
        Memory_Free(self->_dataBuffer._data);
    }

    InitializeEmptyHashSet(self);
    return Error_CreateSuccess();
}
