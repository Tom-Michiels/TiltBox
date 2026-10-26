/* Host runner for generated TiltBox scripts (used by cloud/tools/replay.py and
   by hand): runs one script in the real MicroPython embed interpreter with a
   simulated, slowly tilting box for a time budget. Exit codes: 0 ran without
   exception (or hit the budget), 2 exception/traceback, 3 never called
   matrix_show. Prints a summary line starting with HOST_RESULT. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
extern void embed_run(const char *,uint32_t);
static unsigned shows;static uint8_t fb[64][3];static char tail[4096];static size_t tail_n;static int saw_traceback;
static uint32_t t0;
uint32_t hal_millis(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000+t.tv_nsec/1000000);}
void hal_delay_ms(uint32_t n){usleep(n*1000);}
void hal_serial_write(const char *s,size_t n){
    fwrite(s,1,n,stderr);
    if(n>=sizeof tail-1){memcpy(tail,s+n-(sizeof tail-1),sizeof tail-1);tail_n=sizeof tail-1;}
    else{if(tail_n+n>=sizeof tail){size_t drop=tail_n+n-(sizeof tail-1);memmove(tail,tail+drop,tail_n-drop);tail_n-=drop;}memcpy(tail+tail_n,s,n);tail_n+=n;}
    tail[tail_n]=0;if(strstr(tail,"Traceback"))saw_traceback=1;
}
int hal_abort_requested(void){return 0;}
void hal_matrix_set(int x,int y,uint8_t r,uint8_t g,uint8_t b){if(x>=0&&x<8&&y>=0&&y<8){fb[y*8+x][0]=r;fb[y*8+x][1]=g;fb[y*8+x][2]=b;}}
void hal_matrix_get(int x,int y,uint8_t *r,uint8_t *g,uint8_t *b){*r=fb[y*8+x][0];*g=fb[y*8+x][1];*b=fb[y*8+x][2];}
void hal_matrix_fill(uint8_t r,uint8_t g,uint8_t b){for(int i=0;i<64;i++){fb[i][0]=r;fb[i][1]=g;fb[i][2]=b;}}
void hal_matrix_show(void){shows++;}
void hal_matrix_brightness(uint8_t n){(void)n;}
void hal_matrix_bitmap(const uint8_t *a,uint8_t r,uint8_t g,uint8_t b){for(int y=0;y<8;y++)for(int x=0;x<8;x++)if(a[y]&(0x80>>x))hal_matrix_set(x,y,r,g,b);}
void hal_matrix_char(char a,uint8_t r,uint8_t g,uint8_t b){(void)a;hal_matrix_set(3,3,r,g,b);}
int hal_matrix_icon(const char *s,uint8_t r,uint8_t g,uint8_t b){(void)s;hal_matrix_set(3,3,r,g,b);return 1;}
void hal_matrix_text(const char *s,uint8_t r,uint8_t g,uint8_t b){(void)r;(void)g;(void)b;usleep(60000*(strlen(s)+1));shows++;}
/* Simulated motion: the box slowly circles its tilt, with an occasional shake. */
void hal_tilt(int *x,int *y,int *z){double t=(hal_millis()-t0)/1000.0;*x=(int)(90*sin(t*0.8));*y=(int)(90*cos(t*0.5));*z=(int)(240+8*sin(t*7));if(fmod(t,9.0)<0.4){*x+=(rand()%200)-100;*y+=(rand()%200)-100;}}
void hal_gyro(int *x,int *y,int *z){double t=(hal_millis()-t0)/1000.0;*x=(int)(40*cos(t*0.8));*y=(int)(-30*sin(t*0.5));*z=0;}
int main(int argc,char **argv){
    if(argc<2){fprintf(stderr,"usage: host_run script.py [budget_ms]\n");return 64;}
    FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 66;}
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char *code=malloc(n+1);if(fread(code,1,n,f)!=(size_t)n)return 66;code[n]=0;fclose(f);
    uint32_t budget=argc>2?(uint32_t)atoi(argv[2]):3000;t0=hal_millis();
    embed_run(code,budget);
    uint32_t ms=hal_millis()-t0;
    const char *status=saw_traceback?(strstr(tail,"KeyboardInterrupt")?"ok":"error"):"ok";
    if(!saw_traceback&&ms<budget-100)status="ended";   /* finished early: not an infinite loop */
    if(!shows)status=saw_traceback&&!strstr(tail,"KeyboardInterrupt")?"error":"no_show";
    printf("HOST_RESULT status=%s ms=%u shows=%u fps=%.1f\n",status,ms,shows,ms?shows*1000.0/ms:0);
    return !strcmp(status,"ok")||!strcmp(status,"ended")?0:!strcmp(status,"no_show")?3:2;
}
