/*!
 * @file time_mocks.h
 *
 * Written by the Blues Inc. team.
 *
 * Copyright (c) 2023 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/note-c/blob/master/LICENSE">LICENSE</a>
 * file.
 *
 */

#pragma once

long unsigned int NoteGetMsIncrement(void)
{
    static long unsigned int count = 0;

    // increment by 1 second
    count += 1000;
    // return count pre-increment
    return count - 1000;
}
