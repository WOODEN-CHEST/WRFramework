#include "WRObjectPool.h"


// Macros.
#define OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE 8U


// Types.
typedef struct ObjectPoolSectionStruct
{
    unsigned char* Objects;
    unsigned char* OccupancyBits;
    size_t* FreeSlotStack;
    size_t Capacity;
    size_t InitializedCount;
    size_t OccupiedCount;
    size_t FreeSlotCount;
    bool IsListedAvailable;
} ObjectPoolSection;


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Object pool argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateArgumentOutOfRangeError(const unsigned char* argumentName, const unsigned char* message)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Object pool argument \"%s\" is out of range: %s.",
        argumentName,
        message);
}

static Error CreateInvalidObjectError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument,
        u8"The provided object pointer does not belong to this object pool.");
}

static Error CreatePoolTooLargeError(void)
{
    return Error_Construct1(ErrorCode_BufferTooLarge,
        u8"The object pool cannot grow further because it would exceed addressable capacity.");
}

static Error ValidatePool(ObjectPool* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Error_CreateSuccess();
}

static Error ValidatePoolAndOutput(ObjectPool* self, void** outObject)
{
    Error Result = ValidatePool(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outObject == NULL)
    {
        return CreateNullArgumentError(u8"outObject");
    }

    return Error_CreateSuccess();
}

static bool TryMultiplySizes(size_t left, size_t right, size_t* outProduct)
{
    if (outProduct == NULL)
    {
        return false;
    }
    if ((left > 0) && (right > (SIZE_MAX / left)))
    {
        return false;
    }

    *outProduct = left * right;
    return true;
}

static size_t GetSectionCount(ObjectPool* self)
{
    return self->_sections._count;
}

static ObjectPoolSection* GetSection(ObjectPool* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_sections, index);
}

static size_t GetSectionIndexByAddressAt(ObjectPool* self, size_t position)
{
    return *(size_t*)GenericBuffer_GetPointerToElement(&self->_sectionsByAddress, position);
}

static size_t GetMaxSectionCapacity(ObjectPool* self)
{
    size_t MaxCapacity = OBJECT_POOL_MAX_SECTION_BYTES / self->_elementSize;

    return (MaxCapacity < self->_sectionCapacity) ? self->_sectionCapacity : MaxCapacity;
}

static size_t GetNextSectionCapacity(ObjectPool* self)
{
    size_t SectionCount = GetSectionCount(self);
    size_t MaxCapacity = GetMaxSectionCapacity(self);

    if (SectionCount == 0)
    {
        return self->_sectionCapacity;
    }

    // Every section holds at most MaxCapacity objects, so the subtraction cannot wrap.
    size_t PreviousCapacity = GetSection(self, SectionCount - 1)->Capacity;
    if (PreviousCapacity > (MaxCapacity - PreviousCapacity))
    {
        return MaxCapacity;
    }

    return PreviousCapacity * 2U;
}

static size_t GetOccupancyByteCount(size_t sectionCapacity)
{
    size_t OccupancyByteCount = sectionCapacity / OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE;

    if ((sectionCapacity % OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE) != 0U)
    {
        OccupancyByteCount++;
    }

    return OccupancyByteCount;
}

static unsigned char GetOccupancyMask(size_t slotIndex)
{
    return (unsigned char)(1U << (unsigned int)(slotIndex % OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE));
}

static bool IsSlotOccupied(const ObjectPoolSection* section, size_t slotIndex)
{
    size_t ByteIndex = slotIndex / OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE;

    return (section->OccupancyBits[ByteIndex] & GetOccupancyMask(slotIndex)) != 0U;
}

static void SetSlotOccupied(ObjectPoolSection* section, size_t slotIndex, bool isOccupied)
{
    size_t ByteIndex = slotIndex / OBJECT_POOL_BITS_PER_OCCUPANCY_BYTE;
    unsigned char Mask = GetOccupancyMask(slotIndex);

    if (isOccupied)
    {
        section->OccupancyBits[ByteIndex] |= Mask;
        return;
    }

    section->OccupancyBits[ByteIndex] &= (unsigned char)(~Mask);
}

static bool SectionHasAvailableObject(const ObjectPoolSection* section)
{
    return ((section->FreeSlotCount > 0) || (section->InitializedCount < section->Capacity));
}

