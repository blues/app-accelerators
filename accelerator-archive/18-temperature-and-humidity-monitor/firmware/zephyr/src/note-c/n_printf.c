/*!
 * @file n_printf.c
 *
 * Written by Ray Ozzie and Blues Inc. team.
 *
 * Copyright (c) 2019 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/note-c/blob/master/LICENSE">LICENSE</a>
 * file.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "n_lib.h"

// Externalized Hooks
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's debug interface, if any.
*/
/**************************************************************************/
extern debugOutputFn hookDebugOutput;

//**************************************************************************/
/*!
  @brief  Write a formatted string to the debug output.
  @param   format  A format string for output.
  @param   ...  One or more values to interpolate into the format string.
*/
/**************************************************************************/
void NoteDebugf(const char *format, ...)
{
#ifndef NOTE_NODEBUG
    if (hookDebugOutput != NULL) {
        char line[256];
        va_list args;
        va_start(args, format);
        vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        hookDebugOutput(line);
    }
#endif
}

//**************************************************************************/
/*!
  @brief  Write a formatted string to the debug output.
  @param   format  A format string for output.
  @param   ...  One or more values to interpolate into the format string.
  @note.  Do NOT use this in a memory-constrained environment (vsnprintf is large)
*/
/**************************************************************************/
#ifndef NOTE_LOMEM
bool NotePrintf(const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    return NotePrint(line);
}
#endif
