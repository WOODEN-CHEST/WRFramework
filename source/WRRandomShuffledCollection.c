#include "WRRandomShuffledCollection.h"
#include "WRList.h"
#include "WRShuffle.h"


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Shuffled collection argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateCapacityError(size_t requiredCapacity)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Could not ensure shuffled collection capacity of %zu elements.",
        requiredCapacity);
}

/**
 * Rebuilds the playback order as a freshly shuffled permutation of all item indices and rewinds
 * the cursor. Must only be called with at least one item present.
 */
static Error RandomShuffledCollection_StartNewSequence(RandomShuffledCollection* self, size_t elementCount)
{
    Error Result = Error_CreateSuccess();
    void* WritableTail = NULL;
    size_t* Order = NULL;
    size_t Index = 0;

    (void)GenericBuffer_Clear(&self->_playbackOrder);
    if (!GenericBuffer_GetWritableTail(&self->_playbackOrder, elementCount, &WritableTail))
    {
        return CreateCapacityError(elementCount);
    }
    Order = WritableTail;
    for (Index = 0; Index < elementCount; Index++)
    {
        Order[Index] = Index;
    }
    if (!GenericBuffer_CommitCount(&self->_playbackOrder, elementCount))
    {
        return CreateCapacityError(elementCount);
    }

    Result = Shuffle_Buffer(&self->_playbackOrder, &self->_random);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_cursor = 0;
    self->_isSequenceValid = true;
    return Error_CreateSuccess();
}


// Public functions.
void RandomShuffledCollection_Construct1(RandomShuffledCollection* self, size_t elementSize)
{
    if (self == NULL)
    {
        return;
    }

    ArrayList_Construct1(&self->_items, elementSize);
    GenericBuffer_AllocateVariable(&self->_playbackOrder, 0, sizeof(size_t));
    self->_cursor = 0;
    Random_Construct1(&self->_random);
    self->_isSequenceValid = false;
}

void RandomShuffledCollection_Construct2(RandomShuffledCollection* self, size_t elementSize, Random rng)
{
    if (self == NULL)
    {
        return;
    }

    RandomShuffledCollection_Construct1(self, elementSize);
    self->_random = rng;
}

void RandomShuffledCollection_Deconstruct(RandomShuffledCollection* self)
{
    if (self == NULL)
    {
        return;
    }

    ArrayList_Deconstruct(&self->_items);
    if (self->_playbackOrder._data != NULL)
    {
        Memory_Free(self->_playbackOrder._data);
    }
    GenericBuffer_AllocateVariable(&self->_playbackOrder, 0, sizeof(size_t));
    self->_cursor = 0;
    Random_Deconstruct1(&self->_random);
    self->_isSequenceValid = false;
}

void RandomShuffledCollection_SetRandom(RandomShuffledCollection* self, Random rng)
{
    if (self == NULL)
    {
        return;
    }

    self->_random = rng;
}

Error RandomShuffledCollection_EnsureTotalCapacity(RandomShuffledCollection* self, size_t totalCapacity)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = ArrayList_EnsureTotalCapacity(&self->_items, totalCapacity);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_EnsureTotalCapacity(&self->_playbackOrder, totalCapacity))
    {
        return CreateCapacityError(totalCapacity);
    }

    return Error_CreateSuccess();
}

Error RandomShuffledCollection_Add(RandomShuffledCollection* self, void* item)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = IList_AddLast(&self->_items._list, item);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_isSequenceValid = false;
    return Error_CreateSuccess();
}

Error RandomShuffledCollection_AddRange(RandomShuffledCollection* self, void* items, size_t count)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = IList_AddRangeLast(&self->_items._list, items, count);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_isSequenceValid = false;
    return Error_CreateSuccess();
}

Error RandomShuffledCollection_RemoveAt(RandomShuffledCollection* self, size_t index)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = IList_RemoveAt(&self->_items._list, index);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_isSequenceValid = false;
    return Error_CreateSuccess();
}

Error RandomShuffledCollection_Clear(RandomShuffledCollection* self)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = IList_Clear(&self->_items._list);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_cursor = 0;
    self->_isSequenceValid = false;
    return Error_CreateSuccess();
}

Error RandomShuffledCollection_SetItems(RandomShuffledCollection* self, void* items, size_t count)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if ((count > 0) && (items == NULL))
    {
        return CreateNullArgumentError(u8"items");
    }

    Result = RandomShuffledCollection_Clear(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return RandomShuffledCollection_AddRange(self, items, count);
}

Error RandomShuffledCollection_GetItem(RandomShuffledCollection* self, size_t index, void* outItem)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return IList_GetElement(&self->_items._list, index, outItem);
}

Error RandomShuffledCollection_GetNext(RandomShuffledCollection* self, void* outItem)
{
    Error Result = Error_CreateSuccess();
    size_t ElementCount = 0;
    size_t ItemIndex = 0;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outItem == NULL)
    {
        return CreateNullArgumentError(u8"outItem");
    }

    ElementCount = IList_GetElementCount(&self->_items._list);
    if (ElementCount == 0)
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Cannot get the next item of an empty shuffled collection.");
    }

    if (!self->_isSequenceValid || (self->_cursor >= ElementCount))
    {
        Result = RandomShuffledCollection_StartNewSequence(self, ElementCount);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    if (!GenericBuffer_GetAt(&self->_playbackOrder, self->_cursor, &ItemIndex))
    {
        return Error_Construct1(ErrorCode_InvalidState,
            u8"The shuffled collection's playback order went out of sync with its items.");
    }
    Result = IList_GetElement(&self->_items._list, ItemIndex, outItem);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_cursor++;
    return Error_CreateSuccess();
}

void RandomShuffledCollection_Reshuffle(RandomShuffledCollection* self)
{
    if (self == NULL)
    {
        return;
    }

    self->_isSequenceValid = false;
}
