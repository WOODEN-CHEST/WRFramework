#pragma once
#include "WRMap.h"


// Types.
typedef HashCode (*HashMapKeyHashFunction)(IMap* map, const void* key, const UserData* userData);

typedef struct HashMapStruct
{
    IMap _map;
    HashMapKeyHashFunction _keyHashFunction;
    UserData _keyHashFunctionUserData;
    MapKeyComparator _keyComparator;
    UserData _keyComparatorUserData;
    MapValueComparator _valueComparator;
    UserData _valueComparatorUserData;
    GenericBuffer _dataBuffer;
    bool _isActiveBufferOwned;
    size_t _entryCount;
    size_t _tombstoneCount;
    size_t _capacity;
} HashMap;

typedef struct HashMapConstructOptionsStruct
{
    size_t KeySize;
    size_t ValueSize;
    HashMapKeyHashFunction KeyHashFunction;
    UserData KeyHashFunctionUserData;
    MapKeyComparator KeyComparator;
    UserData KeyComparatorUserData;
    MapValueComparator ValueComparator;
    UserData ValueComparatorUserData;
    size_t InitialCapacity;
} HashMapConstructOptions;


// Functions.
static inline HashMapConstructOptions HashMapConstructOptions_CreateDefault(size_t keySize, 
    size_t valueSize,
    HashMapKeyHashFunction keyHashFunction)
{
    return (HashMapConstructOptions) {
        .KeySize = keySize,
        .ValueSize = valueSize,
        .KeyHashFunction = keyHashFunction,
        .KeyHashFunctionUserData = UserData_CreateEmpty(),
        .KeyComparator = NULL,
        .KeyComparatorUserData = UserData_CreateEmpty(),
        .ValueComparator = NULL,
        .ValueComparatorUserData = UserData_CreateEmpty(),
        .InitialCapacity = 0,
    };
}

static inline IMap* HashMap_AsMap(HashMap* self)
{
    return &self->_map;
}

Error HashMap_Construct1(HashMap* self, HashMapConstructOptions options);

Error HashMap_Deconstruct(HashMap* self);
