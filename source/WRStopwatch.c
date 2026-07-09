#if defined(__linux__)
#if !defined(_POSIX_C_SOURCE)
// clock_gettime and CLOCK_MONOTONIC require a POSIX feature-test level; 200809 covers them.
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "WRStopwatch.h"
#include "WRCompile.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <time.h>
#else
#error "WRStopwatch is only supported on Linux and Windows."
#endif


// Macros.
#define NANOSECONDS_PER_SECOND 1000000000ULL


// Public functions.
uint64_t Stopwatch_GetTimestampNanoseconds(void)
{
#if defined(_WIN32)
    LARGE_INTEGER Counter;
    LARGE_INTEGER Frequency;
    uint64_t Ticks;
    uint64_t TicksPerSecond;
    uint64_t WholeSeconds;
    uint64_t RemainderTicks;

    // On Windows XP and later these never fail, matching this module's infallible contract.
    QueryPerformanceCounter(&Counter);
    QueryPerformanceFrequency(&Frequency);
    Ticks = (uint64_t)Counter.QuadPart;
    TicksPerSecond = (uint64_t)Frequency.QuadPart;

    // Split into whole seconds plus a sub-second remainder before scaling to nanoseconds so that
    // neither multiply can wrap uint64: a naive Ticks * 1e9 would overflow within seconds of boot.
    WholeSeconds = Ticks / TicksPerSecond;
    RemainderTicks = Ticks % TicksPerSecond;
    return (WholeSeconds * NANOSECONDS_PER_SECOND)
        + ((RemainderTicks * NANOSECONDS_PER_SECOND) / TicksPerSecond);

#elif defined(__linux__)
    struct timespec RawTime;

    clock_gettime(CLOCK_MONOTONIC, &RawTime);
    // tv_sec/tv_nsec are already split into whole seconds and a sub-second remainder, so the same
    // overflow-safe form falls out directly with no frequency conversion.
    return ((uint64_t)RawTime.tv_sec * NANOSECONDS_PER_SECOND) + (uint64_t)RawTime.tv_nsec;
#endif
}

Stopwatch Stopwatch_StartNew(void)
{
    Stopwatch Result;
    Result._startTimestamp = Stopwatch_GetTimestampNanoseconds();
    return Result;
}

uint64_t Stopwatch_ElapsedNanoseconds(const Stopwatch* self)
{
    // The clock is monotonic, so the current reading is never below the stored start.
    return Stopwatch_GetTimestampNanoseconds() - self->_startTimestamp;
}

void Stopwatch_Restart(Stopwatch* self)
{
    self->_startTimestamp = Stopwatch_GetTimestampNanoseconds();
}

void Stopwatch_Deconstruct(Stopwatch* self)
{
    UNUSED(self);
}