// Room for every section is reserved when a section is added (AddSection), so listing one never grows the list.
static void ListSectionAsAvailable(ObjectPool* self, size_t sectionIndex)
{
    ObjectPoolSection* Section = GetSection(self, sectionIndex);

    if (Section->IsListedAvailable)
    {
        return;
    }

    GenericBuffer_AddLast(&self->_availableSections, &sectionIndex);
    Section->IsListedAvailable = true;
}

static void RebuildAvailableSections(ObjectPool* self)
{
    GenericBuffer_Clear(&self->_availableSections);

    // Listed from the last section down, so borrows start again at the first section.
    for (size_t Offset = 0; Offset < GetSectionCount(self); Offset++)
    {
        size_t SectionIndex = GetSectionCount(self) - 1 - Offset;
        ObjectPoolSection* Section = GetSection(self, SectionIndex);

        Section->IsListedAvailable = false;
        if (SectionHasAvailableObject(Section))
        {
            ListSectionAsAvailable(self, SectionIndex);
        }
    }
}

// The number of sections whose objects start at or below the address: where a section starting there is inserted
// in _sectionsByAddress, and one past the only section that could hold an object there.
static size_t CountSectionsStartingAtOrBelow(ObjectPool* self, uintptr_t address)
{
    size_t Low = 0;
    size_t High = self->_sectionsByAddress._count;

    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        ObjectPoolSection* Section = GetSection(self, GetSectionIndexByAddressAt(self, Middle));

        if ((uintptr_t)Section->Objects <= address)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }

    return Low;
}

static unsigned char* GetObjectAddress(ObjectPool* self, ObjectPoolSection* section, size_t slotIndex)
{
    return section->Objects + (slotIndex * self->_elementSize);
}

static Error ResetObjectForReuse(ObjectPool* self, void* object)
{
    if (self->_lifecycle.ResetObject != NULL)
    {
        return self->_lifecycle.ResetObject(object, &self->_userData);
    }

    Memory_Zero(object, self->_elementSize);
    return Error_CreateSuccess();
}

static Error ConstructObjectIfNeeded(ObjectPool* self, void* object)
{
    if (self->_lifecycle.ConstructObject != NULL)
    {
        return self->_lifecycle.ConstructObject(object, &self->_userData);
    }

    return Error_CreateSuccess();
}

static Error DeconstructObjectIfNeeded(ObjectPool* self, void* object)
{
    if (self->_lifecycle.DeconstructObject != NULL)
    {
        return self->_lifecycle.DeconstructObject(object, &self->_userData);
    }

    return Error_CreateSuccess();
}

