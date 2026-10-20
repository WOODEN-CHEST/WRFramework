#include "WRArena.h"


// Macros.
// Buffers from Arena_CreateGenericBuffer grow geometrically, like heap-backed growable buffers.
#define ARENA_BUFFER_GROWTH_MULTIPLIER ((size_t)2)


// Types.
struct ArenaBlockStruct
{
    ArenaBlock* Next;
    size_t Capacity;
    // Starts a multiple of the default alignment past the block's start, so most allocations need no padding.
    alignas(max_align_t) unsigned char Data[];
};


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Arena argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateArgumentOutOfRangeError(const unsigned char* argumentName, const unsigned char* message)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Arena argument \"%s\" is out of range: %s.",
        argumentName,
        message);
}

static Error CreateArenaTooLargeError(void)
{
    return Error_Construct1(ErrorCode_BufferTooLarge,
        u8"The arena's initial capacity is too large to allocate.");
}

static bool IsAlignmentValid(size_t alignment)
{
    return ((alignment != 0) && ((alignment & (alignment - 1)) == 0));
}

// Bytes from the address up to the next multiple of the alignment, which must be a power of two.
static size_t GetAlignmentPadding(uintptr_t address, size_t alignment)
{
    return (size_t)((0U - address) & (alignment - 1));
}

static ArenaBlock* AllocateBlock(size_t capacity, size_t allocationSize)
{
    ArenaBlock* Block = Memory_Allocate(allocationSize);

    Block->Next = NULL;
    Block->Capacity = capacity;
    return Block;
}

// NULL when the block's allocation size does not fit in size_t.
static ArenaBlock* CreateBlock(size_t capacity)
{
    size_t AllocationSize = 0;

    if (!Memory_TryAddSize(sizeof(ArenaBlock), capacity, &AllocationSize))
    {
        return NULL;
    }

    return AllocateBlock(capacity, AllocationSize);
}

static void FreeBlocks(ArenaBlock* firstBlock)
{
    ArenaBlock* Block = firstBlock;

    while (Block != NULL)
    {
        ArenaBlock* NextBlock = Block->Next;

        Memory_Free(Block);
        Block = NextBlock;
    }
}

static void SetCurrentBlock(Arena* self, ArenaBlock* block, size_t blockStartPosition)
{
    self->_currentBlock = block;
    self->_blockData = (block != NULL) ? block->Data : NULL;
    self->_blockCapacity = (block != NULL) ? block->Capacity : 0;
    self->_blockStartPosition = blockStartPosition;
    self->_blockOffset = 0;
}

// The position only rises between clears and rewinds, so the peak is recorded just before it goes back.
static void RecordPeakUsedByteCount(Arena* self)
{
    self->_peakUsedByteCount = Arena_GetPeakUsedByteCount(self);
}

static bool DoesBlockFit(const ArenaBlock* block, size_t byteCount, size_t alignment)
{
    size_t Padding = GetAlignmentPadding((uintptr_t)block->Data, alignment);

    return ((Padding <= block->Capacity) && (byteCount <= (block->Capacity - Padding)));
}

// Doubles the arena's capacity within the block size limits, unless the request needs more.
static size_t GetNewBlockCapacity(const Arena* self, size_t requiredCapacity)
{
    size_t Capacity = self->_capacity;

    if (Capacity < ARENA_MINIMUM_BLOCK_BYTES)
    {
        Capacity = ARENA_MINIMUM_BLOCK_BYTES;
    }
    if (Capacity > ARENA_MAXIMUM_BLOCK_BYTES)
    {
        Capacity = ARENA_MAXIMUM_BLOCK_BYTES;
    }

    return (requiredCapacity > Capacity) ? requiredCapacity : Capacity;
}

