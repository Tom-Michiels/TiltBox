#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stddef.h>
extern void embed_run(const char *,uint32_t);
static unsigned shows;static uint8_t fb[64][3];
uint32_t hal_millis(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000+t.tv_nsec/1000000);}
void hal_delay_ms(uint32_t n){usleep(n*1000);}
void hal_serial_write(const char *s,size_t n){fwrite(s,1,n,stdout);}
int hal_abort_requested(void){return 0;}
void hal_matrix_set(int x,int y,uint8_t r,uint8_t g,uint8_t b){if(x>=0&&x<8&&y>=0&&y<8){fb[y*8+x][0]=r;fb[y*8+x][1]=g;fb[y*8+x][2]=b;}}
void hal_matrix_get(int x,int y,uint8_t *r,uint8_t *g,uint8_t *b){*r=fb[y*8+x][0];*g=fb[y*8+x][1];*b=fb[y*8+x][2];}
void hal_matrix_fill(uint8_t r,uint8_t g,uint8_t b){for(int i=0;i<64;i++){fb[i][0]=r;fb[i][1]=g;fb[i][2]=b;}}
void hal_matrix_show(void){shows++;}
void hal_matrix_brightness(uint8_t n){}
void hal_matrix_bitmap(const uint8_t *a,uint8_t r,uint8_t g,uint8_t b){}
void hal_matrix_char(char a,uint8_t r,uint8_t g,uint8_t b){}
int hal_matrix_icon(const char *s,uint8_t r,uint8_t g,uint8_t b){return 1;}
void hal_matrix_text(const char *s,uint8_t r,uint8_t g,uint8_t b){}
void hal_tilt(int *x,int *y,int *z){*x=10;*y=20;*z=256;}
void hal_gyro(int *x,int *y,int *z){*x=-3;*y=4;*z=0;}
int main(void){
    embed_run(
        "assert tilt() == (10,20,256)\nassert gyro() == (-3,4,0)\n"
        "for i in range(64):\n matrix_set(i%8,i//8,10,20,30)\nmatrix_show()\nassert matrix_get(5,2) == (10,20,30)\n"
        "assert 0 <= random.randint(0,7) <= 7\nassert abs(math.sin(1.0)-0.8414709848) < 1e-5\nassert abs(math.cos(2.5)+0.8011436155) < 1e-5\n"
        "assert abs(math.sqrt(2)-1.41421356) < 1e-6\nassert abs(math.atan2(1,1)-0.7853981634) < 1e-4\nassert abs(math.atan2(-1,-1)+2.3561944902) < 1e-4\n"
        "assert math.floor(-1.5) == -2 and math.ceil(1.2) == 2\nassert hsv(0,255,255) == (255,0,0) and hsv(85,255,255)[1] == 255\n"
        "assert tilt_dir() == '' and shake() < 40\nmatrix_clear(); matrix_number(42,255,255,255); assert matrix_get(0,1) == (255,255,255) and matrix_get(1,1) == (0,0,0)\n"
        "matrix_clear(); matrix_line(0,0,7,7,1,2,3); assert matrix_get(7,7) == (1,2,3) and matrix_get(3,3) == (1,2,3)\n"
        "matrix_clear(); matrix_rect(2,2,3,3,9,9,9); assert matrix_get(4,4) == (9,9,9) and matrix_get(5,5) == (0,0,0)\n"
        "for i in range(120):\n a=[j for j in range(12000)]\nassert a[999] == 999\nprint('API_AND_GC_PASS')\n"
        "try:\n open('forbidden')\n print('UNEXPECTED_FILESYSTEM')\nexcept NameError:\n print('NO_FILESYSTEM_PASS')\n"
        "try:\n __import__('machine')\n print('UNEXPECTED_MACHINE')\nexcept ImportError:\n print('NO_HARDWARE_MODULE_PASS')\n",3000);
    embed_run("while True:\n pass\n",50);puts("VM_INTERRUPT_RETURNED");
    embed_run("matrix_show()\nprint('FRESH_INTERPRETER_PASS')\n",500);
    printf("SHOWS=%u\n",shows);return shows==2?0:1;
}