static Error ResetSectionForReuse(ObjectPool* self, ObjectPoolSection* section)
{
    size_t OccupancyByteCount = GetOccupancyByteCount(section->Capacity);

    for (size_t SlotIndex = 0; SlotIndex < section->InitializedCount; SlotIndex++)
    {
        if (!IsSlotOccupied(section, SlotIndex))
        {
            continue;
        }

        Error Result = ResetObjectForReuse(self, GetObjectAddress(self, section, SlotIndex));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    Memory_Zero(section->OccupancyBits, OccupancyByteCount);
    section->OccupiedCount = 0;
    section->FreeSlotCount = section->InitializedCount;

    for (size_t SlotIndex = 0; SlotIndex < section->InitializedCount; SlotIndex++)
    {
        section->FreeSlotStack[SlotIndex] = SlotIndex;
    }

    return Error_CreateSuccess();
}

static void DeconstructSection(ObjectPoolSection* section)
{
    if (section == NULL)
    {
        return;
    }

    Memory_Free(section->Objects);
    Memory_Free(section->OccupancyBits);
    Memory_Free(section->FreeSlotStack);
    Memory_Zero(section, sizeof(*section));
}

static Error ConstructSection(ObjectPool* self, size_t capacity, ObjectPoolSection* outSection)
{
    size_t ObjectBytes = 0;
    size_t OccupancyByteCount = 0;
    size_t FreeStackBytes = 0;

    if (outSection == NULL)
    {
        return CreateNullArgumentError(u8"outSection");
    }
    if (!TryMultiplySizes(capacity, self->_elementSize, &ObjectBytes))
    {
        return CreatePoolTooLargeError();
    }

    OccupancyByteCount = GetOccupancyByteCount(capacity);
    if (!TryMultiplySizes(capacity, sizeof(size_t), &FreeStackBytes))
    {
        return CreatePoolTooLargeError();
    }

    Memory_Zero(outSection, sizeof(*outSection));
    outSection->Capacity = capacity;
    outSection->Objects = Memory_Allocate(ObjectBytes);
    outSection->OccupancyBits = Memory_Allocate(OccupancyByteCount);
    outSection->FreeSlotStack = Memory_Allocate(FreeStackBytes);
    Memory_Zero(outSection->Objects, ObjectBytes);
    Memory_Zero(outSection->OccupancyBits, OccupancyByteCount);
    return Error_CreateSuccess();
}

static Error AddSection(ObjectPool* self, size_t* outSectionIndex)
{
    size_t SectionCount = GetSectionCount(self);
    ObjectPoolSection NewSection;

    if (outSectionIndex == NULL)
    {
        return CreateNullArgumentError(u8"outSectionIndex");
    }
    if (SectionCount == SIZE_MAX)
    {
        return CreatePoolTooLargeError();
    }

    // Room for the new section in all three lists first, so nothing below fails half way and listing a section as
    // available never has to grow its list.
    if (!GenericBuffer_EnsureTotalCapacity(&self->_sections, SectionCount + 1U)
        || !GenericBuffer_EnsureTotalCapacity(&self->_availableSections, SectionCount + 1U)
        || !GenericBuffer_EnsureTotalCapacity(&self->_sectionsByAddress, SectionCount + 1U))
    {
        return CreatePoolTooLargeError();
    }

    Error Result = ConstructSection(self, GetNextSectionCapacity(self), &NewSection);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t AddressPosition = CountSectionsStartingAtOrBelow(self, (uintptr_t)NewSection.Objects);
    if (!GenericBuffer_Insert(&self->_sectionsByAddress, &SectionCount, AddressPosition))
    {
        DeconstructSection(&NewSection);
        return CreatePoolTooLargeError();
    }
    if (!GenericBuffer_AddLast(&self->_sections, &NewSection))
    {
        GenericBuffer_RemoveAt(&self->_sectionsByAddress, AddressPosition);
        DeconstructSection(&NewSection);
        return CreatePoolTooLargeError();
    }

    *outSectionIndex = SectionCount;
    return Error_CreateSuccess();
}

// Borrows take from the section listed last: it has a slot to hand out, or a new section is added and listed.
static Error GetAvailableSection(ObjectPool* self, size_t* outSectionIndex)
{
    if (self->_availableSections._count > 0)
    {
        *outSectionIndex = *(size_t*)GenericBuffer_GetPointerToLast(&self->_availableSections);
        return Error_CreateSuccess();
    }

    Error Result = AddSection(self, outSectionIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ListSectionAsAvailable(self, *outSectionIndex);
    return Error_CreateSuccess();
}

static Error TryFindSectionByObject(ObjectPool* self, void* object, size_t* outSectionIndex, size_t* outSlotIndex)
{
    uintptr_t TargetAddress = (uintptr_t)object;

    if (outSectionIndex == NULL)
    {
        return CreateNullArgumentError(u8"outSectionIndex");
    }
    if (outSlotIndex == NULL)
    {
        return CreateNullArgumentError(u8"outSlotIndex");
    }

    size_t Position = CountSectionsStartingAtOrBelow(self, TargetAddress);
    if (Position == 0)
    {
        return CreateInvalidObjectError();
    }

    size_t SectionIndex = GetSectionIndexByAddressAt(self, Position - 1);
    ObjectPoolSection* Section = GetSection(self, SectionIndex);
    size_t OffsetBytes = (size_t)(TargetAddress - (uintptr_t)Section->Objects);

    // A section's byte count was checked against overflow when it was made.
    if ((OffsetBytes >= (Section->Capacity * self->_elementSize)) || ((OffsetBytes % self->_elementSize) != 0U))
    {
        return CreateInvalidObjectError();
    }

    *outSectionIndex = SectionIndex;
    *outSlotIndex = OffsetBytes / self->_elementSize;
    return Error_CreateSuccess();
}


// Public functions.
Error ObjectPool_Construct2(ObjectPool* self,
    size_t elementSize,
    size_t sectionCapacity,
    ObjectPoolLifecycle lifecycle,
    const UserData* userData)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (elementSize == 0)
    {
        return CreateArgumentOutOfRangeError(u8"elementSize", u8"must be greater than zero");
    }
    if (sectionCapacity == 0)
    {
        return CreateArgumentOutOfRangeError(u8"sectionCapacity", u8"must be greater than zero");
    }

    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->_sections, 0, sizeof(ObjectPoolSection));
    GenericBuffer_AllocateVariable(&self->_availableSections, 0, sizeof(size_t));
    GenericBuffer_AllocateVariable(&self->_sectionsByAddress, 0, sizeof(size_t));
    self->_elementSize = elementSize;
    self->_sectionCapacity = sectionCapacity;
    self->_lifecycle = lifecycle;
    self->_userData = (userData != NULL) ? *userData : UserData_CreateEmpty();
    return Error_CreateSuccess();
}

