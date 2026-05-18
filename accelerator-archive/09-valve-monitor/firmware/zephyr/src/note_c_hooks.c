#include "note_c_hooks.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static const size_t REQUEST_HEADER_SIZE = 2;

const struct device *i2c_dev = NULL;
bool i2c1_initialized = false;

uint32_t platform_millis(void)
{
    return k_uptime_get_32();
}

void platform_delay(uint32_t ms)
{
    k_msleep(ms);
}

const char *noteI2cReceive(uint16_t device_address_, uint8_t *buffer_, uint16_t size_, uint32_t *available_)
{
    // Let the Notecard know that we are getting ready to read some data
    uint8_t size_buf[2];
    size_buf[0] = 0;
    size_buf[1] = (uint8_t)size_;
    uint8_t write_result = i2c_write(i2c_dev, size_buf, sizeof(size_buf), device_address_);

    if (write_result != 0)
    {
        return "i2c: Unable to initate read from the Notecard\n";
    }

    // Read from the Notecard and copy the response bytes into the response buffer
    const int request_length = size_ + REQUEST_HEADER_SIZE;
    uint8_t read_buf[256];
    uint8_t read_result = i2c_read(i2c_dev, read_buf, request_length, device_address_);

    if (read_result != 0)
    {
        return "i2c: Unable to receive data from the Notecard.\n";
    }
    else
    {
        *available_ = (uint32_t)read_buf[0];
        uint8_t bytes_to_read = read_buf[1];
        for (size_t i = 0; i < bytes_to_read; i++)
        {
            buffer_[i] = read_buf[i + 2];
        }

        return NULL;
    }
}

bool noteI2cReset(uint16_t device_address_)
{
    (void)device_address_;

    if (i2c1_initialized)
        return true;

    if (!i2c_dev)
    {
        i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
    }

    if (!device_is_ready(i2c_dev))
    {
        printk("i2c: Device is not ready.\n");

        return false;
    }

    printk("i2c: Device is ready.\n");

    i2c1_initialized = true;
    return true;
}

const char *noteI2cTransmit(uint16_t device_address_, uint8_t *buffer_, uint16_t size_)
{
    // Create a buffer that contains the number of bytes and the data to write to the Notecard
    uint8_t write_buf[size_ + 1];
    write_buf[0] = (uint8_t)size_;
    for (size_t i = 0; i < size_; i++)
    {
        write_buf[i + 1] = buffer_[i];
    }

    // Write the message
    uint8_t write_result = i2c_write(i2c_dev, write_buf, sizeof(write_buf), device_address_);

    if (write_result != 0)
    {
        return "i2c: Unable to transmit data to the Notecard\n";
    }
    else
    {
        return NULL;
    }
}

size_t noteLogPrint(const char *message_)
{
    if (message_)
    {
        printk("%s", message_);
        return 1;
    }

    return 0;
}

bool noteSerialAvailable(void)
{
    bool result = false;

    return result;
}

char noteSerialReceive(void)
{
    char result = 'z';

    return result;
}

bool noteSerialReset(void)
{
    bool result = false;

    return result;
}

void noteSerialTransmit(uint8_t *text_, size_t len_, bool flush_)
{
}
