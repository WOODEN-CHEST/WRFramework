#include "WRRandomWeightedCollection.h"
#include <stdint.h>
#include "WRList.h"
#include "WRMath.h"


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Weighted collection argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateInvalidWeightError(double weight)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Weighted collection weights must be finite and >= 0.0, got %f.",
        weight);
}

static Error CreateCapacityError(size_t requiredCapacity)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Could not ensure weighted collection capacity of %zu elements.",
        requiredCapacity);
}

static Error CreateDesyncError(void)
{
    return Error_Construct1(ErrorCode_InvalidState,
        u8"The weighted collection's item and weight storage went out of sync.");
}

static Error ValidateWeight(double weight)
{
    if (Math_IsNaNDouble(weight) || Math_IsInfinityDouble(weight) || (weight < 0.0))
    {
        return CreateInvalidWeightError(weight);
    }

    return Error_CreateSuccess();
}

static Error ValidateWeightRange(double* weights, size_t count)
{
    size_t Index = 0;
    Error Result = Error_CreateSuccess();

    for (Index = 0; Index < count; Index++)
    {
        Result = ValidateWeight(weights[Index]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static void ClampTotalWeight(RandomWeightedCollection* self)
{
    // Incremental subtraction may leave a tiny negative rounding residue; the true sum is never negative.
    if (self->_totalWeight < 0.0)
    {
        self->_totalWeight = 0.0;
    }
}

/**
 * Rebuilds the alias tables (Vose's alias method) from the current weights. On success the pick
 * tables and _totalWeight exactly match the weights and _isAliasTableValid is set. Must only be
 * called with at least one item present.
 */
static Error RandomWeightedCollection_RebuildAliasTable(RandomWeightedCollection* self)
{
    size_t ElementCount = IList_GetElementCount(&self->_items._list);
    double* Weights = GenericBuffer_GetPointerToFirst(&self->_weights);
    double* Probabilities = NULL;
    size_t* Aliases = NULL;
    size_t* Worklist = NULL;
    void* WritableTail = NULL;
    double TotalWeight = 0.0;
    double ScaledWeight = 0.0;
    size_t Index = 0;
    size_t SmallCount = 0;
    size_t LargeStart = ElementCount;
    size_t SmallIndex = 0;
    size_t LargeIndex = 0;

    (void)GenericBuffer_Clear(&self->_aliasProbabilities);
    (void)GenericBuffer_Clear(&self->_aliasIndices);
    if (!GenericBuffer_GetWritableTail(&self->_aliasProbabilities, ElementCount, &WritableTail))
    {
        return CreateCapacityError(ElementCount);
    }
    Probabilities = WritableTail;
    if (!GenericBuffer_GetWritableTail(&self->_aliasIndices, ElementCount, &WritableTail))
    {
        return CreateCapacityError(ElementCount);
    }
    Aliases = WritableTail;
    // The worklist is pure scratch: its space is reserved and written, but its count is never committed.
    if (!GenericBuffer_GetWritableTail(&self->_aliasWorklist, ElementCount, &WritableTail))
    {
        return CreateCapacityError(ElementCount);
    }
    Worklist = WritableTail;

    for (Index = 0; Index < ElementCount; Index++)
    {
        TotalWeight += Weights[Index];
    }
    self->_totalWeight = TotalWeight;
    if (TotalWeight <= 0.0)
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Cannot pick from the weighted collection because its total weight is zero.");
    }
    if (Math_IsInfinityDouble(TotalWeight))
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Cannot pick from the weighted collection because its total weight overflowed to infinity.");
    }

    // Partition into "small" (scaled weight < 1) and "large" columns; smalls grow from the
    // worklist's bottom, larges from its top, so one array serves as both stacks.
    for (Index = 0; Index < ElementCount; Index++)
    {
        ScaledWeight = (Weights[Index] / TotalWeight) * (double)ElementCount;
        Probabilities[Index] = ScaledWeight;
        if (ScaledWeight < 1.0)
        {
            Worklist[SmallCount] = Index;
            SmallCount++;
        }
        else
        {
            LargeStart--;
            Worklist[LargeStart] = Index;
        }
    }

    // Each iteration finalizes one small column by topping it up from a large one.
    while ((SmallCount > 0) && (LargeStart < ElementCount))
    {
        SmallCount--;
        SmallIndex = Worklist[SmallCount];
        LargeIndex = Worklist[LargeStart];
        Aliases[SmallIndex] = LargeIndex;
        Probabilities[LargeIndex] = (Probabilities[LargeIndex] + Probabilities[SmallIndex]) - 1.0;
        if (Probabilities[LargeIndex] < 1.0)
        {
            LargeStart++;
            Worklist[SmallCount] = LargeIndex;
            SmallCount++;
        }
    }

    // Leftovers on either stack are full columns up to floating-point rounding residue.
    while (LargeStart < ElementCount)
    {
        Probabilities[Worklist[LargeStart]] = 1.0;
        Aliases[Worklist[LargeStart]] = Worklist[LargeStart];
        LargeStart++;
    }
    while (SmallCount > 0)
    {
        SmallCount--;
        Probabilities[Worklist[SmallCount]] = 1.0;
        Aliases[Worklist[SmallCount]] = Worklist[SmallCount];
    }

    if (!GenericBuffer_CommitCount(&self->_aliasProbabilities, ElementCount)
        || !GenericBuffer_CommitCount(&self->_aliasIndices, ElementCount))
    {
        return CreateCapacityError(ElementCount);
    }

    self->_isAliasTableValid = true;
    return Error_CreateSuccess();
}

static void FreeAndResetBuffer(GenericBuffer* buffer, size_t elementSize)
{
    if (buffer->_data != NULL)
    {
        Memory_Free(buffer->_data);
    }
    GenericBuffer_AllocateVariable(buffer, 0, elementSize);
}


// Public functions.
void RandomWeightedCollection_Construct1(RandomWeightedCollection* self, size_t elementSize)
{
    if (self == NULL)
    {
        return;
    }

    ArrayList_Construct1(&self->_items, elementSize);
    GenericBuffer_AllocateVariable(&self->_weights, 0, sizeof(double));
    GenericBuffer_AllocateVariable(&self->_aliasProbabilities, 0, sizeof(double));
    GenericBuffer_AllocateVariable(&self->_aliasIndices, 0, sizeof(size_t));
    GenericBuffer_AllocateVariable(&self->_aliasWorklist, 0, sizeof(size_t));
    self->_totalWeight = 0.0;
    self->_isAliasTableValid = false;
}

void RandomWeightedCollection_Construct2(RandomWeightedCollection* self, size_t elementSize, size_t initialCapacity)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return;
    }

    RandomWeightedCollection_Construct1(self, elementSize);
    if (initialCapacity > 0)
    {
        // A failed reservation still leaves a valid empty collection, so the error is discarded.
        Result = ArrayList_EnsureTotalCapacity(&self->_items, initialCapacity);
        Error_Deconstruct(&Result);
        (void)GenericBuffer_EnsureTotalCapacity(&self->_weights, initialCapacity);
    }
}

