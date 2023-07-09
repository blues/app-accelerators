#pragma once
#include <Stream.h>

#define DS1603L_READ_SUCCESS (0)
#define DS1603L_SENSOR_NOT_DETECTED (1)
#define DS1603L_READ_CHECKSUM_FAIL (2)
#define DS1603L_READ_NO_DATA (3)
#define DS1603L_SENSOR_TIMEOUT (12000)

/**
 * @brief Reads streamed data from a DS1603L ultrasound distance sensor.
 *
 * The stream read from the sensor comprises frames of 4 bytes:
 *    byte 0: marker, always 255
 *    byte 1: distance in millimeters, high-byte
 *    byte 2: distance in millimeters, low-byte
 *    byte 3: checksum - the sum of the first 3 bytes, modulo 256 (i.e. just the lower byte of the sum.)
 *
 * The implementation uses a 32-bit value to hold the 4 bytes, and appends data by shifting the
 * value left 8 bits. When a complete frame is read, the highest byte contains the marker,
 * and the lowest byte contains the checksum.
 *
 * If a valid reading is not received within the timeout period, DS1603L_SENSOR_NOT_DETECTED is returned.
 * Non-zero status values indicate an error.
 */
class DS1603L
{
public:
  /**
   * @brief Construct a new DS1603L object with the Stream to use to read data from the sensor,
   * and a timeout for when to consider the sensor offline.
   *
   * @param serial    A stream that provides the sensor data.
   * @param timeout   Timeout in milliseconds before DS1603L_SENSOR_NOT_DETECTED is returned.
   */
  DS1603L(Stream &serial, uint32_t timeout=DS1603L_SENSOR_TIMEOUT) : serial(serial), timeout(timeout) {}

  void begin()
  {
    status = DS1603L_SENSOR_NOT_DETECTED;
    lastReadingTime = 0;
    data = 0;
    distance = 0;
  }

  uint8_t readSensor(uint32_t now = millis())
  {
    status = DS1603L_READ_NO_DATA;
    while (serial.available())
    {
      uint8_t read = serial.read();
      data = (data << 8) | read;    // use `data` like a 4-byte circular buffer

      uint8_t marker = data >> 24;
      uint8_t dist_high = data >> 16;
      uint8_t dist_low = data >> 8;
      uint8_t expected_checksum = data;

      if (marker == 255)
      {
        uint8_t checksum = marker;
        checksum += dist_high;
        checksum += dist_low;
        if (checksum == expected_checksum)
        {
          distance = (data >> 8) & 0xFFFF;
          status = DS1603L_READ_SUCCESS;
          // a distance of 0 is given when the range cannot be determined. Report it back,
          // but don't count it as a valid reading.
          if (distance) {
            lastReadingTime = now;
          }
        }
        else
        {
          distance = 0;
          status = DS1603L_READ_CHECKSUM_FAIL;
        }
      }
    }
    if ((now - lastReadingTime) > DS1603L_SENSOR_TIMEOUT)
    {
      distance = 0;
      lastReadingTime = now;
      status = DS1603L_SENSOR_NOT_DETECTED;
    }
    return status;
  }

  uint8_t getStatus() const { return status; }
  uint16_t getDistance() const { return distance; }

private:
  Stream &serial;
  uint8_t status;
  uint32_t data;
  uint16_t distance;
  uint32_t lastReadingTime;
  uint32_t timeout;
};
