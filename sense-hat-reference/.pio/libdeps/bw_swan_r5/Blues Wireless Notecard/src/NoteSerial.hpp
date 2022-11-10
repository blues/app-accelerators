#ifndef NOTE_SERIAL_HPP
#define NOTE_SERIAL_HPP

#include <stddef.h>
#include <stdint.h>

class NoteSerial
{
public:
    /**************************************************************************/
    /*!
        @brief  Type used to abstract specific hardware implementation types.
    */
    /**************************************************************************/
    typedef void * param_t;

    virtual ~NoteSerial(void) {}

    /**************************************************************************/
    /*!
        @brief  Determines if the Notecard Serial port has data available.
        @return The number of bytes available to read.
    */
    /**************************************************************************/
    virtual size_t available(void) = 0;

    /**************************************************************************/
    /*!
        @brief  Read a byte from the Notecard Serial port.
        @return A single character byte.
    */
    /**************************************************************************/
    virtual char receive(void) = 0;

    /**************************************************************************/
    /*!
        @brief  Resets the serial port.
        @return `true` if the Serial port is available.
    */
    /**************************************************************************/
    virtual bool reset(void) = 0;

    /**************************************************************************/
    /*!
        @brief  Writes a message to the Notecard Serial port.
        @param    buffer
                  The bytes to write.
        @param    size
                  The number of bytes to write.
        @param    flush
                  Use `true` to flush to Serial.
        @return The number of bytes transmitted.
    */
    /**************************************************************************/
    virtual size_t transmit(uint8_t * buffer, size_t size, bool flush) = 0;
};

/******************************************************************************/
/*!
    @brief  Helper function to abstract, create and maintain a single instance
    of the NoteSerial interface implementation, as required by the underlying
    `note-c` library.
    @param[in] serial_parameters
               Pointer to the parameters required to instantiate
               the platform specific UART implementation.
*/
/******************************************************************************/
NoteSerial * make_note_serial (
    NoteSerial::param_t serial_parameters
);

#endif // NOTE_SERIAL_HPP