Error ObjectPool_Construct1(ObjectPool* self, size_t elementSize, size_t sectionCapacity)
{
    return ObjectPool_Construct2(self,
        elementSize,
        sectionCapacity,
        (ObjectPoolLifecycle){ 0 },
        NULL);
}

Error ObjectPool_Deconstruct(ObjectPool* self)
{
    Error FirstError = Error_CreateSuccess();
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    for (size_t SectionIndex = 0; SectionIndex < GetSectionCount(self); SectionIndex++)
    {
        ObjectPoolSection* Section = GetSection(self, SectionIndex);
        for (size_t SlotIndex = 0; SlotIndex < Section->InitializedCount; SlotIndex++)
        {
            Result = DeconstructObjectIfNeeded(self, GetObjectAddress(self, Section, SlotIndex));

            // Best-effort teardown: keep the first error and deconstruct every later one so its
            // message is not leaked. Error_Deconstruct on a success error is a safe no-op.
            if (FirstError.Code == ErrorCode_Success)
            {
                FirstError = Result;
            }
            else
            {
                Error_Deconstruct(&Result);
            }
        }
        DeconstructSection(Section);
    }

    Memory_Free(self->_sections._data);
    Memory_Free(self->_availableSections._data);
    Memory_Free(self->_sectionsByAddress._data);
    Memory_Zero(self, sizeof(*self));
    return FirstError;
}

Error ObjectPool_GetNewObject(ObjectPool* self, void** outObject)
{
    size_t SectionIndex = 0;
    size_t SlotIndex = 0;
    ObjectPoolSection* Section = NULL;
    Error Result = ValidatePoolAndOutput(self, outObject);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outObject = NULL;
    Result = GetAvailableSection(self, &SectionIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Section = GetSection(self, SectionIndex);
    if (Section->FreeSlotCount > 0)
    {
        Section->FreeSlotCount--;
        SlotIndex = Section->FreeSlotStack[Section->FreeSlotCount];
    }
    else
    {
        SlotIndex = Section->InitializedCount;
        Result = ConstructObjectIfNeeded(self, GetObjectAddress(self, Section, SlotIndex));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Section->InitializedCount++;
    }

    SetSlotOccupied(Section, SlotIndex, true);
    Section->OccupiedCount++;
    if (!SectionHasAvailableObject(Section))
    {
        // The section came from the end of the list.
        GenericBuffer_RemoveLast(&self->_availableSections);
        Section->IsListedAvailable = false;
    }
    *outObject = GetObjectAddress(self, Section, SlotIndex);
    return Error_CreateSuccess();
}

Error ObjectPool_DisposeObject(ObjectPool* self, void* object)
{
    size_t SectionIndex = 0;
    size_t SlotIndex = 0;
    ObjectPoolSection* Section = NULL;
    unsigned char* ObjectBytes = object;
    Error Result = ValidatePool(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (object == NULL)
    {
        return CreateNullArgumentError(u8"object");
    }

    Result = TryFindSectionByObject(self, object, &SectionIndex, &SlotIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Section = GetSection(self, SectionIndex);
    if (!IsSlotOccupied(Section, SlotIndex))
    {
        return Error_CreateSuccess();
    }

    SetSlotOccupied(Section, SlotIndex, false);
    Result = ResetObjectForReuse(self, ObjectBytes);
    if (Result.Code != ErrorCode_Success)
    {
        SetSlotOccupied(Section, SlotIndex, true);
        return Result;
    }
    Section->FreeSlotStack[Section->FreeSlotCount] = SlotIndex;
    Section->FreeSlotCount++;
    Section->OccupiedCount--;
    ListSectionAsAvailable(self, SectionIndex);
    return Error_CreateSuccess();
}

Error ObjectPool_Clear(ObjectPool* self)
{
    Error Result = ValidatePool(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t SectionIndex = 0; SectionIndex < GetSectionCount(self); SectionIndex++)
    {
        Result = ResetSectionForReuse(self, GetSection(self, SectionIndex));
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
    }

    // Also after a failed reset: the sections reset before it have slots to hand out again.
    RebuildAvailableSections(self);
    return Result;
}
