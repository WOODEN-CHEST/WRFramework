#if defined(__linux__)
#if !defined(_POSIX_C_SOURCE)
// clock_gettime, localtime_r and gmtime_r require a POSIX feature-test level; 200809 covers all three.
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "WRDateTime.h"
#include "WRCompile.h"

#if defined(__linux__)
#include <time.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error "WRDateTime is only supported on Linux and Windows."
#endif


// Macros.
#define NANOSECONDS_PER_MILLISECOND 1000000L
#define TM_YEAR_EPOCH 1900
#define YEAR_MIN_DIGITS 4
#define MONTH_DIGITS 2
#define DAY_DIGITS 2
#define HOUR_DIGITS 2
#define MINUTE_DIGITS 2
#define SECOND_DIGITS 2
#define MILLISECOND_DIGITS 3


// Static functions.
static DateTime GetCurrentTime(bool isUtc)
{
    DateTime Result;
    Result.Kind = isUtc ? DateTimeKind_Utc : DateTimeKind_Local;

#if defined(__linux__)
    struct timespec RawTime;
    struct tm BrokenTime;

    clock_gettime(CLOCK_REALTIME, &RawTime);
    if (isUtc)
    {
        gmtime_r(&RawTime.tv_sec, &BrokenTime);
    }
    else
    {
        localtime_r(&RawTime.tv_sec, &BrokenTime);
    }

    Result.Year = BrokenTime.tm_year + TM_YEAR_EPOCH;
    Result.Month = BrokenTime.tm_mon + 1;
    Result.Day = BrokenTime.tm_mday;
    Result.Hour = BrokenTime.tm_hour;
    Result.Minute = BrokenTime.tm_min;
    Result.Second = BrokenTime.tm_sec;
    Result.Millisecond = (int32_t)(RawTime.tv_nsec / NANOSECONDS_PER_MILLISECOND);
    // tm_wday and DayOfWeek both number Sunday as 0 through Saturday as 6.
    Result.WeekDay = (DayOfWeek)BrokenTime.tm_wday;

#elif defined(_WIN32)
    SYSTEMTIME SystemTime;

    if (isUtc)
    {
        GetSystemTime(&SystemTime);
    }
    else
    {
        GetLocalTime(&SystemTime);
    }

    Result.Year = SystemTime.wYear;
    Result.Month = SystemTime.wMonth;
    Result.Day = SystemTime.wDay;
    Result.Hour = SystemTime.wHour;
    Result.Minute = SystemTime.wMinute;
    Result.Second = SystemTime.wSecond;
    Result.Millisecond = SystemTime.wMilliseconds;
    // wDayOfWeek and DayOfWeek both number Sunday as 0 through Saturday as 6.
    Result.WeekDay = (DayOfWeek)SystemTime.wDayOfWeek;
#endif

    return Result;
}

static bool AppendPaddedNumber(GenericBuffer* buffer, int32_t value, int32_t minDigits)
{
    unsigned char Digits[16];
    int32_t DigitCount = 0;
    bool IsNegative = value < 0;
    // Widen before negating so INT32_MIN cannot overflow.
    int64_t Remaining = IsNegative ? -(int64_t)value : (int64_t)value;
    int32_t PadIndex = 0;
    int32_t DigitIndex = 0;

    do
    {
        Digits[DigitCount] = (unsigned char)(u8'0' + (Remaining % 10));
        Remaining /= 10;
        DigitCount++;
    } while ((Remaining > 0) && (DigitCount < (int32_t)sizeof(Digits)));

    if (IsNegative && !GenericBuffer_AppendByte(buffer, u8'-'))
    {
        return false;
    }

    for (PadIndex = DigitCount; PadIndex < minDigits; PadIndex++)
    {
        if (!GenericBuffer_AppendByte(buffer, u8'0'))
        {
            return false;
        }
    }

    for (DigitIndex = DigitCount - 1; DigitIndex >= 0; DigitIndex--)
    {
        if (!GenericBuffer_AppendByte(buffer, Digits[DigitIndex]))
        {
            return false;
        }
    }

    return true;
}


