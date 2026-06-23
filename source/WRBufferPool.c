#include "WRBufferPool.h"
#include "WRCollection.h"
#include "WRHash.h"
#include "WRObjectPool.h"


// Macros.
#define BUFFER_POOL_SECTION_CAPACITY ((size_t)16)


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Buffer pool argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateArgumentOutOfRangeError(const unsigned char* argumentName, const unsigned char* message)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Buffer pool argument \"%s\" is out of range: %s.",
        argumentName,
        message);
}

static Error CreateForeignBufferError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument,
        u8"The provided buffer does not belong to this buffer pool.");
}

static HashCode BufferPool_HashElementSize(IMap* map, const void* key, const UserData* userData)
{
    const size_t* ElementSize = key;

    (void)map;
    (void)userData;
    return Hash_SizeT(*ElementSize);
}

static Error ValidatePool(WRBufferPool* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Error_CreateSuccess();
}

static Error ValidatePoolAndOutput(WRBufferPool* self, GenericBuffer** outBuffer)
{
    Error Result = ValidatePool(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outBuffer == NULL)
    {
        return CreateNullArgumentError(u8"outBuffer");
    }

    return Error_CreateSuccess();
}

static Error BufferPool_ResetBorrowedBuffer(GenericBuffer* buffer)
{
    if (GenericBuffer_Clear(buffer))
    {
        return Error_CreateSuccess();
    }

    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"Could not clear the pooled buffer.");
}

