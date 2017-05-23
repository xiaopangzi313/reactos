/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     File support functions.
 * COPYRIGHT:   Copyright 2017-2018 Hermes Belusca-Maito
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/
NTSTATUS
ConcatPaths(
    IN OUT PWSTR PathElem1,
    IN SIZE_T cchPathSize,
    IN PCWSTR PathElem2 OPTIONAL)
{
    NTSTATUS Status;
    SIZE_T cchPathLen;

    if (!PathElem2)
        return STATUS_SUCCESS;
    if (cchPathSize <= 1)
        return STATUS_SUCCESS;

    cchPathLen = min(cchPathSize, wcslen(PathElem1));

    if (PathElem2[0] != L'\\' && cchPathLen > 0 && PathElem1[cchPathLen-1] != L'\\')
    {
        /* PathElem2 does not start with '\' and PathElem1 does not end with '\' */
        Status = RtlStringCchCatW(PathElem1, cchPathSize, L"\\");
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else if (PathElem2[0] == L'\\' && cchPathLen > 0 && PathElem1[cchPathLen-1] == L'\\')
    {
        /* PathElem2 starts with '\' and PathElem1 ends with '\' */
        while (*PathElem2 == L'\\')
            ++PathElem2; // Skip any backslash
    }
    Status = RtlStringCchCatW(PathElem1, cchPathSize, PathElem2);
    return Status;
}

//
// NOTE: It may be possible to merge both DoesPathExist and DoesFileExist...
//
BOOLEAN
DoesPathExist(
    IN HANDLE RootDirectory OPTIONAL,
    IN PCWSTR PathName)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, PathName);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               RootDirectory,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        FILE_LIST_DIRECTORY | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE);
    if (NT_SUCCESS(Status))
        NtClose(FileHandle);
    else
        DPRINT1("Failed to open directory %wZ, Status 0x%08lx\n", &Name, Status);

    return NT_SUCCESS(Status);
}

BOOLEAN
DoesFileExist(
    IN HANDLE RootDirectory OPTIONAL,
    IN PCWSTR PathName OPTIONAL,
    IN PCWSTR FileName)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;
    WCHAR FullName[MAX_PATH];

    if (PathName)
        RtlStringCchCopyW(FullName, ARRAYSIZE(FullName), PathName);
    else
        FullName[0] = UNICODE_NULL;

    if (FileName)
        ConcatPaths(FullName, ARRAYSIZE(FullName), FileName);

    RtlInitUnicodeString(&Name, FullName);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               RootDirectory,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (NT_SUCCESS(Status))
        NtClose(FileHandle);
    else
        DPRINT1("Failed to open file %wZ, Status 0x%08lx\n", &Name, Status);

    return NT_SUCCESS(Status);
}

/*
 * The format of NtPath should be:
 *    \Device\HarddiskXXX\PartitionYYY[\path] ,
 * where XXX and YYY respectively represent the hard disk and partition numbers,
 * and [\path] represent an optional path (separated by '\\').
 *
 * If a NT path of such a form is correctly parsed, the function returns respectively:
 * - in pDiskNumber: the hard disk number XXX,
 * - in pPartNumber: the partition number YYY,
 * - in PathComponent: pointer value (inside NtPath) to the beginning of \path.
 *
 * NOTE: The function does not accept leading whitespace.
 */
BOOLEAN
NtPathToDiskPartComponents(
    IN PCWSTR NtPath,
    OUT PULONG pDiskNumber,
    OUT PULONG pPartNumber,
    OUT PCWSTR* PathComponent OPTIONAL)
{
    ULONG DiskNumber, PartNumber;
    PCWSTR Path;

    *pDiskNumber = 0;
    *pPartNumber = 0;
    if (PathComponent) *PathComponent = NULL;

    Path = NtPath;

    if (_wcsnicmp(Path, L"\\Device\\Harddisk", 16) != 0)
    {
        /* The NT path doesn't start with the prefix string, thus it cannot be a hard disk device path */
        DPRINT1("'%S' : Not a possible hard disk device.\n", NtPath);
        return FALSE;
    }

    Path += 16;

    /* A number must be present now */
    if (!iswdigit(*Path))
    {
        DPRINT1("'%S' : expected a number! Not a regular hard disk device.\n", Path);
        return FALSE;
    }
    DiskNumber = wcstoul(Path, (PWSTR*)&Path, 10);

    /* Either NULL termination, or a path separator must be present now */
    if (*Path && *Path != OBJ_NAME_PATH_SEPARATOR)
    {
        DPRINT1("'%S' : expected a path separator!\n", Path);
        return FALSE;
    }

    if (!*Path)
    {
        DPRINT1("The path only specified a hard disk (and nothing else, like a partition...), so we stop there.\n");
        goto Quit;
    }

    /* Here, *Path == L'\\' */

    if (_wcsnicmp(Path, L"\\Partition", 10) != 0)
    {
        /* Actually, \Partition is optional so, if we don't have it, we still return success. Or should we? */
        DPRINT1("'%S' : unexpected format!\n", NtPath);
        goto Quit;
    }

    Path += 10;

    /* A number must be present now */
    if (!iswdigit(*Path))
    {
        /* If we don't have a number it means this part of path is actually not a partition specifier, so we shouldn't fail either. Or should we? */
        DPRINT1("'%S' : expected a number!\n", Path);
        goto Quit;
    }
    PartNumber = wcstoul(Path, (PWSTR*)&Path, 10);

    /* Either NULL termination, or a path separator must be present now */
    if (*Path && *Path != OBJ_NAME_PATH_SEPARATOR)
    {
        /* We shouldn't fail here because it just means this part of path is actually not a partition specifier. Or should we? */
        DPRINT1("'%S' : expected a path separator!\n", Path);
        goto Quit;
    }

    /* OK, here we really have a partition specifier: return its number */
    *pPartNumber = PartNumber;

Quit:
    /* Return the disk number */
    *pDiskNumber = DiskNumber;

    /* Return the path component also, if the user wants it */
    if (PathComponent) *PathComponent = Path;

    return TRUE;
}

