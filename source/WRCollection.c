#include "WRCollection.h"


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Collection enumerator argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateReferenceEnumerationUnsupportedError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"This collection enumerator does not support returning elements by reference.");
}

static Error CreateInvalidArgumentError(const unsigned char* argumentName, const unsigned char* message)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Argument \"%s\" is invalid: %s.",
        argumentName,
        message);
}

static Error CreateDestinationBufferTooSmallError()
{
    return Error_Construct3(ErrorCode_BufferTooSmall,
        u8"The destination buffer is too small to hold the collection elements.");
}

static Error WriteToBuffer(ICollection* self, GenericBuffer* buffer, bool isByReference)
{
    if ((self == NULL) || (buffer == NULL))
    {
        return CreateNullArgumentError(u8"self");
    }

    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(self);
    size_t ElementSize = isByReference ? sizeof(void*) : CollectionEnumerator_GetSingleElementSize(Enumerator);
    if (buffer->_elementSize != ElementSize)
    {
        CollectionEnumerator_Destroy(Enumerator);
        return CreateInvalidArgumentError(u8"buffer", u8"element size mismatch");
    }
    
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while (HasNext && (Result.Code == ErrorCode_Success))
    {
        void* TargetTail = NULL;
        if (!GenericBuffer_GetWritableTail(buffer, 1, &TargetTail))
        {
            CollectionEnumerator_Destroy(Enumerator);
            return CreateDestinationBufferTooSmallError();
        }
        unsigned char* TargetData = TargetTail;

        if (isByReference)
        {
            void* TargetPointer = NULL;
            Result = CollectionEnumerator_NextByReference(Enumerator, &TargetPointer);
            if (Result.Code == ErrorCode_Success)
            {
                Memory_Copy(&TargetPointer, TargetData, sizeof(TargetPointer));
            }
        }
        else
        {
            Result = CollectionEnumerator_NextByValue(Enumerator, TargetData);
        }

        if (Result.Code != ErrorCode_Success)
        {
            CollectionEnumerator_Destroy(Enumerator);
            return Result;
        }
        GenericBuffer_CommitCount(buffer, 1);

        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    }

    CollectionEnumerator_Destroy(Enumerator);
    return Error_CreateSuccess();
}


// Public functions.
Error ICollection_WriteToBufferByValue(ICollection* self, GenericBuffer* buffer)
{
    return WriteToBuffer(self, buffer, false);
}

Error ICollection_WriteToBufferByReference(ICollection* self, GenericBuffer* buffer)
{
    return WriteToBuffer(self, buffer, true);
}

Error CollectionEnumerator_NextByReference(CollectionEnumerator* self, void** outPointer)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outPointer == NULL)
    {
        return CreateNullArgumentError(u8"outPointer");
    }
    if (!CollectionEnumerator_IsReferenceReturningSupported(self))
    {
        *outPointer = NULL;
        return CreateReferenceEnumerationUnsupportedError();
    }

    return (*self->_vtable._nextByReference)(self->_vtable.Self, outPointer);
}
