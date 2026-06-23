#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include <stddef.h>


typedef enum PathTypeEnum
{
    PathType_None,
    FilePathType_Absolute,
    FilePathType_Relative
} PathType;

typedef enum PathNormalizeConditionsEnum
{
    PathNormalizeConditions_None = 0,
    PathNormalizeConditions_FixSeparators = (1 << 0),
    PathNormalizeConditions_CollapseDirectorySegment = (1 << 1),
} PathNormalizeConditions;


Error Path_ChangeExtension(const unsigned char* path, const unsigned char* newExtension, GenericBuffer* result);

Error Path_RemoveExtension(const unsigned char* path, GenericBuffer* result);

Error Path_GetExtension(const unsigned char* path, GenericBuffer* result);

Error Path_HasExtension(const unsigned char* path, bool* hasExtension);

Error Path_Combine(const unsigned char** paths, size_t pathCount, GenericBuffer* result);

Error Path_Append(const unsigned char* pathA, const unsigned char* pathB, GenericBuffer* result);

bool Path_EndsInDirectorySeparator(const unsigned char* path);

Error Path_GetParentPath(const unsigned char* path, GenericBuffer* result);

Error Path_GetLastEntryName(const unsigned char* path, GenericBuffer* result);

Error Path_GetLastEntryStem(const unsigned char* path, GenericBuffer* result);

bool Path_IsEntryNameValid(const unsigned char* entryName);

Error Path_ValidateEntryName(const unsigned char* entryName);

bool Path_IsValid(const unsigned char* path);

Error Path_Validate(const unsigned char* path);

PathType Path_GetPathType(const unsigned char* path);

Error Path_Normalize(const unsigned char* path, PathNormalizeConditions conditions, GenericBuffer* buffer);

bool Path_IsNormalized(const unsigned char* path, PathNormalizeConditions conditions);

bool Path_IsRooted(const unsigned char* path);

bool Path_IsFullyQualified(const unsigned char* path);

Error Path_GetRoot(const unsigned char* path, GenericBuffer* result);

bool Path_IsSubPath(const unsigned char* parentPath, const unsigned char* childPath);

Error Path_TrimTrailingSeparator(const unsigned char* path, GenericBuffer* result);

Error Path_EnsureTrailingSeparator(const unsigned char* path, GenericBuffer* result);

bool Path_ContainsDirectorySegments(const unsigned char* path);

/**
 * Splits a path into its segments. Each segment's bytes (null-terminated) are written to strBuffer,
 * and the byte offset of each segment's start within strBuffer is written to segmentIndexBuffer (a
 * size_t buffer). Offsets, rather than pointers, are returned so they survive any later growth of
 * strBuffer; recover a segment with (strBuffer->_data + offset).
 */
Error Path_Split(const unsigned char* path, GenericBuffer* strBuffer, GenericBuffer* segmentIndexBuffer);