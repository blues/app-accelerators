#ifndef NOTECARD_H
#define NOTECARD_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

void platform_delay(uint32_t ms);
uint32_t platform_millis(void);

const char *noteI2cReceive(uint16_t device_address_, uint8_t *buffer_, uint16_t size_, uint32_t *available_);
bool noteI2cReset(uint16_t device_address_);
const char *noteI2cTransmit(uint16_t device_address_, uint8_t *buffer_, uint16_t size_);

size_t noteLogPrint(const char *message_);

bool noteSerialAvailable(void);
char noteSerialReceive(void);
bool noteSerialReset(void);
void noteSerialTransmit(uint8_t *text_, size_t len_, bool flush_);

#endif // NOTECARD_H
