#ifndef NOTE_I2C_HPP
#define NOTE_I2C_HPP

#include <stddef.h>
#include <stdint.h>

class NoteI2c
{
public:
    /**************************************************************************/
    /*!
        @brief  Type used to abstract specific hardware implementation types.
    */
    /**************************************************************************/
    typedef void * param_t;

    virtual ~NoteI2c(void) {}

    /**************************************************************************/
    /*!
        @brief  Receives an amount of data from the Notecard in blocking mode.
        @param[in]  device_address
                The I2C address.
        @param[out] buffer
                A buffer to hold the data read from the I2C controller.
        @param[in]  requested_byte_count
                The number of bytes requested.
        @param[out] available
                The number of bytes available for subsequent calls to receive().
        @returns A string with an error, or `nullptr` if the receive was
        successful.
    */
    /**************************************************************************/
    virtual const char * receive(uint16_t device_address, uint8_t * buffer, uint16_t size, uint32_t * available) = 0;

    /**************************************************************************/
    /*!
        @brief  Resets the I2C port. Required by note-c.
        @return `true`.
    */
    /**************************************************************************/
    virtual bool reset(uint16_t device_address) = 0;

    /**************************************************************************/
    /*!
        @brief  Transmits an amount of data from the host in blocking mode.
        @param[in] device_address
                The I2C address.
        @param[in] buffer
                The data to transmit over I2C. The caller should have shifted
                it right so that the low bit is NOT the read/write bit.
        @param[in] size
                The number of bytes to transmit.
        @returns A string with an error, or `nullptr` if the transmission was
                successful.
    */
    /**************************************************************************/
    virtual const char * transmit(uint16_t device_address, uint8_t * buffer, uint16_t size) = 0;

    /**************************************************************************/
    /*!
        @brief  Size of the header for Serial-Over-I2C requests.

        @details The request made to the low-level I2C controller should be
                 for REQUEST_HEADER_SIZE + the `size` parameter supplied to the
                 `receive` method.

        @see NoteI2c::receive
    */
    /**************************************************************************/
    static const size_t REQUEST_HEADER_SIZE = 2;
};

/******************************************************************************/
/*!
    @brief  Helper function to abstract, create and maintain a single instance
    of the NoteI2c interface implementation, as required by the underlying
    `note-c` library.
    @param[in] i2c_parameters
               Pointer to the parameters required to instantiate
               the platform specific I2C implementation.
*/
/******************************************************************************/
NoteI2c * make_note_i2c (
    NoteI2c::param_t i2c_parameters
);

#endif // NOTE_I2C_HPP