static Error BufferPool_InitializeBorrowedBuffer(void* object, const UserData* userData)
{
    GenericBuffer* Buffer = object;

    if (Buffer == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    (void)userData;
    GenericBuffer_CreateVariable(Buffer,
        NULL,
        0,
        0,
        0,
        NULL,
        NULL);
    return Error_CreateSuccess();
}

static Error BufferPool_ResetBufferObject(void* object, const UserData* userData)
{
    GenericBuffer* Buffer = object;

    if (Buffer == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    (void)userData;
    return BufferPool_ResetBorrowedBuffer(Buffer);
}

static Error BufferPool_DeconstructBufferObject(void* object, const UserData* userData)
{
    GenericBuffer* Buffer = object;

    if (Buffer == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    (void)userData;
    Memory_Free(Buffer->_data);
    GenericBuffer_CreateVariable(Buffer,
        NULL,
        0,
        sizeof(unsigned char),
        0,
        NULL,
        NULL);
    return Error_CreateSuccess();
}

static Error ConstructEntryPool(ObjectPool* outPool)
{
    ObjectPoolLifecycle Lifecycle =
    {
        .ConstructObject = BufferPool_InitializeBorrowedBuffer,
        .ResetObject = BufferPool_ResetBufferObject,
        .DeconstructObject = BufferPool_DeconstructBufferObject,
    };

    return ObjectPool_Construct2(outPool,
        sizeof(GenericBuffer),
        BUFFER_POOL_SECTION_CAPACITY,
        Lifecycle,
        NULL);
}

static Error GetOrCreateEntryPool(WRBufferPool* self, size_t elementSize, ObjectPool** outPool)
{
    bool WasAdded = false;
    ObjectPool NewPool;
    Error Result = IMap_GetPointerToElement(HashMap_AsMap(&self->_entries), &elementSize, (void**)outPool);

    if (Result.Code == ErrorCode_Success)
    {
        return Error_CreateSuccess();
    }
    if (Result.Code != ErrorCode_InvalidOperation)
    {
        return Result;
    }

    Result = ConstructEntryPool(&NewPool);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IMap_Add(HashMap_AsMap(&self->_entries), &elementSize, &NewPool, &WasAdded);
    if (Result.Code != ErrorCode_Success)
    {
        Error DeconstructResult = ObjectPool_Deconstruct(&NewPool);
        if (DeconstructResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&DeconstructResult);
        }
        return Result;
    }
    if (!WasAdded)
    {
        Error DeconstructResult = ObjectPool_Deconstruct(&NewPool);
        if (DeconstructResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&DeconstructResult);
        }
    }

    return IMap_GetPointerToElement(HashMap_AsMap(&self->_entries), &elementSize, (void**)outPool);
}

static Error GetEntryPool(WRBufferPool* self, size_t elementSize, ObjectPool** outPool)
{
    if (outPool == NULL)
    {
        return CreateNullArgumentError(u8"outPool");
    }

    return IMap_GetPointerToElement(HashMap_AsMap(&self->_entries), &elementSize, (void**)outPool);
}


// Public functions.
Error BufferPool_Construct1(WRBufferPool* self)
{
    HashMapConstructOptions Options = HashMapConstructOptions_CreateDefault(sizeof(size_t),
        sizeof(ObjectPool),
        BufferPool_HashElementSize);

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    return HashMap_Construct1(&self->_entries, Options);
}

Error BufferPool_Deconstruct(WRBufferPool* self)
{
    Error FirstError = Error_CreateSuccess();
    Error Result = Error_CreateSuccess();
    CollectionEnumerator* Enumerator = NULL;
    bool HasNext = false;

    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Enumerator = ICollection_GetEnumerator(IMap_AsEntryCollection(HashMap_AsMap(&self->_entries)));

    // Best-effort teardown: keep the first error, and deconstruct every later one so its message is
    // not leaked. Error_Deconstruct on a success error is a no-op, so the success path is safe too.
    Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    if (FirstError.Code == ErrorCode_Success)
    {
        FirstError = Result;
    }
    else
    {
        Error_Deconstruct(&Result);
    }

    while (HasNext && (Result.Code == ErrorCode_Success))
    {
        MapEntryView EntryView;
        ObjectPool* Pool = NULL;

        Result = CollectionEnumerator_NextByValue(Enumerator, &EntryView);
        if (Result.Code != ErrorCode_Success)
        {
            if (FirstError.Code == ErrorCode_Success)
            {
                FirstError = Result;
            }
            else
            {
                Error_Deconstruct(&Result);
            }
            break;
        }

        Pool = EntryView._value;
        Result = ObjectPool_Deconstruct(Pool);
        if (FirstError.Code == ErrorCode_Success)
        {
            FirstError = Result;
        }
        else
        {
            Error_Deconstruct(&Result);
        }

        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if (FirstError.Code == ErrorCode_Success)
        {
            FirstError = Result;
        }
        else
        {
            Error_Deconstruct(&Result);
        }
    }

    if (Enumerator != NULL)
    {
        CollectionEnumerator_Deconstruct(Enumerator);
    }

    Result = HashMap_Deconstruct(&self->_entries);
    if (FirstError.Code == ErrorCode_Success)
    {
        FirstError = Result;
    }
    else
    {
        Error_Deconstruct(&Result);
    }

    Memory_Zero(self, sizeof(*self));
    return FirstError;
}

Error BufferPool_Borrow(WRBufferPool* self, size_t elementSize, GenericBuffer** outBuffer)
{
    ObjectPool* Pool = NULL;
    void* BorrowedObject = NULL;
    Error Result = ValidatePoolAndOutput(self, outBuffer);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (elementSize == 0)
    {
        *outBuffer = NULL;
        return CreateArgumentOutOfRangeError(u8"elementSize", u8"must be greater than zero");
    }

    *outBuffer = NULL;
    Result = GetOrCreateEntryPool(self, elementSize, &Pool);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ObjectPool_GetNewObject(Pool, &BorrowedObject);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (((GenericBuffer*)BorrowedObject)->_elementSize == 0)
    {
        // Initialize a fresh pooled slot as an empty growable buffer: AllocateVariable with zero
        // initial capacity installs the default growth callback without allocating. Returned
        // buffers keep their capacity/callback and skip this on reborrow.
        GenericBuffer_AllocateVariable(BorrowedObject, 0, elementSize);
    }

    Result = BufferPool_ResetBorrowedBuffer(BorrowedObject);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = ObjectPool_DisposeObject(Pool, BorrowedObject);
        if (ReturnResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }

    *outBuffer = BorrowedObject;
    return Error_CreateSuccess();
}

Error BufferPool_Return(WRBufferPool* self, GenericBuffer* bufferToReturn)
{
    ObjectPool* Pool = NULL;
    Error Result = ValidatePool(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (bufferToReturn == NULL)
    {
        return CreateNullArgumentError(u8"bufferToReturn");
    }
    if (bufferToReturn->_elementSize == 0)
    {
        return CreateForeignBufferError();
    }

    Result = GetEntryPool(self, bufferToReturn->_elementSize, &Pool);
    if (Result.Code == ErrorCode_InvalidOperation)
    {
        return CreateForeignBufferError();
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ObjectPool_DisposeObject(Pool, bufferToReturn);
    if (Result.Code == ErrorCode_IllegalArgument)
    {
        return CreateForeignBufferError();
    }

    return Result;
}
