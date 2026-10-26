#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* Pure helpers shared by the target firmware and host verification. */
static inline int tb_pixel_index(int x,int y){return (x<0||x>7||y<0||y>7)?-1:y*8+((y&1)?7-x:x);}
static inline void tb_limit_frame(const uint8_t in[192],uint8_t out[192],unsigned brightness,bool usb){
    unsigned sum=0;
    for(int i=0;i<192;i++)sum+=(unsigned)in[i]*brightness/255;
    unsigned max_sum=((usb?200U:400U)-64U)*255U/15U;
    for(int i=0;i<192;i++){unsigned v=(unsigned)in[i]*brightness/255;if(sum>max_sum)v=v*max_sum/sum;out[i]=v;}
}
static inline void tb_le16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static inline void tb_le32(uint8_t *p,unsigned n){tb_le16(p,n);tb_le16(p+2,n>>16);}
/* 44-byte canonical PCM16 mono WAV header; nbytes = data chunk length. */
#define TB_WAV_RATE 16000
static inline void tb_wav_header(uint8_t w[44],unsigned nbytes){
    const char riff[]="RIFF",wave[]="WAVEfmt ",data[]="data";
    for(int i=0;i<44;i++)w[i]=0;
    for(int i=0;i<4;i++){w[i]=riff[i];w[36+i]=data[i];}
    for(int i=0;i<8;i++)w[8+i]=wave[i];
    tb_le32(w+4,36+nbytes);tb_le32(w+16,16);tb_le16(w+20,1);tb_le16(w+22,1);
    tb_le32(w+24,TB_WAV_RATE);tb_le32(w+28,TB_WAV_RATE*2);tb_le16(w+32,2);tb_le16(w+34,16);
    tb_le32(w+40,nbytes);
}
/* Audio conditioning. The microphone delivers 24-bit samples in a 32-bit slot.
   tb_audio_prepare() converts raw I2S words in place to int16 PCM:
   1. 24-bit extraction, 2. one-pole DC high-pass (~20 Hz at 16 kHz),
   3. peak normalisation to -3 dBFS with a gain cap of 30 dB (32x), hard clip. */
typedef struct { int32_t prev_in, prev_out; } tb_hpf_t;
static inline int32_t tb_hpf_step(tb_hpf_t *f,int32_t in){
    /* y[n] = x[n] - x[n-1] + a*y[n-1], a = 1015/1024 */
    int32_t out=in-f->prev_in+(int32_t)(((int64_t)f->prev_out*1015)>>10);
    f->prev_in=in;f->prev_out=out;return out;
}
static inline int16_t tb_clip16(int32_t v){return v>32767?32767:v<-32768?-32768:(int16_t)v;}
/* raw: n 32-bit words (one per sample after channel selection). Returns gain in Q8. */
static inline unsigned tb_audio_prepare(int32_t *raw,size_t n,int16_t *out){
    tb_hpf_t f={n?raw[0]>>8:0,0};int32_t peak=0; /* start at the DC level: no step transient */
    for(size_t i=0;i<n;i++){int32_t s=tb_hpf_step(&f,raw[i]>>8);raw[i]=s;int32_t a=s<0?-s:s;if(a>peak)peak=a;}
    /* target peak 0.7 * 2^23 (24-bit); gain capped at 32x (30 dB), minimum 1x. */
    unsigned gain_q8=256;
    if(peak>0){int64_t g=((int64_t)5872025*256)/peak;if(g>32*256)g=32*256;if(g<256)g=256;gain_q8=(unsigned)g;}
    for(size_t i=0;i<n;i++){int64_t v=((int64_t)raw[i]*gain_q8)>>16;out[i]=tb_clip16((int32_t)v);}
    return gain_q8;
}
static inline uint32_t tb_crc32(const uint8_t *p,size_t n){
    uint32_t c=0xffffffffu;
    for(size_t i=0;i<n;i++){c^=p[i];for(int k=0;k<8;k++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}
    return ~c;
}
