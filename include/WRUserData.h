#pragma once
#include <stddef.h>
#include <stdint.h>


/**
 * UserData is a fixed 128-byte value used wherever the framework lets a caller attach their
 * own data to a callback, a subscription, or a pooled object.
 *
 * It exists so the caller can choose how to pass their context:
 *   - Store a single pointer (UserData_FromPointer / UserData_GetPointer) when the real data
 *     lives elsewhere and must outlive the call.
 *   - Store a small value inline (write to .Bytes, or through a typed view) to avoid a separate
 *     allocation when the data is small enough to fit in the inline storage.
 *
 * Passing and lifetime rules (also documented in AGENTS.md):
 *   - Never pass UserData by value as a function argument. 128 bytes on the stack per call is
 *     wasteful. Functions that use the data immediately (predicates, comparators, mappers,
 *     extractors, and similar helpers) take a 'const UserData*' instead.
 *   - When the data must be retained past the call (event subscriptions, pooled objects, threads),
 *     the owning struct stores a UserData by value (a copy) and hands a pointer to that stored
 *     copy to the callback when it fires.
 *
 * UserData owns nothing. Copying it is a plain byte copy and there is no destructor. If a pointer
 * is stored inside it, ownership of the pointee is unchanged and is the caller's responsibility.
 */


#define USER_DATA_BYTE_COUNT ((size_t)128)


// Types.
typedef union UserDataUnion
{
    unsigned char Bytes[USER_DATA_BYTE_COUNT];
    void* Pointer;
    max_align_t _alignment; // Forces alignment suitable for any value stored inline; never read directly.
} UserData;


// Functions.

/* Returns a zero-initialized UserData (all bytes zero, pointer NULL). */
static inline UserData UserData_CreateEmpty(void)
{
    return (UserData){ 0 };
}

/* Returns a UserData that holds a single pointer; all remaining bytes are zero. */
static inline UserData UserData_FromPointer(void* pointer)
{
    UserData Data = { 0 };
    Data.Pointer = pointer;
    return Data;
}

/* Returns the pointer stored in the UserData. Only meaningful if the data was set via a pointer. */
static inline void* UserData_GetPointer(const UserData* self)
{
    return self->Pointer;
}
