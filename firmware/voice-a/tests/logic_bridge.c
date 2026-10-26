#include "voice_logic.h"
int index_at(int x,int y){return tb_pixel_index(x,y);}
void limit(const uint8_t *a,uint8_t *b,unsigned c,bool d){tb_limit_frame(a,b,c,d);}
void wav(uint8_t *p,unsigned n){tb_wav_header(p,n);}
unsigned prepare(int32_t *raw,size_t n,int16_t *out){return tb_audio_prepare(raw,n,out);}
unsigned crc(const uint8_t *p,size_t n){return tb_crc32(p,n);}
