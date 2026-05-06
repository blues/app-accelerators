#ifdef __cplusplus

#include "MEMS_Audio_ll_stm32l4.h"

#ifndef STM32L4
#error This library presently supports the STM32L4 chip family.
#endif

class MEMSAudioCapture
{
public:
    MEMSAudioCapture() {}

    ~MEMSAudioCapture() { end(); }

    bool begin(pcm_data_available_t pcm_handler)
    {
        return init(pcm_handler)==MEMS_AUDIO_OK;
    }

    bool end()
    {
        return mems_audio_uninit(&memsAudio)==MEMS_AUDIO_OK;
    }

private:
    mems_audio_err_t init(pcm_data_available_t pcm_handler) {
        memsAudio.pcmOutputBuffer = pcmBuffer;
        memsAudio.pcmOutputBufferLength = sizeof(pcmBuffer) / sizeof(*pcmBuffer);
        memsAudio.pcm_data_available = pcm_handler;
        memsAudioImpl.pdmBuffer = pdmBuffer;
        memsAudioImpl.pdmBufferLength = sizeof(pdmBuffer) / sizeof(*pdmBuffer);
        CHECK_MEMS_AUDIO_ERROR(mems_audio_init_stm32l4_sai_pdm(&memsAudio, &memsAudioImpl));
        CHECK_MEMS_AUDIO_ERROR(mems_audio_record(&memsAudio));
        return MEMS_AUDIO_OK;
    }

    MemsAudio memsAudio;
    MemsAudio_STM32L4SAIPDM memsAudioImpl;
    pdm_sample_t pdmBuffer[MEMS_AUDIO_PDM_BUFFER_LENGTH];
    pcm_sample_t pcmBuffer[MEMS_AUDIO_PCM_BUFFER_LENGTH];
};

#endif