// Public functions.
DateTime DateTime_Now(void)
{
    return GetCurrentTime(false);
}

DateTime DateTime_UtcNow(void)
{
    return GetCurrentTime(true);
}

ComparisonResult DateTime_Compare(const DateTime* a, const DateTime* b)
{
    ComparisonResult Result;

    Result = Comparator_CompareInt32(a->Year, b->Year);
    if (Result != ComparisonResult_AEqualsB)
    {
        return Result;
    }

    Result = Comparator_CompareInt32(a->Month, b->Month);
    if (Result != ComparisonResult_AEqualsB)
    {
        return Result;
    }

    Result = Comparator_CompareInt32(a->Day, b->Day);
    if (Result != ComparisonResult_AEqualsB)
    {
        return Result;
    }

    Result = Comparator_CompareInt32(a->Hour, b->Hour);
    if (Result != ComparisonResult_AEqualsB)
    {
        return Result;
    }

    Result = Comparator_CompareInt32(a->Minute, b->Minute);
    if (Result != ComparisonResult_AEqualsB)
    {
        return Result;
    }

    Result = Comparator_CompareInt32(a->Second, b->Second);
    if (Result != ComparisonResult_AEqualsB)
    {
        return Result;
    }

    return Comparator_CompareInt32(a->Millisecond, b->Millisecond);
}

bool DateTime_Equals(const DateTime* a, const DateTime* b)
{
    return DateTime_Compare(a, b) == ComparisonResult_AEqualsB;
}

Error DateTime_ToString(const DateTime* self, GenericBuffer* buffer)
{
    unsigned char LastByte = 0;

    if ((self == NULL) || (buffer == NULL))
    {
        return Error_Construct1(ErrorCode_IllegalArgument,
            u8"DateTime_ToString requires non-null arguments.");
    }
    if (buffer->_elementSize != sizeof(unsigned char))
    {
        return Error_Construct1(ErrorCode_IllegalArgument,
            u8"DateTime_ToString requires a byte (element size 1) buffer.");
    }
    if (GenericBuffer_IsReadOnly(buffer))
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"DateTime_ToString cannot write to a read-only buffer.");
    }

    // Follow the growing-string convention: drop an existing trailing terminator before appending.
    if (GenericBuffer_GetLast(buffer, &LastByte) && (LastByte == 0))
    {
        GenericBuffer_RemoveLast(buffer);
    }

    if (!AppendPaddedNumber(buffer, self->Year, YEAR_MIN_DIGITS)
        || !GenericBuffer_AppendByte(buffer, u8'-')
        || !AppendPaddedNumber(buffer, self->Month, MONTH_DIGITS)
        || !GenericBuffer_AppendByte(buffer, u8'-')
        || !AppendPaddedNumber(buffer, self->Day, DAY_DIGITS)
        || !GenericBuffer_AppendByte(buffer, u8' ')
        || !AppendPaddedNumber(buffer, self->Hour, HOUR_DIGITS)
        || !GenericBuffer_AppendByte(buffer, u8':')
        || !AppendPaddedNumber(buffer, self->Minute, MINUTE_DIGITS)
        || !GenericBuffer_AppendByte(buffer, u8':')
        || !AppendPaddedNumber(buffer, self->Second, SECOND_DIGITS)
        || !GenericBuffer_AppendByte(buffer, u8'.')
        || !AppendPaddedNumber(buffer, self->Millisecond, MILLISECOND_DIGITS)
        || !GenericBuffer_NullTerminate(buffer))
    {
        return Error_Construct1(ErrorCode_InvalidOperation,
            u8"DateTime_ToString failed to write to the destination buffer.");
    }

    return Error_CreateSuccess();
}

void DateTime_Deconstruct(DateTime* self)
{
    UNUSED(self);
}