NTSTATUS
OpenAndMapFile(
    IN HANDLE RootDirectory OPTIONAL,
    IN PCWSTR PathName OPTIONAL,
    IN PCWSTR FileName,             // OPTIONAL
    OUT PHANDLE FileHandle,         // IN OUT PHANDLE OPTIONAL
    OUT PHANDLE SectionHandle,
    OUT PVOID* BaseAddress,
    OUT PULONG FileSize OPTIONAL)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    SIZE_T ViewSize;
    PVOID ViewBase;
    UNICODE_STRING Name;
    WCHAR FullName[MAX_PATH];

    if (PathName)
        RtlStringCchCopyW(FullName, ARRAYSIZE(FullName), PathName);
    else
        FullName[0] = UNICODE_NULL;

    if (FileName)
        ConcatPaths(FullName, ARRAYSIZE(FullName), FileName);

    RtlInitUnicodeString(&Name, FullName);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               RootDirectory,
                               NULL);

    *FileHandle = NULL;
    *SectionHandle = NULL;

    Status = NtOpenFile(FileHandle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open file '%wZ', Status 0x%08lx\n", &Name, Status);
        return Status;
    }

    if (FileSize)
    {
        /* Query the file size */
        FILE_STANDARD_INFORMATION FileInfo;
        Status = NtQueryInformationFile(*FileHandle,
                                        &IoStatusBlock,
                                        &FileInfo,
                                        sizeof(FileInfo),
                                        FileStandardInformation);
        if (!NT_SUCCESS(Status))
        {
            DPRINT("NtQueryInformationFile() failed (Status %lx)\n", Status);
            NtClose(*FileHandle);
            *FileHandle = NULL;
            return Status;
        }

        if (FileInfo.EndOfFile.HighPart != 0)
            DPRINT1("WARNING!! The file '%wZ' is too large!\n", &Name);

        *FileSize = FileInfo.EndOfFile.LowPart;

        DPRINT("File size: %lu\n", *FileSize);
    }

    /* Map the file in memory */

    /* Create the section */
    Status = NtCreateSection(SectionHandle,
                             SECTION_MAP_READ,
                             NULL,
                             NULL,
                             PAGE_READONLY,
                             SEC_COMMIT /* | SEC_IMAGE (_NO_EXECUTE) */,
                             *FileHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create a memory section for file '%wZ', Status 0x%08lx\n", &Name, Status);
        NtClose(*FileHandle);
        *FileHandle = NULL;
        return Status;
    }

    /* Map the section */
    ViewSize = 0;
    ViewBase = NULL;
    Status = NtMapViewOfSection(*SectionHandle,
                                NtCurrentProcess(),
                                &ViewBase,
                                0, 0,
                                NULL,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_READONLY);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to map a view for file %wZ, Status 0x%08lx\n", &Name, Status);
        NtClose(*SectionHandle);
        *SectionHandle = NULL;
        NtClose(*FileHandle);
        *FileHandle = NULL;
        return Status;
    }

    *BaseAddress = ViewBase;
    return STATUS_SUCCESS;
}

BOOLEAN
UnMapFile(
    IN HANDLE SectionHandle,
    IN PVOID BaseAddress)
{
    NTSTATUS Status;
    BOOLEAN Success = TRUE;

    Status = NtUnmapViewOfSection(NtCurrentProcess(), BaseAddress);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("UnMapFile: NtUnmapViewOfSection(0x%p) failed with Status 0x%08lx\n",
                BaseAddress, Status);
        Success = FALSE;
    }
    Status = NtClose(SectionHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("UnMapFile: NtClose(0x%p) failed with Status 0x%08lx\n",
                SectionHandle, Status);
        Success = FALSE;
    }

    return Success;
}

/* EOF */
