/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_PLATFORM_H
#define FRESH_PLATFORM_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#define PATH_LIST_SEP ';'
#define FRESH_ATTRIBUTE_EXECUTABLE 0u

#else

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#define PATH_LIST_SEP ':'

#define WINAPI
#define TRUE 1
#define FALSE 0
#define MAX_PATH 1024
#define INFINITE 0xFFFFFFFFu
#define STILL_ACTIVE 259
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

#define FILE_ATTRIBUTE_READONLY 0x1u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_ATTRIBUTE_TEMPORARY 0x100u
#define FRESH_ATTRIBUTE_EXECUTABLE 0x40000000u

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_APPEND_DATA 0x4u
#define FILE_WRITE_ATTRIBUTES 0x100u
#define FILE_SHARE_READ 0x1u
#define FILE_SHARE_WRITE 0x2u
#define FILE_SHARE_DELETE 0x4u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000u
#define FILE_BEGIN 0u
#define HANDLE_FLAG_INHERIT 0x1u
#define DUPLICATE_SAME_ACCESS 0x2u
#define MOVEFILE_REPLACE_EXISTING 0x1u
#define MOVEFILE_COPY_ALLOWED 0x2u
#define PROCESS_TERMINATE 0x1u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_DIR_NOT_EMPTY 145u
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
#define GetFileExInfoStandard 0

#define _O_BINARY 0
#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _isatty isatty
#define _fileno fileno
#define _dup dup
#define _dup2 dup2
#define _close close
#define _read read
#define _mkdir(path) mkdir((path), 0777)

static inline int _setmode(int fd, int mode) {
    (void)fd;
    (void)mode;
    return 0;
}
#define _popen popen
#define _pclose pclose
#define _lock_file flockfile
#define _unlock_file funlockfile
#define _fgetc_nolock getc_unlocked

typedef void *HANDLE;
typedef unsigned long DWORD;
typedef long LONG;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef void *LPVOID;
typedef unsigned long long ULONGLONG;

typedef struct {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct {
    unsigned short wYear;
    unsigned short wMonth;
    unsigned short wDayOfWeek;
    unsigned short wDay;
    unsigned short wHour;
    unsigned short wMinute;
    unsigned short wSecond;
    unsigned short wMilliseconds;
} SYSTEMTIME;

typedef union {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    long long QuadPart;
} LARGE_INTEGER;

typedef union {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    unsigned long long QuadPart;
} ULARGE_INTEGER;

typedef struct {
    DWORD nLength;
    void *lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    char cFileName[MAX_PATH];
} WIN32_FIND_DATAA;

typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);

DWORD GetLastError(void);

DWORD GetFileAttributesA(const char *path);
BOOL GetFileAttributesExA(const char *path, int level, WIN32_FILE_ATTRIBUTE_DATA *out);
BOOL SetFileAttributesA(const char *path, DWORD attributes);
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *data);
BOOL FindNextFileA(HANDLE find, WIN32_FIND_DATAA *data);
BOOL FindClose(HANDLE find);

HANDLE CreateFileA(const char *path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *sa,
                   DWORD disposition, DWORD flags, HANDLE template_file);
BOOL CloseHandle(HANDLE handle);
BOOL ReadFile(HANDLE handle, void *buffer, DWORD size, DWORD *read, void *overlapped);
BOOL WriteFile(HANDLE handle, const void *buffer, DWORD size, DWORD *written, void *overlapped);
DWORD SetFilePointer(HANDLE handle, LONG distance, LONG *high, DWORD method);
BOOL SetEndOfFile(HANDLE handle);
BOOL SetFileTime(HANDLE handle, const FILETIME *created, const FILETIME *accessed,
                 const FILETIME *written);
BOOL DeleteFileA(const char *path);
BOOL RemoveDirectoryA(const char *path);
BOOL CopyFileA(const char *source, const char *destination, BOOL fail_if_exists);
BOOL MoveFileExA(const char *source, const char *destination, DWORD flags);
BOOL CreateHardLinkA(const char *link, const char *target, SECURITY_ATTRIBUTES *sa);
BOOL CreateSymbolicLinkA(const char *link, const char *target, DWORD flags);

DWORD GetTempPathA(DWORD size, char *out);
UINT GetTempFileNameA(const char *directory, const char *prefix, UINT unique, char *out);
DWORD GetCurrentDirectoryA(DWORD size, char *out);
BOOL SetCurrentDirectoryA(const char *path);
DWORD GetFullPathNameA(const char *path, DWORD size, char *out, char **file_part);
DWORD GetModuleFileNameA(void *module, char *out, DWORD size);
BOOL GetComputerNameA(char *out, DWORD *size);

char *GetEnvironmentStringsA(void);
BOOL FreeEnvironmentStringsA(char *block);
BOOL SetEnvironmentVariableA(const char *name, const char *value);
int _putenv_s(const char *name, const char *value);

HANDLE GetStdHandle(DWORD which);
HANDLE GetCurrentProcess(void);
DWORD GetCurrentProcessId(void);
BOOL DuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, HANDLE *out,
                     DWORD access, BOOL inherit, DWORD options);
BOOL SetHandleInformation(HANDLE handle, DWORD mask, DWORD flags);
BOOL CreatePipe(HANDLE *read_end, HANDLE *write_end, SECURITY_ATTRIBUTES *sa, DWORD size);
int _open_osfhandle(intptr_t handle, int flags);
intptr_t _get_osfhandle(int fd);

HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);
BOOL TerminateProcess(HANDLE process, UINT code);
DWORD GetProcessId(HANDLE process);
BOOL GetExitCodeProcess(HANDLE process, DWORD *code);
DWORD WaitForSingleObject(HANDLE handle, DWORD timeout);
HANDLE CreateThread(SECURITY_ATTRIBUTES *sa, size_t stack, LPTHREAD_START_ROUTINE start,
                    LPVOID parameter, DWORD flags, DWORD *id);
LONG InterlockedExchange(volatile LONG *target, LONG value);
LONG InterlockedCompareExchange(volatile LONG *target, LONG exchange, LONG comparand);

DWORD GetTickCount(void);
void Sleep(DWORD milliseconds);
BOOL QueryPerformanceCounter(LARGE_INTEGER *out);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *out);
void GetSystemTimeAsFileTime(FILETIME *out);
BOOL FileTimeToLocalFileTime(const FILETIME *utc, FILETIME *local);
BOOL FileTimeToSystemTime(const FILETIME *time, SYSTEMTIME *out);
LONG CompareFileTime(const FILETIME *a, const FILETIME *b);

HANDLE fresh_process_handle(pid_t pid);
int fresh_handle_fd(HANDLE handle);

#endif

#endif