void RandomWeightedCollection_Deconstruct(RandomWeightedCollection* self)
{
    if (self == NULL)
    {
        return;
    }

    ArrayList_Deconstruct(&self->_items);
    FreeAndResetBuffer(&self->_weights, sizeof(double));
    FreeAndResetBuffer(&self->_aliasProbabilities, sizeof(double));
    FreeAndResetBuffer(&self->_aliasIndices, sizeof(size_t));
    FreeAndResetBuffer(&self->_aliasWorklist, sizeof(size_t));
    self->_totalWeight = 0.0;
    self->_isAliasTableValid = false;
}

Error RandomWeightedCollection_Add(RandomWeightedCollection* self, void* item, double weight)
{
    Error Result = Error_CreateSuccess();
    Error RollbackResult = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = ValidateWeight(weight);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IList_AddLast(&self->_items._list, item);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_AddLast(&self->_weights, &weight))
    {
        // Keep the parallel arrays in sync by undoing the item append.
        RollbackResult = IList_RemoveLast(&self->_items._list);
        Error_Deconstruct(&RollbackResult);
        return CreateCapacityError(self->_weights._count + 1);
    }

    self->_totalWeight += weight;
    self->_isAliasTableValid = false;
    return Error_CreateSuccess();
}

Error RandomWeightedCollection_AddRange(RandomWeightedCollection* self, void* items, double* weights, size_t count)
{
    Error Result = Error_CreateSuccess();
    Error RollbackResult = Error_CreateSuccess();
    double RangeWeightSum = 0.0;
    size_t Index = 0;
    size_t OldCount = 0;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (count == 0)
    {
        return Error_CreateSuccess();
    }
    if (items == NULL)
    {
        return CreateNullArgumentError(u8"items");
    }
    if (weights == NULL)
    {
        return CreateNullArgumentError(u8"weights");
    }

    Result = ValidateWeightRange(weights, count);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (Index = 0; Index < count; Index++)
    {
        RangeWeightSum += weights[Index];
    }

    OldCount = IList_GetElementCount(&self->_items._list);
    Result = IList_AddRangeLast(&self->_items._list, items, count);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_AddLastRange(&self->_weights, weights, count))
    {
        // Keep the parallel arrays in sync by undoing the item append.
        RollbackResult = IList_RemoveRange(&self->_items._list, OldCount, count);
        Error_Deconstruct(&RollbackResult);
        return CreateCapacityError(OldCount + count);
    }

    self->_totalWeight += RangeWeightSum;
    self->_isAliasTableValid = false;
    return Error_CreateSuccess();
}

