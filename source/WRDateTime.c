#if defined(__linux__)
#if !defined(_POSIX_C_SOURCE)
// clock_gettime, localtime_r and gmtime_r require a POSIX feature-test level; 200809 covers all three.
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "WRDateTime.h"
#include "WRCompile.h"
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__linux__)
#error "WRDateTime is only supported on Linux and Windows."
#endif


// Macros.
#define NANOSECONDS_PER_MILLISECOND 1000000L
#define TM_YEAR_EPOCH 1900
#define SECONDS_PER_DAY 86400
#define SECONDS_PER_HOUR 3600
#define SECONDS_PER_MINUTE 60
#define DAYS_PER_WEEK 7
// 1970-01-01 (the Unix epoch) fell on a Thursday, which is index 4 in the Sunday-based DayOfWeek.
#define EPOCH_WEEKDAY 4
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


// Number of days from 1970-01-01 to the given proleptic-Gregorian date, and its inverse. Both use
// Howard Hinnant's public-domain civil-from/to-days algorithms; the constants are intrinsic to that
// method (146097 days per 400-year era, 719468 days between 0000-03-01 and 1970-01-01, etc.).
static int64_t DaysFromCivil(int64_t year, int32_t month, int32_t day)
{
    int64_t ShiftedYear = year - (month <= 2);
    int64_t Era = (ShiftedYear >= 0 ? ShiftedYear : ShiftedYear - 399) / 400;
    int64_t YearOfEra = ShiftedYear - (Era * 400);
    int64_t DayOfYear = ((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5) + day - 1;
    int64_t DayOfEra = (YearOfEra * 365) + (YearOfEra / 4) - (YearOfEra / 100) + DayOfYear;
    return (Era * 146097) + DayOfEra - 719468;
}

static void CivilFromDays(int64_t days, int32_t* outYear, int32_t* outMonth, int32_t* outDay)
{
    int64_t ShiftedDays = days + 719468;
    int64_t Era = (ShiftedDays >= 0 ? ShiftedDays : ShiftedDays - 146096) / 146097;
    int64_t DayOfEra = ShiftedDays - (Era * 146097);
    int64_t YearOfEra = (DayOfEra - (DayOfEra / 1460) + (DayOfEra / 36524) - (DayOfEra / 146096)) / 365;
    int64_t DayOfYear = DayOfEra - ((365 * YearOfEra) + (YearOfEra / 4) - (YearOfEra / 100));
    int64_t MonthIndex = ((5 * DayOfYear) + 2) / 153;
    int64_t Day = DayOfYear - (((153 * MonthIndex) + 2) / 5) + 1;
    int64_t Month = MonthIndex + (MonthIndex < 10 ? 3 : -9);
    int64_t Year = YearOfEra + (Era * 400) + (Month <= 2);

    *outYear = (int32_t)Year;
    *outMonth = (int32_t)Month;
    *outDay = (int32_t)Day;
}

static DateTime UtcDateTimeFromEpoch(int64_t unixSeconds)
{
    DateTime Result;
    int64_t Days = unixSeconds / SECONDS_PER_DAY;
    int64_t SecondsOfDay = unixSeconds % SECONDS_PER_DAY;
    int64_t Weekday = 0;

    // C truncates toward zero; shift to floor division so pre-epoch instants land on the right day.
    if (SecondsOfDay < 0)
    {
        SecondsOfDay += SECONDS_PER_DAY;
        Days -= 1;
    }

    CivilFromDays(Days, &Result.Year, &Result.Month, &Result.Day);
    Result.Hour = (int32_t)(SecondsOfDay / SECONDS_PER_HOUR);
    Result.Minute = (int32_t)((SecondsOfDay % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
    Result.Second = (int32_t)(SecondsOfDay % SECONDS_PER_MINUTE);
    Result.Millisecond = 0;

    Weekday = ((Days % DAYS_PER_WEEK) + EPOCH_WEEKDAY) % DAYS_PER_WEEK;
    if (Weekday < 0)
    {
        Weekday += DAYS_PER_WEEK;
    }
    Result.WeekDay = (DayOfWeek)Weekday;
    Result.Kind = DateTimeKind_Utc;
    return Result;
}

static Error LocalDateTimeFromEpoch(int64_t unixSeconds, DateTime* out)
{
    time_t Time = (time_t)unixSeconds;
    struct tm BrokenTime;

    if ((int64_t)Time != unixSeconds)
    {
        return Error_Construct1(ErrorCode_ArgumentOutOfRange,
            u8"Unix second is outside the range representable by the platform's time_t.");
    }

#if defined(__linux__)
    if (localtime_r(&Time, &BrokenTime) == NULL)
#elif defined(_WIN32)
    if (localtime_s(&BrokenTime, &Time) != 0)
#endif
    {
        return Error_Construct1(ErrorCode_ArgumentOutOfRange,
            u8"Unix second could not be converted to local time.");
    }

    out->Year = BrokenTime.tm_year + TM_YEAR_EPOCH;
    out->Month = BrokenTime.tm_mon + 1;
    out->Day = BrokenTime.tm_mday;
    out->Hour = BrokenTime.tm_hour;
    out->Minute = BrokenTime.tm_min;
    out->Second = BrokenTime.tm_sec;
    out->Millisecond = 0;
    out->WeekDay = (DayOfWeek)BrokenTime.tm_wday;
    out->Kind = DateTimeKind_Local;
    return Error_CreateSuccess();
}

static Error EpochFromLocalDateTime(const DateTime* self, int64_t* out)
{
    struct tm BrokenTime;
    time_t Result;

    Memory_Zero(&BrokenTime, sizeof(BrokenTime));
    BrokenTime.tm_year = self->Year - TM_YEAR_EPOCH;
    BrokenTime.tm_mon = self->Month - 1;
    BrokenTime.tm_mday = self->Day;
    BrokenTime.tm_hour = self->Hour;
    BrokenTime.tm_min = self->Minute;
    BrokenTime.tm_sec = self->Second;
    BrokenTime.tm_isdst = -1; // Let the library decide whether daylight saving is in effect.

    Result = mktime(&BrokenTime);
    if (Result == (time_t)-1)
    {
        return Error_Construct1(ErrorCode_ArgumentOutOfRange,
            u8"Local DateTime is outside the range representable as a Unix second.");
    }

    *out = (int64_t)Result;
    return Error_CreateSuccess();
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

Error DateTime_FromUnixSeconds(int64_t unixSeconds, DateTimeKind kind, DateTime* out)
{
    if (out == NULL)
    {
        return Error_Construct1(ErrorCode_IllegalArgument,
            u8"DateTime_FromUnixSeconds requires a non-null output.");
    }

    if (kind == DateTimeKind_Utc)
    {
        *out = UtcDateTimeFromEpoch(unixSeconds);
        return Error_CreateSuccess();
    }

    return LocalDateTimeFromEpoch(unixSeconds, out);
}

Error DateTime_ToUnixSeconds(const DateTime* self, int64_t* out)
{
    if ((self == NULL) || (out == NULL))
    {
        return Error_Construct1(ErrorCode_IllegalArgument,
            u8"DateTime_ToUnixSeconds requires non-null arguments.");
    }

    if (self->Kind == DateTimeKind_Local)
    {
        return EpochFromLocalDateTime(self, out);
    }

    *out = (DaysFromCivil(self->Year, self->Month, self->Day) * SECONDS_PER_DAY)
        + ((int64_t)self->Hour * SECONDS_PER_HOUR)
        + ((int64_t)self->Minute * SECONDS_PER_MINUTE)
        + (int64_t)self->Second;
    return Error_CreateSuccess();
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
