// vorbis_bridge.c — Provides vorbis_decode_memory() for FluidSynth's SF3 loader.
// FluidSynth calls this function (via EXTERN_VORBIS_SUPPORT) to decode
// Vorbis-compressed samples in SF3 files.
// Including stb_vorbis.c without STB_VORBIS_HEADER_ONLY pulls in the full impl.

#include "stb/stb_vorbis.c"

#include <stdlib.h>

int vorbis_decode_memory(const unsigned char *mem, unsigned int len,
                         short **output, unsigned int *channels,
                         unsigned int *sample_rate)
{
    int ch = 0;
    int sr = 0;
    short *data = NULL;
    int samples = stb_vorbis_decode_memory(mem, (int)len, &ch, &sr, &data);
    if (samples < 0) {
        return -1;
    }
    if (output) *output = data;
    if (channels) *channels = (unsigned int)ch;
    if (sample_rate) *sample_rate = (unsigned int)sr;
    return samples * ch; // total number of shorts
}
