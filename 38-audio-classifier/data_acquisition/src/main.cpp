#include <Arduino.h> // TODO: Needed?
#include <MEMSAudioCapture.h>

MEMSAudioCapture mic;

void printSamples(MemsAudio* audio, pcm_sample_t* samples, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        Serial.println(samples[i]);
    }
}

void setup(void)
{
    Serial.begin(115200);
    mic.begin(printSamples);
}

void loop()
{
}
