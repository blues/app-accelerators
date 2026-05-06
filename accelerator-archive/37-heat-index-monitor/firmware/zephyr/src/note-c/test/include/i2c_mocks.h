/*!
 * @file i2c_mocks.h
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

#define I2C_MULTI_CHUNK_RECV_BYTES (ALLOC_CHUNK * 2)

const char *NoteI2CReceiveMultiChunk(uint16_t, uint8_t* pBuffer, uint16_t size,
                                     uint32_t *avail)
{
    static uint32_t left = I2C_MULTI_CHUNK_RECV_BYTES;

    assert(avail != NULL);

    if (size == 0) {
        *avail = left;
    } else {
        memset(pBuffer, 1, size);
        left -= size;
        *avail = left;
        if (left == 0) {
            pBuffer[size - 1] = '\n';
            left = I2C_MULTI_CHUNK_RECV_BYTES;
        }
    }

    return NULL;
}

const char *NoteI2CReceiveOne(uint16_t, uint8_t* pBuffer, uint16_t size,
                              uint32_t *avail)
{
    assert(avail != NULL);

    if (size == 0) {
        *avail = 1;
    } else {
        *avail = 0;
        pBuffer[0] = '\n';
    }

    return NULL;
}

const char *NoteI2CReceiveNothing(uint16_t, uint8_t*, uint16_t, uint32_t *avail)
{
    assert(avail != NULL);

    *avail = 0;

    return NULL;
}
