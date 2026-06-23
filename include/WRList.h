#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"
#include "WRCollection.h"
#include "WRComparator.h"
#include "WRMemory.h"
#include "WRUserData.h"


#define LIST_INDEX_INVALID (~((size_t)0))


// Types.
typedef enum IListFlagsEnum
{
    IListFlags_None = 0,
    IListFlags_IsReadOnly = (1 << 0),
} IListFlags;

typedef struct IListVTableStruct
{
    void* Self;

    size_t (*_getElementCount)(void* self);

    Error (*_insert)(void* self, size_t index, void* element);
    Error (*_remove)(void* self, size_t index);
    Error (*_replace)(void* self, size_t index, void* element);
    Error (*_insertRange)(void* self, size_t index, void* elements, size_t elementCount);
    Error (*_removeRange)(void* self, size_t index, size_t elementCount);
    Error (*_replaceRange)(void* self, size_t index, void* elements, size_t elementCount);
    
    Error (*_clear)(void* self);

    Error (*_getElement)(void* self, size_t index, void* outElement);
    Error (*_getPointerToElement)(void* self, size_t index, void** outElementPointer);

    void (*_deconstruct)(void* self);
} IListVTable;

typedef struct IListStruct
{
    size_t _elementSize;
    IListFlags _flags;
    IListVTable _vtable;
    ICollection _collection;
} IList;

typedef struct IListElementDataStruct
{
    void* _element;
    size_t _index;
} IListElementData;

typedef bool (*IListPredicate)(IList* list, IListElementData element, const UserData* userData);

typedef ComparisonResult (*IListComparator)(IList* list, IListElementData a, IListElementData b, const UserData* userData);

typedef void (*IListMapper)(IList* list, IListElementData sourceElement, void* destinationElement, const UserData* userData);

typedef int64_t (*IListIntExtractor)(IList* list, IListElementData sourceElement, const UserData* userData);

typedef double (*IListDoubleExtractor)(IList* list, IListElementData sourceElement, const UserData* userData);



// Functions.
static inline ICollection* IList_AsCollection(IList* self)
{
    return &self->_collection;
}

static inline CollectionEnumerator* IList_GetEnumerator(IList* self)
{
    return ICollection_GetEnumerator(IList_AsCollection(self));
}

static inline size_t IList_GetElementCount(IList* self)
{
    return self->_vtable._getElementCount(self->_vtable.Self);
}

static inline size_t IList_GetElementSize(IList* self)
{
    return self->_elementSize;
}

static inline IListFlags IList_GetFlags(IList* self)
{
    return self->_flags;
}

static inline bool IList_IsReadOnly(IList* self)
{
    return (IList_GetFlags(self) & IListFlags_IsReadOnly) != 0;
}

Error IList_AddLast(IList* self, void* element);

Error IList_AddFirst(IList* self, void* element);

Error IList_Insert(IList* self, size_t index, void* element);

Error IList_RemoveFirst(IList* self);

Error IList_RemoveLast(IList* self);

Error IList_RemoveAt(IList* self, size_t index);

Error IList_Replace(IList* self, size_t index, void* element);

Error IList_AddRangeLast(IList* self, void* elements, size_t elementCount);

Error IList_AddRangeFirst(IList* self, void* elements, size_t elementCount);

Error IList_InsertRange(IList* self, size_t index, void* elements, size_t elementCount);

Error IList_RemoveRange(IList* self, size_t index, size_t elementCount);

Error IList_ReplaceRange(IList* self, size_t index, void* elements, size_t elementCount);

Error IList_Clear(IList* self);



Error IList_GetElement(IList* self, size_t index, void* outElement);

Error IList_GetPointerToElement(IList* self, size_t index, void** outElementPointer);

Error IList_GetFirst(IList* self, void* outElement);

Error IList_GetLast(IList* self, void* outElement);



/**
 * Scan / sort / reduce operations need a small element-sized scratch buffer (because list elements
 * may only be reachable by value through the interface). Each operation comes in two forms:
 *
 *   - The plain form (e.g. IList_Filter) takes a caller-owned `scratch` buffer. PREFER THIS FORM:
 *     allocate one scratch buffer of IList_GetScratchSize(self) bytes, reuse it across many calls,
 *     and these operations perform no per-call allocation. Passing NULL for `scratch` makes the
 *     call allocate and free internally (a convenience, but it hides an allocation).
 *   - The ...Allocating form (e.g. IList_FilterAllocating) always allocates and frees the scratch
 *     for you. Use it only when you have no reusable scratch buffer to provide.
 *
 * `scratch` must be at least IList_GetScratchSize(self) bytes; that size is safe for every operation
 * here (it is sized for the largest case).
 */
static inline size_t IList_GetScratchSize(IList* self)
{
    return self->_elementSize * 2;
}

Error IList_SortAscending(IList* self, IListComparator comparator, const UserData* userData, void* scratch);
Error IList_SortAscendingAllocating(IList* self, IListComparator comparator, const UserData* userData);

Error IList_SortDescending(IList* self, IListComparator comparator, const UserData* userData, void* scratch);
Error IList_SortDescendingAllocating(IList* self, IListComparator comparator, const UserData* userData);

Error IList_Filter(IList* self, IListPredicate predicate, const UserData* userData, void* scratch);
Error IList_FilterAllocating(IList* self, IListPredicate predicate, const UserData* userData);

Error IList_Map(IList* self, GenericBuffer* destination, IListMapper mapper, const UserData* userData, void* scratch);
Error IList_MapAllocating(IList* self, GenericBuffer* destination, IListMapper mapper, const UserData* userData);

Error IList_SumInt(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue, void* scratch);
Error IList_SumIntAllocating(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue);

Error IList_SumDouble(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue, void* scratch);
Error IList_SumDoubleAllocating(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue);

Error IList_MaxInt(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue, void* scratch);
Error IList_MaxIntAllocating(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue);

Error IList_MaxDouble(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue, void* scratch);
Error IList_MaxDoubleAllocating(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue);

Error IList_MinInt(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue, void* scratch);
Error IList_MinIntAllocating(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue);

Error IList_MinDouble(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue, void* scratch);
Error IList_MinDoubleAllocating(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue);

Error IList_Contains(IList* self, IListPredicate predicate, const UserData* userData, bool* outValue, void* scratch);
Error IList_ContainsAllocating(IList* self, IListPredicate predicate, const UserData* userData, bool* outValue);

Error IList_CountWhere(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue, void* scratch);
Error IList_CountWhereAllocating(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue);

Error IList_FirstIndexOf(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue, void* scratch);
Error IList_FirstIndexOfAllocating(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue);

Error IList_LastIndexOf(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue, void* scratch);
Error IList_LastIndexOfAllocating(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue);

Error IList_Reverse(IList* self, void* scratch);
Error IList_ReverseAllocating(IList* self);

Error IList_CopyTo(IList* self, GenericBuffer* destination, void* scratch);
Error IList_CopyToAllocating(IList* self, GenericBuffer* destination);


Error IList_Deconstruct(IList* self);