// The new block goes right after the current one, ahead of any spares, so the blocks up to the current position keep
// their positions. NULL if growth is disabled or the block's size does not fit in size_t.
static ArenaBlock* AddBlockAfterCurrent(Arena* self, size_t byteCount, size_t alignment)
{
    size_t RequiredCapacity = 0;
    size_t NewCapacity = 0;

    if (!self->_isGrowthEnabled)
    {
        return NULL;
    }
    // Room for the worst-case padding, wherever the block's memory starts.
    if (!Memory_TryAddSize(byteCount, alignment - 1, &RequiredCapacity))
    {
        return NULL;
    }

    size_t BlockCapacity = GetNewBlockCapacity(self, RequiredCapacity);
    if (!Memory_TryAddSize(self->_capacity, BlockCapacity, &NewCapacity))
    {
        return NULL;
    }

    ArenaBlock* Block = CreateBlock(BlockCapacity);
    if (Block == NULL)
    {
        return NULL;
    }

    if (self->_currentBlock == NULL)
    {
        self->_firstBlock = Block;
    }
    else
    {
        Block->Next = self->_currentBlock->Next;
        self->_currentBlock->Next = Block;
    }

    self->_capacity = NewCapacity;
    return Block;
}

// Replaces the blocks with one block of their combined size, or leaves them as they are if its size does not fit in
// size_t.
static void MergeBlocks(Arena* self)
{
    size_t AllocationSize = 0;

    if (!Memory_TryAddSize(sizeof(ArenaBlock), self->_capacity, &AllocationSize))
    {
        return;
    }

    // Freed first, so the merged block can reuse their memory.
    FreeBlocks(self->_firstBlock);
    self->_firstBlock = AllocateBlock(self->_capacity, AllocationSize);
}

static bool TryExtendBufferInPlace(Arena* arena, GenericBuffer* buffer, size_t oldByteCount, size_t newByteCount)
{
    size_t AddedByteCount = newByteCount - oldByteCount;

    if (buffer->_data == NULL)
    {
        return false;
    }
    // Compared as integers, because pointer arithmetic on the arena's block pointer is undefined while it is NULL.
    if (((uintptr_t)buffer->_data + oldByteCount) != ((uintptr_t)arena->_blockData + arena->_blockOffset))
    {
        return false;
    }
    if (AddedByteCount > (arena->_blockCapacity - arena->_blockOffset))
    {
        return false;
    }

    arena->_blockOffset += AddedByteCount;
    return true;
}

// The growth callback of buffers from Arena_CreateGenericBuffer, whose callback context is their arena.
static bool GrowArenaBuffer(GenericBuffer* destination, size_t requestedCapacity)
{
    size_t NewCapacity = 0;

    if ((destination == NULL) || (destination->_userData == NULL))
    {
        return false;
    }
    if (!Memory_TryGrowCapacity(destination->_capacity,
        requestedCapacity,
        ARENA_BUFFER_GROWTH_MULTIPLIER,
        destination->_elementSize,
        &NewCapacity))
    {
        return false;
    }

    Arena* Owner = destination->_userData;
    // Neither product overflows: the old storage was allocated, and Memory_TryGrowCapacity checked the new one.
    size_t OldByteCount = destination->_capacity * destination->_elementSize;
    size_t NewByteCount = NewCapacity * destination->_elementSize;
    if (TryExtendBufferInPlace(Owner, destination, OldByteCount, NewByteCount))
    {
        destination->_capacity = NewCapacity;
        return true;
    }

    unsigned char* NewData = Arena_Allocate(Owner, NewByteCount, ARENA_DEFAULT_ALIGNMENT);
    if (NewData == NULL)
    {
        return false;
    }

    // The whole old capacity is copied, as a reallocation would, so bytes past the count (such as the NUL the number
    // formatters leave there) survive the growth.
    if (OldByteCount > 0)
    {
        Memory_Copy(destination->_data, NewData, OldByteCount);
    }
    destination->_data = NewData;
    destination->_capacity = NewCapacity;
    return true;
}


// Public functions.
Error Arena_Construct1(Arena* self)
{
    return Arena_Construct2(self, ArenaConstructOptions_CreateDefault());
}

