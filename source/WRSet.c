#include "WRSet.h"


// Macros.
// Used as an array size, so it must be a macro (an integer constant expression).
#define SET_MAX_STACK_SCRATCH_SIZE ((size_t)256)


// Types.
// Enumerator buffers require suitable alignment, which a plain byte array does not guarantee.
typedef union SetEnumeratorStackBufferUnion
{
    max_align_t _alignment;
    unsigned char Bytes[SET_MAX_STACK_SCRATCH_SIZE];
} SetEnumeratorStackBuffer;

typedef Error (*SetCollectionVisitor)(void* state, void* element, bool* outShouldContinue);

typedef struct SetMembershipQueryStateStruct
{
    ISet* Lookup;
    bool Result;
} SetMembershipQueryState;

typedef struct SetIntersectionStateStruct
{
    ISet* Lookup;
    GenericBuffer* RemovalScratch;
} SetIntersectionState;


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Set argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateReadOnlyError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The set is read-only.");
}

static Error CreateElementSizeMismatchError(size_t setElementSize, size_t otherElementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"The other side's element size of %zu bytes does not match the set's element size of %zu bytes.",
        otherElementSize,
        setElementSize);
}

static Error ValidateSetPair(ISet* self, ISet* other)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (other == NULL)
    {
        return CreateNullArgumentError(u8"other");
    }
    if (ISet_GetElementSize(self) != ISet_GetElementSize(other))
    {
        return CreateElementSizeMismatchError(ISet_GetElementSize(self), ISet_GetElementSize(other));
    }

    return Error_CreateSuccess();
}

/**
 * Enumerates every element of the collection and hands it to the visitor, preferring by-reference
 * iteration and falling back to by-value copies through a stack (or, for large sizes, heap)
 * scratch. Stops early on a visitor error or when the visitor clears its continue flag.
 */
static Error ForEachCollectionElement(ICollection* collection,
    size_t expectedElementSize,
    SetCollectionVisitor visitor,
    void* state)
{
    SetEnumeratorStackBuffer EnumeratorStackBuffer;
    unsigned char ElementStackBuffer[SET_MAX_STACK_SCRATCH_SIZE];
    void* EnumeratorBuffer = EnumeratorStackBuffer.Bytes;
    void* ElementBuffer = ElementStackBuffer;
    CollectionEnumerator* Enumerator = NULL;
    size_t EnumeratorSize = ICollection_GetEnumeratorSize(collection);
    bool UseReferences = false;
    bool HasNext = false;
    bool ShouldContinue = true;
    Error Result = Error_CreateSuccess();

    if (EnumeratorSize > SET_MAX_STACK_SCRATCH_SIZE)
    {
        EnumeratorBuffer = Memory_Allocate(EnumeratorSize);
    }

    Enumerator = ICollection_InitEnumerator(collection, EnumeratorBuffer);
    if (Enumerator == NULL)
    {
        Result = Error_Construct1(ErrorCode_InvalidState,
            u8"Failed to initialize an enumerator over the collection.");
    }
    else
    {
        if (CollectionEnumerator_GetSingleElementSize(Enumerator) != expectedElementSize)
        {
            Result = CreateElementSizeMismatchError(expectedElementSize,
                CollectionEnumerator_GetSingleElementSize(Enumerator));
        }
        else
        {
            UseReferences = CollectionEnumerator_IsReferenceReturningSupported(Enumerator);
            if (!UseReferences && (expectedElementSize > SET_MAX_STACK_SCRATCH_SIZE))
            {
                ElementBuffer = Memory_Allocate(expectedElementSize);
            }

            while (true)
            {
                void* CurrentElement = ElementBuffer;

                Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
                if ((Result.Code != ErrorCode_Success) || !HasNext)
                {
                    break;
                }

                if (UseReferences)
                {
                    Result = CollectionEnumerator_NextByReference(Enumerator, &CurrentElement);
                }
                else
                {
                    Result = CollectionEnumerator_NextByValue(Enumerator, CurrentElement);
                }
                if (Result.Code != ErrorCode_Success)
                {
                    break;
                }

                Result = visitor(state, CurrentElement, &ShouldContinue);
                if ((Result.Code != ErrorCode_Success) || !ShouldContinue)
                {
                    break;
                }
            }
        }

        CollectionEnumerator_Deconstruct(Enumerator);
    }

    if (EnumeratorBuffer != EnumeratorStackBuffer.Bytes)
    {
        Memory_Free(EnumeratorBuffer);
    }
    if (ElementBuffer != ElementStackBuffer)
    {
        Memory_Free(ElementBuffer);
    }
    return Result;
}

