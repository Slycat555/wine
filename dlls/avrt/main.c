/* Avrt dll implementation
 *
 * Copyright (C) 2009 Maarten Lankhorst
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "wine/debug.h"
#include "avrt.h"

WINE_DEFAULT_DEBUG_CHANNEL(avrt);

static inline WCHAR *strdupAW(const char *src)
{
    int len;
    WCHAR *dst;
    if (!src) return NULL;
    len = MultiByteToWideChar(CP_ACP, 0, src, -1, NULL, 0);
    if ((dst = malloc(len * sizeof(*dst)))) MultiByteToWideChar(CP_ACP, 0, src, -1, dst, len);
    return dst;
}

HANDLE WINAPI AvSetMmThreadCharacteristicsA(const char *name, DWORD *index)
{
    WCHAR *nameW = NULL;
    HANDLE ret;

    if (name && !(nameW = strdupAW(name)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }

    ret = AvSetMmThreadCharacteristicsW(nameW, index);

    free(nameW);
    return ret;
}

/* Multimedia Class Scheduler Service (MMCSS) tasks. On Windows every task runs above all
 * normal threads; the audio tasks highest. Wine maps Windows thread priorities to Unix
 * nice levels when RLIMIT_NICE allows it, so map each task to a thread priority. */
static const struct
{
    const WCHAR *name;
    int priority;
}
mm_tasks[] =
{
    { L"Pro Audio",      THREAD_PRIORITY_TIME_CRITICAL },
    { L"Audio",          THREAD_PRIORITY_TIME_CRITICAL },
    { L"Capture",        THREAD_PRIORITY_TIME_CRITICAL },
    { L"Playback",       THREAD_PRIORITY_TIME_CRITICAL },
    { L"Games",          THREAD_PRIORITY_HIGHEST },
    { L"Window Manager", THREAD_PRIORITY_HIGHEST },
    { L"Distribution",   THREAD_PRIORITY_ABOVE_NORMAL },
};

struct mm_task
{
    HANDLE thread;
    int old_priority;
    int task_priority;
};

static int find_mm_task(const WCHAR *name)
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(mm_tasks); i++)
        if (!wcsicmp(name, mm_tasks[i].name)) return i;
    return -1;
}

static HANDLE set_mm_task(int task, DWORD *index)
{
    struct mm_task *mm;

    if (!(mm = malloc(sizeof(*mm))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &mm->thread,
                         THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION, FALSE, 0))
    {
        free(mm);
        return NULL;
    }
    mm->old_priority = GetThreadPriority(mm->thread);
    mm->task_priority = mm_tasks[task].priority;
    if (!SetThreadPriority(mm->thread, mm->task_priority))
        WARN("Failed to raise thread priority, error %lu.\n", GetLastError());
    if (!*index) *index = 1;

    TRACE("task %s, thread priority %d -> %d, handle %p\n", debugstr_w(mm_tasks[task].name),
          mm->old_priority, mm->task_priority, mm);
    return mm;
}

HANDLE WINAPI AvSetMmThreadCharacteristicsW(const WCHAR *name, DWORD *index)
{
    int task;

    TRACE("(%s,%p)\n", debugstr_w(name), index);

    if (!name || (task = find_mm_task(name)) < 0)
    {
        SetLastError(ERROR_INVALID_TASK_NAME);
        return NULL;
    }

    if (!index)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }

    return set_mm_task(task, index);
}

BOOL WINAPI AvQuerySystemResponsiveness(HANDLE AvrtHandle, ULONG *value)
{
    TRACE("(%p, %p)\n", AvrtHandle, value);

    if (!AvrtHandle || !value)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *value = 20; /* Windows' default SystemResponsiveness */
    return TRUE;
}

BOOL WINAPI AvRevertMmThreadCharacteristics(HANDLE AvrtHandle)
{
    struct mm_task *mm = AvrtHandle;

    TRACE("(%p)\n", AvrtHandle);

    if (!mm)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    SetThreadPriority(mm->thread, mm->old_priority);
    CloseHandle(mm->thread);
    free(mm);
    return TRUE;
}

BOOL WINAPI AvSetMmThreadPriority(HANDLE AvrtHandle, AVRT_PRIORITY prio)
{
    struct mm_task *mm = AvrtHandle;
    int priority;

    TRACE("(%p)->(%d)\n", AvrtHandle, prio);

    if (!mm)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    switch (prio)
    {
    case AVRT_PRIORITY_CRITICAL:
    case AVRT_PRIORITY_HIGH:     priority = THREAD_PRIORITY_TIME_CRITICAL; break;
    case AVRT_PRIORITY_NORMAL:   priority = mm->task_priority; break;
    case AVRT_PRIORITY_LOW:      priority = THREAD_PRIORITY_ABOVE_NORMAL; break;
    case AVRT_PRIORITY_VERYLOW:  priority = THREAD_PRIORITY_NORMAL; break;
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SetThreadPriority(mm->thread, priority);
}

HANDLE WINAPI AvSetMmMaxThreadCharacteristicsA(const char *task1, const char *task2, DWORD *index)
{
    WCHAR *task1W = NULL, *task2W = NULL;
    HANDLE ret;

    if (task1 && !(task1W = strdupAW(task1)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }

    if (task2 && !(task2W = strdupAW(task2)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }

    ret = AvSetMmMaxThreadCharacteristicsW(task1W, task2W, index);

    free(task2W);
    free(task1W);
    return ret;
}

HANDLE WINAPI AvSetMmMaxThreadCharacteristicsW(const WCHAR *task1, const WCHAR *task2, DWORD *index)
{
    int t1, t2;

    TRACE("(%s,%s,%p)\n", debugstr_w(task1), debugstr_w(task2), index);

    if (!task1 || !task2 || (t1 = find_mm_task(task1)) < 0 || (t2 = find_mm_task(task2)) < 0)
    {
        SetLastError(ERROR_INVALID_TASK_NAME);
        return NULL;
    }

    if (!index)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }

    /* Use whichever task has the higher priority. */
    return set_mm_task(mm_tasks[t2].priority > mm_tasks[t1].priority ? t2 : t1, index);
}