Error Arena_Construct2(Arena* self, ArenaConstructOptions options)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!options.IsGrowthEnabled && (options.InitialCapacity == 0))
    {
        return CreateArgumentOutOfRangeError(u8"options.InitialCapacity",
            u8"must be greater than zero when growth is disabled");
    }

    Memory_Zero(self, sizeof(*self));
    self->_isGrowthEnabled = options.IsGrowthEnabled;
    if (options.InitialCapacity == 0)
    {
        return Error_CreateSuccess();
    }

    ArenaBlock* Block = CreateBlock(options.InitialCapacity);
    if (Block == NULL)
    {
        return CreateArenaTooLargeError();
    }

    self->_firstBlock = Block;
    self->_capacity = options.InitialCapacity;
    SetCurrentBlock(self, Block, 0);
    return Error_CreateSuccess();
}

void Arena_Deconstruct(Arena* self)
{
    if (self == NULL)
    {
        return;
    }

    FreeBlocks(self->_firstBlock);
    Memory_Zero(self, sizeof(*self));
}

void* Arena_AllocateFromNextBlock(Arena* self, size_t size, size_t alignment)
{
    if ((self == NULL) || !IsAlignmentValid(alignment))
    {
        return NULL;
    }

    size_t ByteCount = (size == 0) ? 1 : size;
    ArenaBlock* Block = (self->_currentBlock != NULL) ? self->_currentBlock->Next : NULL;
    if ((Block == NULL) || !DoesBlockFit(Block, ByteCount, alignment))
    {
        Block = AddBlockAfterCurrent(self, ByteCount, alignment);
        if (Block == NULL)
        {
            return NULL;
        }
    }

    // The rest of the current block counts as used until the next clear or rewind.
    size_t BlockStartPosition = (self->_currentBlock != NULL) ? (self->_blockStartPosition + self->_blockCapacity) : 0;
    SetCurrentBlock(self, Block, BlockStartPosition);

    size_t Padding = GetAlignmentPadding((uintptr_t)self->_blockData, alignment);
    self->_blockOffset = Padding + ByteCount;
    return self->_blockData + Padding;
}

bool Arena_RewindToMarker(Arena* self, ArenaMarker marker)
{
    if (self == NULL)
    {
        return false;
    }
    if ((marker._clearCount != self->_clearCount) || (marker._position > Arena_GetUsedByteCount(self)))
    {
        return false;
    }

    RecordPeakUsedByteCount(self);
    if (marker._position >= self->_blockStartPosition)
    {
        self->_blockOffset = marker._position - self->_blockStartPosition;
        return true;
    }

    // Positions count the blocks' bytes in chain order, and this one lies before the current block, so the walk stops
    // at an earlier block.
    ArenaBlock* Block = self->_firstBlock;
    size_t BlockStartPosition = 0;
    while ((marker._position - BlockStartPosition) > Block->Capacity)
    {
        BlockStartPosition += Block->Capacity;
        Block = Block->Next;
    }

    SetCurrentBlock(self, Block, BlockStartPosition);
    self->_blockOffset = marker._position - BlockStartPosition;
    return true;
}

void Arena_Clear(Arena* self)
{
    if (self == NULL)
    {
        return;
    }

    RecordPeakUsedByteCount(self);
    self->_clearCount++;
    if ((self->_firstBlock != NULL) && (self->_firstBlock->Next != NULL))
    {
        MergeBlocks(self);
    }

    SetCurrentBlock(self, self->_firstBlock, 0);
}

bool Arena_CreateGenericBuffer(Arena* self, GenericBuffer* buffer, size_t initialCapacity, size_t elementSize)
{
    void* Data = NULL;

    if ((self == NULL) || (buffer == NULL) || (elementSize == 0))
    {
        return false;
    }
    if (initialCapacity > 0)
    {
        Data = Arena_AllocateArray(self, initialCapacity, elementSize, ARENA_DEFAULT_ALIGNMENT);
    }

    GenericBuffer_CreateVariable(buffer,
        Data,
        (Data != NULL) ? initialCapacity : 0,
        elementSize,
        0,
        self,
        GrowArenaBuffer);
    return ((initialCapacity == 0) || (Data != NULL));
}