static Error SetVisitor_AddToSet(void* state, void* element, bool* outShouldContinue)
{
    ISet* TargetSet = state;
    bool WasAdded = false;

    *outShouldContinue = true;
    return ISet_Add(TargetSet, element, &WasAdded);
}

static Error SetVisitor_RemoveFromSet(void* state, void* element, bool* outShouldContinue)
{
    ISet* TargetSet = state;
    bool WasRemoved = false;

    *outShouldContinue = true;
    return ISet_Remove(TargetSet, element, &WasRemoved);
}

static Error SetVisitor_ToggleInSet(void* state, void* element, bool* outShouldContinue)
{
    ISet* TargetSet = state;
    bool Contains = false;
    bool WasMutated = false;
    Error Result = ISet_Contains(TargetSet, element, &Contains);

    *outShouldContinue = true;
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (Contains)
    {
        return ISet_Remove(TargetSet, element, &WasMutated);
    }
    return ISet_Add(TargetSet, element, &WasMutated);
}

static Error SetVisitor_CheckOverlap(void* state, void* element, bool* outShouldContinue)
{
    SetMembershipQueryState* QueryState = state;
    bool Contains = false;
    Error Result = ISet_Contains(QueryState->Lookup, element, &Contains);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (Contains)
    {
        QueryState->Result = true;
    }
    *outShouldContinue = !Contains;
    return Error_CreateSuccess();
}

static Error SetVisitor_CheckContainsAll(void* state, void* element, bool* outShouldContinue)
{
    SetMembershipQueryState* QueryState = state;
    bool Contains = false;
    Error Result = ISet_Contains(QueryState->Lookup, element, &Contains);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!Contains)
    {
        QueryState->Result = false;
    }
    *outShouldContinue = Contains;
    return Error_CreateSuccess();
}

static Error SetVisitor_CollectMissing(void* state, void* element, bool* outShouldContinue)
{
    SetIntersectionState* IntersectionState = state;
    bool Contains = false;
    Error Result = ISet_Contains(IntersectionState->Lookup, element, &Contains);

    *outShouldContinue = true;
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Contains && !GenericBuffer_AddLast(IntersectionState->RemovalScratch, element))
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"Could not grow the set intersection scratch storage.");
    }

    return Error_CreateSuccess();
}


// Public functions.
Error ISet_Add(ISet* self, const void* element, bool* outWasAdded)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (element == NULL)
    {
        return CreateNullArgumentError(u8"element");
    }
    if (outWasAdded == NULL)
    {
        return CreateNullArgumentError(u8"outWasAdded");
    }
    if (ISet_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }

    return self->_vtable._add(self->_vtable.Self, element, outWasAdded);
}

Error ISet_Remove(ISet* self, const void* element, bool* outWasRemoved)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (element == NULL)
    {
        return CreateNullArgumentError(u8"element");
    }
    if (outWasRemoved == NULL)
    {
        return CreateNullArgumentError(u8"outWasRemoved");
    }
    if (ISet_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }

    return self->_vtable._remove(self->_vtable.Self, element, outWasRemoved);
}

Error ISet_Clear(ISet* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (ISet_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }

    return self->_vtable._clear(self->_vtable.Self);
}

Error ISet_Contains(ISet* self, const void* element, bool* outContains)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (element == NULL)
    {
        return CreateNullArgumentError(u8"element");
    }
    if (outContains == NULL)
    {
        return CreateNullArgumentError(u8"outContains");
    }

    return self->_vtable._contains(self->_vtable.Self, element, outContains);
}

Error ISet_Deconstruct(ISet* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return self->_vtable._deconstruct(self->_vtable.Self);
}

Error ISet_UnionWith(ISet* self, ICollection* other)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (other == NULL)
    {
        return CreateNullArgumentError(u8"other");
    }
    if (ISet_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }
    if (other == ISet_AsCollection(self))
    {
        return Error_CreateSuccess();
    }

    return ForEachCollectionElement(other, ISet_GetElementSize(self), SetVisitor_AddToSet, self);
}

Error ISet_ExceptWith(ISet* self, ICollection* other)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (other == NULL)
    {
        return CreateNullArgumentError(u8"other");
    }
    if (ISet_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }
    if (other == ISet_AsCollection(self))
    {
        return ISet_Clear(self);
    }

    return ForEachCollectionElement(other, ISet_GetElementSize(self), SetVisitor_RemoveFromSet, self);
}

