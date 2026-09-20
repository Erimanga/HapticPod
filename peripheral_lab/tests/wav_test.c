#include "lab_wav.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t wav[] = {
#include "lab_wav_sample.inc"
};
static uint8_t edited[sizeof(wav) + 10];
static void put32(uint8_t *p, unsigned x) { for (unsigned i=0; i<4; ++i) p[i] = x >> (8*i); }
int main(void)
{
    const uint8_t *pcm;
    unsigned samples;
    assert(lab_wav_decode(wav, sizeof(wav), &pcm, &samples));
    assert(samples == 32000 && pcm == wav + 44);
    for (unsigned n=0; n<sizeof(wav); ++n)
        assert(!lab_wav_decode(wav, n, &pcm, &samples));
    memcpy(edited, wav, sizeof(wav)); edited[20] = 3;
    assert(!lab_wav_decode(edited, sizeof(wav), &pcm, &samples)); /* float unsupported */
    memcpy(edited, wav, sizeof(wav)); edited[22] = 2;
    assert(!lab_wav_decode(edited, sizeof(wav), &pcm, &samples)); /* stereo unsupported */
    memcpy(edited, wav, sizeof(wav)); edited[24] = 0;
    assert(!lab_wav_decode(edited, sizeof(wav), &pcm, &samples)); /* wrong sample rate */
    memcpy(edited, wav, sizeof(wav)); put32(edited + 40, 0xffffffff);
    assert(!lab_wav_decode(edited, sizeof(wav), &pcm, &samples)); /* oversized data */
    memcpy(edited, wav, 12);
    memcpy(edited+12, "JUNK", 4); put32(edited+16, 1);
    edited[20] = 42; edited[21] = 0;
    memcpy(edited+22, wav+12, sizeof(wav)-12);
    put32(edited+4, sizeof(edited)-8);
    assert(lab_wav_decode(edited, sizeof(edited), &pcm, &samples));
    assert(samples == 32000 && pcm == edited + 54); /* odd unknown chunk and padding */
    puts("PASS: actual embedded WAV, truncation, invalid format/rate/channels/length, unknown RIFF chunks and padding.");
}
