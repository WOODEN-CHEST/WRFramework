#include "WRComparator.h"
#include "WRChar.h"


// Public functions.
ComparisonResult Comparator_CompareString(const unsigned char* a, const unsigned char* b)
{
    if (a == b)
    {
        return ComparisonResult_AEqualsB;
    }
    if (a == NULL)
    {
        return ComparisonResult_ALessThanB;
    }
    if (b == NULL)
    {
        return ComparisonResult_AGreaterThanB;
    }

    while ((*a != u8'\0') && (*b != u8'\0'))
    {
        CodePoint CodePointA = CharUTF8_GetCodePoint(a);
        CodePoint CodePointB = CharUTF8_GetCodePoint(b);

        // A malformed sequence on either side has no meaningful code point, so compare a single
        // raw byte instead. Advancing by one byte keeps the scan moving (the encoded length would
        // be 0 here) and preserves a total order over otherwise invalid input.
        if ((CodePointA == CODEPOINT_NONE) || (CodePointB == CODEPOINT_NONE))
        {
            if (*a != *b)
            {
                return COMPARE_NUMBER(*a, *b);
            }
            a++;
            b++;
            continue;
        }

        if (CodePointA != CodePointB)
        {
            return Comparator_CompareInt32(CodePointA, CodePointB);
        }

        a += CharUTF8_GetByteCountCodepoint(CodePointA);
        b += CharUTF8_GetByteCountCodepoint(CodePointB);
    }

    // The scan ended because at least one string reached its terminator. Whichever still has a
    // non-terminator byte is the longer string and therefore the greater one; equal terminators
    // mean the strings matched completely.
    return COMPARE_NUMBER(*a, *b);
}
