#ifndef NOTE_LOG_HPP
#define NOTE_LOG_HPP

#include <stddef.h>

class NoteLog
{
public:
    /**************************************************************************/
    /*!
        @brief  Type used to abstract specific hardware implementation types.
    */
    /**************************************************************************/
    typedef void * param_t;

    virtual ~NoteLog(void) {}

    /**************************************************************************/
    /*!
        @brief  Writes a message to the Notecard debug port.
        @param    message
                A null-terminated, message string.
        @return The number of bytes transmitted.
    */
    /**************************************************************************/
    virtual size_t print(const char * message) = 0;
};

/******************************************************************************/
/*!
    @brief  Helper function to abstract, create and maintain a single instance
    of the NoteLog interface implementation, as required by the underlying
    `note-c` library.
    @param[in] log_parameters
               Pointer to the parameters required to instantiate
               the platform specific log output implementation.
*/
/******************************************************************************/
NoteLog * make_note_log (
    NoteLog::param_t log_parameters
);

#endif // NOTE_LOG_HPP