Error RandomWeightedCollection_RemoveAt(RandomWeightedCollection* self, size_t index)
{
    Error Result = Error_CreateSuccess();
    double RemovedWeight = 0.0;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = IList_RemoveAt(&self->_items._list, index);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_GetAt(&self->_weights, index, &RemovedWeight)
        || !GenericBuffer_RemoveAt(&self->_weights, index))
    {
        return CreateDesyncError();
    }

    self->_totalWeight -= RemovedWeight;
    ClampTotalWeight(self);
    self->_isAliasTableValid = false;
    return Error_CreateSuccess();
}

Error RandomWeightedCollection_Clear(RandomWeightedCollection* self)
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
    (void)GenericBuffer_Clear(&self->_weights);

    self->_totalWeight = 0.0;
    self->_isAliasTableValid = false;
    return Error_CreateSuccess();
}

Error RandomWeightedCollection_SetItems(RandomWeightedCollection* self, void* items, double* weights, size_t count)
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
    if ((count > 0) && (weights == NULL))
    {
        return CreateNullArgumentError(u8"weights");
    }

    // Validated before clearing so an invalid weight leaves the collection unchanged.
    Result = ValidateWeightRange(weights, count);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = RandomWeightedCollection_Clear(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return RandomWeightedCollection_AddRange(self, items, weights, count);
}

Error RandomWeightedCollection_GetItem(RandomWeightedCollection* self, size_t index, void* outItem)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return IList_GetElement(&self->_items._list, index, outItem);
}

Error RandomWeightedCollection_GetPointerToItem(RandomWeightedCollection* self, size_t index, void** outPointer)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return IList_GetPointerToElement(&self->_items._list, index, outPointer);
}

Error RandomWeightedCollection_GetWeight(RandomWeightedCollection* self, size_t index, double* outWeight)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outWeight == NULL)
    {
        return CreateNullArgumentError(u8"outWeight");
    }

    if (!GenericBuffer_GetAt(&self->_weights, index, outWeight))
    {
        return Error_Construct3(ErrorCode_IndexOutOfBounds,
            u8"Index %zu is outside the valid weighted collection range of %zu elements.",
            index,
            self->_weights._count);
    }

    return Error_CreateSuccess();
}

Error RandomWeightedCollection_SetWeight(RandomWeightedCollection* self, size_t index, double weight)
{
    Error Result = Error_CreateSuccess();
    double OldWeight = 0.0;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = ValidateWeight(weight);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = RandomWeightedCollection_GetWeight(self, index, &OldWeight);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_Replace(&self->_weights, &weight, index))
    {
        return CreateDesyncError();
    }

    self->_totalWeight += weight - OldWeight;
    ClampTotalWeight(self);
    self->_isAliasTableValid = false;
    return Error_CreateSuccess();
}

Error RandomWeightedCollection_GetRandomIndex(RandomWeightedCollection* self, Random* rng, size_t* outIndex)
{
    Error Result = Error_CreateSuccess();
    double* Probabilities = NULL;
    size_t* Aliases = NULL;
    size_t ElementCount = 0;
    size_t ColumnIndex = 0;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (rng == NULL)
    {
        return CreateNullArgumentError(u8"rng");
    }
    if (outIndex == NULL)
    {
        return CreateNullArgumentError(u8"outIndex");
    }

    ElementCount = IList_GetElementCount(&self->_items._list);
    if (ElementCount == 0)
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Cannot pick a random item from an empty weighted collection.");
    }
    if ((uint64_t)ElementCount > (uint64_t)INT64_MAX)
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"The weighted collection has more elements than a pick can index.");
    }

    if (!self->_isAliasTableValid)
    {
        Result = RandomWeightedCollection_RebuildAliasTable(self);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    Probabilities = GenericBuffer_GetPointerToFirst(&self->_aliasProbabilities);
    Aliases = GenericBuffer_GetPointerToFirst(&self->_aliasIndices);
    ColumnIndex = (size_t)Random_NextInt64InLimit(rng, (int64_t)ElementCount);
    if (Random_NextDouble(rng) < Probabilities[ColumnIndex])
    {
        *outIndex = ColumnIndex;
    }
    else
    {
        *outIndex = Aliases[ColumnIndex];
    }

    return Error_CreateSuccess();
}

Error RandomWeightedCollection_GetRandomItem(RandomWeightedCollection* self, Random* rng, void* outItem)
{
    Error Result = Error_CreateSuccess();
    size_t PickedIndex = 0;

    Result = RandomWeightedCollection_GetRandomIndex(self, rng, &PickedIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_GetElement(&self->_items._list, PickedIndex, outItem);
}

Error RandomWeightedCollection_GetRandomItemPointer(RandomWeightedCollection* self, Random* rng, void** outPointer)
{
    Error Result = Error_CreateSuccess();
    size_t PickedIndex = 0;

    Result = RandomWeightedCollection_GetRandomIndex(self, rng, &PickedIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IList_GetPointerToElement(&self->_items._list, PickedIndex, outPointer);
}