Error ISet_IntersectWith(ISet* self, ISet* other)
{
    GenericBuffer RemovalScratch;
    SetIntersectionState State = { .Lookup = NULL, .RemovalScratch = NULL };
    Error Result = ValidateSetPair(self, other);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (ISet_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }
    if (self == other)
    {
        return Error_CreateSuccess();
    }

    // Removal is deferred to after the enumeration, since mutating a set invalidates enumerators
    // over it. The scratch buffer only allocates once something actually has to be removed.
    GenericBuffer_AllocateVariable(&RemovalScratch, 0, ISet_GetElementSize(self));
    State.Lookup = other;
    State.RemovalScratch = &RemovalScratch;
    Result = ForEachCollectionElement(ISet_AsCollection(self),
        ISet_GetElementSize(self),
        SetVisitor_CollectMissing,
        &State);

    for (size_t Index = 0; (Result.Code == ErrorCode_Success) && (Index < RemovalScratch._count); Index++)
    {
        bool WasRemoved = false;
        Result = ISet_Remove(self, GenericBuffer_GetPointerToElement(&RemovalScratch, Index), &WasRemoved);
    }

    if (RemovalScratch._data != NULL)
    {
        Memory_Free(RemovalScratch._data);
    }
    return Result;
}

Error ISet_SymmetricExceptWith(ISet* self, ISet* other)
{
    Error Result = ValidateSetPair(self, other);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (ISet_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }
    if (self == other)
    {
        return ISet_Clear(self);
    }

    return ForEachCollectionElement(ISet_AsCollection(other),
        ISet_GetElementSize(self),
        SetVisitor_ToggleInSet,
        self);
}

Error ISet_Overlaps(ISet* self, ICollection* other, bool* outOverlaps)
{
    SetMembershipQueryState State = { .Lookup = NULL, .Result = false };
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (other == NULL)
    {
        return CreateNullArgumentError(u8"other");
    }
    if (outOverlaps == NULL)
    {
        return CreateNullArgumentError(u8"outOverlaps");
    }
    if (other == ISet_AsCollection(self))
    {
        *outOverlaps = ISet_GetElementCount(self) > 0;
        return Error_CreateSuccess();
    }

    State.Lookup = self;
    Result = ForEachCollectionElement(other, ISet_GetElementSize(self), SetVisitor_CheckOverlap, &State);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outOverlaps = State.Result;
    return Error_CreateSuccess();
}

Error ISet_IsSubsetOf(ISet* self, ISet* other, bool* outIsSubset)
{
    SetMembershipQueryState State = { .Lookup = NULL, .Result = true };
    Error Result = ValidateSetPair(self, other);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outIsSubset == NULL)
    {
        return CreateNullArgumentError(u8"outIsSubset");
    }
    if (self == other)
    {
        *outIsSubset = true;
        return Error_CreateSuccess();
    }
    if (ISet_GetElementCount(self) > ISet_GetElementCount(other))
    {
        *outIsSubset = false;
        return Error_CreateSuccess();
    }

    State.Lookup = other;
    Result = ForEachCollectionElement(ISet_AsCollection(self),
        ISet_GetElementSize(self),
        SetVisitor_CheckContainsAll,
        &State);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outIsSubset = State.Result;
    return Error_CreateSuccess();
}

Error ISet_IsSupersetOf(ISet* self, ICollection* other, bool* outIsSuperset)
{
    SetMembershipQueryState State = { .Lookup = NULL, .Result = true };
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (other == NULL)
    {
        return CreateNullArgumentError(u8"other");
    }
    if (outIsSuperset == NULL)
    {
        return CreateNullArgumentError(u8"outIsSuperset");
    }
    if (other == ISet_AsCollection(self))
    {
        *outIsSuperset = true;
        return Error_CreateSuccess();
    }

    State.Lookup = self;
    Result = ForEachCollectionElement(other, ISet_GetElementSize(self), SetVisitor_CheckContainsAll, &State);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outIsSuperset = State.Result;
    return Error_CreateSuccess();
}

Error ISet_SetEquals(ISet* self, ISet* other, bool* outEquals)
{
    SetMembershipQueryState State = { .Lookup = NULL, .Result = true };
    Error Result = ValidateSetPair(self, other);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outEquals == NULL)
    {
        return CreateNullArgumentError(u8"outEquals");
    }
    if (self == other)
    {
        *outEquals = true;
        return Error_CreateSuccess();
    }
    if (ISet_GetElementCount(self) != ISet_GetElementCount(other))
    {
        *outEquals = false;
        return Error_CreateSuccess();
    }

    State.Lookup = other;
    Result = ForEachCollectionElement(ISet_AsCollection(self),
        ISet_GetElementSize(self),
        SetVisitor_CheckContainsAll,
        &State);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outEquals = State.Result;
    return Error_CreateSuccess();
}
