"""Host checks of actual board.c functions and low-power source contracts.

I2C and ADC are mocked. This does not test real ADC accuracy or sleep current.
"""
from pathlib import Path
import json, math, re, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/board.c').read_text()

def function(signature):
    start = source.index(signature)
    first = source.index('{', start)
    depth = 1
    end = first + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

init = function('esp_err_t tb_imu_init(void)')
sleep = function('void tb_sleep(bool low_battery)')
adc = function('int tb_battery_mv(void)')
tilt = function('esp_err_t tb_imu_read(int16_t *x,int16_t *y,int16_t *z)')
boot = function('void tb_init(void)')
assert 'wr(0x12,0x64)' in init and 'wr(0x12,0x64)' in sleep
assert 'wr(0x12,0x44)' not in source
assert sleep.index('wr(0x12,0x64)') < sleep.index('wr(0x10,0)')
assert 'wr(0x11,0)' in sleep  # Voice B gyro is enabled while awake, off in sleep.
assert 'uint64_t mask=1ULL<<TB_WAKE;' in sleep
assert 'rtc_gpio_set_direction(TB_WAKE,RTC_GPIO_MODE_INPUT_ONLY)' in sleep
assert 'rtc_gpio_pulldown_dis(TB_WAKE)' in sleep
assert 'rtc_gpio_pullup_en(TB_WAKE)' in sleep
assert 'rtc_gpio_hold_dis(TB_WAKE)' in boot
assert 'vTaskDelay(pdMS_TO_TICKS(400));' in boot
assert boot.index('pdMS_TO_TICKS(400)') < boot.index('tb_battery_mv()')

harness = r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND -2
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define ESP_LOGE(...) ((void)0)
#define ADC_CHANNEL_7 7
#define pdMS_TO_TICKS(x) (x)
static int adc,adc_cal;
/* ST LSM6DS3TR-C datasheet, WHO_AM_I section: fixed value 0x6a. */
static uint8_t regs[256],who=0x6a;
static int16_t accel[3];
static int read_error,adc_error,cal_error,samples;
static int wr(uint8_t r,uint8_t v){regs[r]=v;return 0;}
static int rd(uint8_t r,void *v,size_t n){
  if(r==0x0f){assert(n==1);*(uint8_t*)v=who;}
  else {assert(r==0x28&&n==6);for(int i=0;i<3;i++){((uint8_t*)v)[i*2]=(uint16_t)accel[i];((uint8_t*)v)[i*2+1]=(uint16_t)accel[i]>>8;}}
  return read_error;
}
static void vTaskDelay(int t){(void)t;}
static int adc_oneshot_read(int h,int ch,int *raw){assert(ch==7);*raw=1500;samples++;return adc_error;}
static int adc_cali_raw_to_voltage(int h,int raw,int *mv){*mv=raw;return cal_error;}
'''
harness += '\n' + init + '\n' + adc + '\n' + tilt
harness += r'''
int main(void){
  assert(tb_imu_init()==0);
  assert(regs[0x12]==0x64); /* BDU, H_LACTIVE, IF_INC; push-pull retained */
  assert(regs[0x10]==0x40 && regs[0x11]==0x4c); /* Voice B: gyro enabled, 104 Hz / 2000 dps */
  who=0x69;assert(tb_imu_init()==ESP_ERR_NOT_FOUND);
  who=0;assert(tb_imu_init()==ESP_ERR_NOT_FOUND);
  read_error=-7;assert(tb_imu_init()==-7);
  int16_t x,y,z;
  assert(tb_imu_read(&x,&y,&z)==-7);read_error=0;
  /* Physical poses captured while lowering the lit x=7 and y=7 edges. */
  accel[0]=-13071;accel[1]=-1638;accel[2]=-9800;
  assert(tb_imu_read(&x,&y,&z)==0 && x>180 && y>-40 && y<40 && z>0);
  accel[0]=-589;accel[1]=-13310;accel[2]=-9150;
  assert(tb_imu_read(&x,&y,&z)==0 && y>180 && x>-40 && x<40 && z>0);
  assert(tb_battery_mv()==3000 && samples==32);
  adc_error=-1;assert(tb_battery_mv()==-1);
  adc_error=0;cal_error=-1;assert(tb_battery_mv()==-1);
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tiltbox-low-power-') as d:
    code = Path(d)/'actual-functions.c'
    exe = Path(d)/'actual-functions'
    code.write_text(harness)
    subprocess.run(['cc','-std=c11','-O2',str(code),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

tau = (1e6/2)*100e-9
tau_margin = tau*1.01*1.2
assert 0.4 > 6*tau_margin
print(json.dumps({
    'result':'PASS', 'scope':'Compiled actual IMU init/tilt/ADC C functions with mocked I2C/ADC, physical pose regression fixtures, sleep/boot source contracts and RC arithmetic. Not live hardware tests.',
    'CTRL3_C': '0x64', 'interrupt_active_low': True, 'push_pull_retained': True,
    'wake_source': 'edge button only', 'adc_samples': 32, 'adc_divider_scale': 2,
    'divider_current_uA_at_4V2': {'before':4.2/400e3*1e6,'after':4.2/2e6*1e6},
    'R26_current_uA_before_nominal_3V3': 3.3/100e3*1e6,
    'R26_after': 'No intentional DC sink with idle-high INT1 after init and in sleep; not guaranteed zero leakage.',
    'tau_ms_nominal':tau*1000, 'boot_settling_ms':400,
    'tau_ms_with_1pct_R_20pct_C_margin':tau_margin*1000,
    'remaining_full_scale_settling_error_mV_at_4V2':4.2*math.exp(-.4/tau_margin)*1000,
    'physical_validation':False,
},indent=2))
