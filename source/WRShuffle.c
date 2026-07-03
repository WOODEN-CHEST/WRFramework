#include "WRShuffle.h"
#include <stdint.h>


// Macros.
// Used as an array size, so it must be a macro (an integer constant expression).
#define SHUFFLE_MAX_STACK_SCRATCH_SIZE ((size_t)256)


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Shuffle argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateTooManyElementsError(void)
{
    return Error_Construct1(ErrorCode_ArgumentOutOfRange,
        u8"The container has more elements than the shuffle can index.");
}

static void ShuffleBufferElements(GenericBuffer* buffer, Random* rng, void* swapScratch)
{
    unsigned char* Data = buffer->_data;
    size_t ElementSize = buffer->_elementSize;
    size_t Index = 0;
    size_t SwapIndex = 0;

    for (Index = buffer->_count - 1; Index > 0; Index--)
    {
        SwapIndex = (size_t)Random_NextInt64InLimit(rng, (int64_t)(Index + 1));
        if (SwapIndex == Index)
        {
            continue;
        }

        Memory_Copy(Data + (Index * ElementSize), swapScratch, ElementSize);
        Memory_Copy(Data + (SwapIndex * ElementSize), Data + (Index * ElementSize), ElementSize);
        Memory_Copy(swapScratch, Data + (SwapIndex * ElementSize), ElementSize);
    }
}

static Error ShuffleListElements(IList* list, Random* rng, void* firstScratch, void* secondScratch)
{
    size_t Index = 0;
    size_t SwapIndex = 0;
    Error Result = Error_CreateSuccess();

    for (Index = IList_GetElementCount(list) - 1; Index > 0; Index--)
    {
        SwapIndex = (size_t)Random_NextInt64InLimit(rng, (int64_t)(Index + 1));
        if (SwapIndex == Index)
        {
            continue;
        }

        Result = IList_GetElement(list, Index, firstScratch);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Result = IList_GetElement(list, SwapIndex, secondScratch);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Result = IList_Replace(list, Index, secondScratch);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Result = IList_Replace(list, SwapIndex, firstScratch);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}


// Public functions.
Error Shuffle_List(IList* list, Random* rng)
{
    unsigned char StackScratch[SHUFFLE_MAX_STACK_SCRATCH_SIZE];
    unsigned char* Scratch = StackScratch;
    size_t ElementSize = 0;
    size_t ScratchSize = 0;
    Error Result = Error_CreateSuccess();

    if (list == NULL)
    {
        return CreateNullArgumentError(u8"list");
    }
    if (rng == NULL)
    {
        return CreateNullArgumentError(u8"rng");
    }
    if (IList_GetElementCount(list) < 2)
    {
        return Error_CreateSuccess();
    }
    if (IList_IsReadOnly(list))
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Cannot shuffle a read-only list.");
    }
    if ((uint64_t)IList_GetElementCount(list) > (uint64_t)INT64_MAX)
    {
        return CreateTooManyElementsError();
    }

    ElementSize = IList_GetElementSize(list);
    if (!Memory_TryMultiplySize(ElementSize, 2, &ScratchSize))
    {
        return Error_Construct1(ErrorCode_ArgumentOutOfRange,
            u8"The list's element size is too large to shuffle.");
    }
    if (ScratchSize > SHUFFLE_MAX_STACK_SCRATCH_SIZE)
    {
        Scratch = Memory_Allocate(ScratchSize);
    }

    Result = ShuffleListElements(list, rng, Scratch, Scratch + ElementSize);

    if (Scratch != StackScratch)
    {
        Memory_Free(Scratch);
    }
    return Result;
}

Error Shuffle_Buffer(GenericBuffer* buffer, Random* rng)
{
    unsigned char StackScratch[SHUFFLE_MAX_STACK_SCRATCH_SIZE];
    unsigned char* Scratch = StackScratch;

    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }
    if (rng == NULL)
    {
        return CreateNullArgumentError(u8"rng");
    }
    if (buffer->_count < 2)
    {
        return Error_CreateSuccess();
    }
    if ((uint64_t)buffer->_count > (uint64_t)INT64_MAX)
    {
        return CreateTooManyElementsError();
    }
    if (!GenericBuffer_TryPrepareForManualMutation(buffer, 0))
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Cannot shuffle the buffer because it does not permit mutation.");
    }

    if (buffer->_elementSize > SHUFFLE_MAX_STACK_SCRATCH_SIZE)
    {
        Scratch = Memory_Allocate(buffer->_elementSize);
    }

    ShuffleBufferElements(buffer, rng, Scratch);

    if (Scratch != StackScratch)
    {
        Memory_Free(Scratch);
    }
    return Error_CreateSuccess();
}
