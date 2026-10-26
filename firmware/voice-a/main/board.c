#include "board.h"
#include "voice_logic.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/i2c_master.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_sleep.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG="board";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t imu;
static rmt_channel_handle_t led;
static rmt_encoder_handle_t encoder;
static rmt_symbol_word_t symbols[64*24];
static QueueHandle_t commands;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t adc_cal;
static i2s_chan_handle_t audio_rx;
static bool fault_latched;
static bool led_powered=true;
static unsigned brightness=180;

static esp_err_t wr(uint8_t reg,uint8_t value) {
    uint8_t p[2]={reg,value};return i2c_master_transmit(imu,p,2,100);
}
static esp_err_t rd(uint8_t reg,void *data,size_t len) {
    return i2c_master_transmit_receive(imu,&reg,1,data,len,100);
}
static void output(int pin,int level) {
    gpio_reset_pin(pin);gpio_set_direction(pin,GPIO_MODE_OUTPUT);gpio_set_level(pin,level);
}
void tb_init(void) {
    gpio_deep_sleep_hold_dis();
    rtc_gpio_hold_dis(TB_WAKE);
    const int gated[]={TB_BOOST_EN,TB_LED_EN,TB_MIC_EN,TB_LED_DATA,TB_MIC_WS,TB_MIC_BCLK};
    for (unsigned i=0;i<sizeof(gated)/sizeof(gated[0]);i++) {rtc_gpio_hold_dis(gated[i]);gpio_hold_dis(gated[i]);rtc_gpio_deinit(gated[i]);output(gated[i],0);}
    const int inputs[]={TB_WAKE,TB_IMU_INT,TB_LED_FAULT,TB_VBUS,TB_CHG_STAT1,TB_CHG_STAT2};
    for (unsigned i=0;i<sizeof(inputs)/sizeof(inputs[0]);i++) {rtc_gpio_deinit(inputs[i]);gpio_reset_pin(inputs[i]);gpio_set_direction(inputs[i],GPIO_MODE_INPUT);}
    commands=xQueueCreate(8,sizeof(int));assert(commands);
    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());e=nvs_flash_init();} ESP_ERROR_CHECK(e);
    i2c_master_bus_config_t bc={.i2c_port=I2C_NUM_0,.sda_io_num=TB_SDA,.scl_io_num=TB_SCL,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7};
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc,&bus));
    i2c_device_config_t dc={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x6a,.scl_speed_hz=400000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus,&dc,&imu));
    rmt_tx_channel_config_t rc={.gpio_num=TB_LED_DATA,.clk_src=RMT_CLK_SRC_DEFAULT,.resolution_hz=40000000,.mem_block_symbols=64,.trans_queue_depth=1};
    ESP_ERROR_CHECK(rmt_new_tx_channel(&rc,&led));rmt_copy_encoder_config_t ec={};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&ec,&encoder));ESP_ERROR_CHECK(rmt_enable(led));
    adc_oneshot_unit_init_cfg_t ac={.unit_id=ADC_UNIT_1};ESP_ERROR_CHECK(adc_oneshot_new_unit(&ac,&adc));
    adc_oneshot_chan_cfg_t cc={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};ESP_ERROR_CHECK(adc_oneshot_config_channel(adc,ADC_CHANNEL_7,&cc));
    adc_cali_curve_fitting_config_t cal={.unit_id=ADC_UNIT_1,.chan=ADC_CHANNEL_7,.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cal,&adc_cal));
    /* R20/R21 = 1 MOhm, C84 = 100 nF: tau = 50 ms nominal.
       Allow >6 tau with 1% R / 20% C margin before the first battery reading. */
    vTaskDelay(pdMS_TO_TICKS(400));
    gpio_set_level(TB_BOOST_EN,1);vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TB_LED_EN,1);vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t blank[192]={0};tb_led_send(blank);
    ESP_LOGI(TAG,"TiltBox Voice A: VBAT=%d mV, VBUS=%d",tb_battery_mv(),gpio_get_level(TB_VBUS));
}
int tb_battery_mv(void) {
    int total=0,raw,mv;
    for(int i=0;i<32;i++){if(adc_oneshot_read(adc,ADC_CHANNEL_7,&raw)!=ESP_OK)return -1;total+=raw;}
    if(adc_cali_raw_to_voltage(adc_cal,total/32,&mv)!=ESP_OK)return -1;
    return 2*mv; /* 1M/1M + 100 nF, calibrated ADC; verify on prototype */
}
void tb_set_brightness(int delta) {int b=(int)brightness+delta;brightness=b<20?20:b>255?255:b;}
void tb_led_send(const uint8_t *grb) {
    if(fault_latched||!led_powered)return;
    if(!gpio_get_level(TB_LED_FAULT)) {fault_latched=true;gpio_set_level(TB_LED_EN,0);gpio_set_level(TB_BOOST_EN,0);ESP_LOGE(TAG,"LED overcurrent latched: inspect board, then sleep/wake");return;}
    /* Conservative budget: 1 mA idle/LED + 15 mA/channel, versus 12 mA nominal.
       The independent TPS2553-1 remains the protection if firmware misbehaves. */
    uint8_t limited[192];tb_limit_frame(grb,limited,brightness,gpio_get_level(TB_VBUS));
    for(int i=0;i<192;i++) {
        unsigned v=limited[i];
        for(int bit=7;bit>=0;bit--) {rmt_symbol_word_t *s=&symbols[i*8+7-bit];bool one=v&(1U<<bit);
            s->level0=1;s->duration0=one?26:12;s->level1=0;s->duration1=one?24:38;}
    }
    rmt_transmit_config_t tc={.loop_count=0};ESP_ERROR_CHECK(rmt_transmit(led,encoder,symbols,sizeof(symbols),&tc));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led,100));esp_rom_delay_us(300);
}
esp_err_t tb_imu_init(void) {
    uint8_t who=0;esp_err_t e=rd(0x0f,&who,1);if(e!=ESP_OK)return e;
    /* The populated TR-C variant identifies as 0x6a (plain LSM6DS3: 0x69). */
    if(who!=0x6a){ESP_LOGE(TAG,"IMU ID 0x%02x, expected 0x6a",who);return ESP_ERR_NOT_FOUND;}
    ESP_ERROR_CHECK(wr(0x12,0x01));vTaskDelay(pdMS_TO_TICKS(15));
    /* BDU + H_LACTIVE + IF_INC. Unused push-pull INT1 idles high,
       so the external 100 kOhm R26 no longer sinks ~33 uA at 3.3 V. */
    ESP_ERROR_CHECK(wr(0x12,0x64));
    ESP_ERROR_CHECK(wr(0x11,0x4c)); /* gyro 104 Hz, +/-2000 dps: exposed to generated games */
    ESP_ERROR_CHECK(wr(0x10,0x40)); /* accelerometer 104 Hz, +/-2 g */
    vTaskDelay(pdMS_TO_TICKS(30));return ESP_OK;
}
esp_err_t tb_imu_read(int16_t *x,int16_t *y,int16_t *z) {
    uint8_t d[6];esp_err_t e=rd(0x28,d,6);if(e!=ESP_OK)return e;
    int16_t ax=(int16_t)((uint16_t)d[1]<<8|d[0]),ay=(int16_t)((uint16_t)d[3]<<8|d[2]),az=(int16_t)((uint16_t)d[5]<<8|d[4]);
    /* Measured on the JLCPCB prototype, 2026-10-02, relative to actual pixels:
       lowering x=7 edge -> ax < 0; lowering y=7 edge -> ay < 0;
       LEDs face-up -> az < 0. Return screen-space downhill tilt, ~256/g. */
    *x=-ax/64;*y=-ay/64;*z=-az/64;return ESP_OK;
}
esp_err_t tb_gyro_read(int16_t *x,int16_t *y,int16_t *z) {
    uint8_t d[6];esp_err_t e=rd(0x22,d,6);if(e!=ESP_OK)return e;
    int16_t gx=(int16_t)((uint16_t)d[1]<<8|d[0]),gy=(int16_t)((uint16_t)d[3]<<8|d[2]),gz=(int16_t)((uint16_t)d[5]<<8|d[4]);
    /* 70 mdps/LSB at +/-2000 dps; same sign convention as the accelerometer. */
    *x=(int16_t)(-(int32_t)gx*70/1000);*y=(int16_t)(-(int32_t)gy*70/1000);*z=(int16_t)(-(int32_t)gz*70/1000);return ESP_OK;
}
bool tb_face_down(void) {int16_t x,y,z;return tb_imu_read(&x,&y,&z)==ESP_OK&&z<-150;}
bool tb_button_pressed(void) {return !gpio_get_level(TB_WAKE);}
void tb_queue_command(int command) {if(commands)xQueueSend(commands,&command,0);}
int tb_poll_command(void) {
    static int debounce;static int64_t last_battery;
    /* Button: sleep when pressed upright; face-down presses are push-to-talk (voice.c). */
    debounce=gpio_get_level(TB_WAKE)?0:debounce+1;
    if(debounce==2&&!tb_face_down())tb_sleep(false);
    int64_t now=esp_timer_get_time();
    if(now-last_battery>5000000){last_battery=now;int mv=tb_battery_mv();
        if(!gpio_get_level(TB_VBUS)&&mv>500&&mv<3200){ESP_LOGW(TAG,"Low battery: %d mV",mv);tb_sleep(true);}}
    int command=0;xQueueReceive(commands,&command,0);return command;
}
i2s_chan_handle_t tb_audio_channel(void) {
    if(audio_rx)return audio_rx;
    gpio_set_level(TB_MIC_EN,1);vTaskDelay(pdMS_TO_TICKS(20));
    i2s_chan_config_t ch=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    ch.dma_desc_num=12;ch.dma_frame_num=480; /* ~360 ms of DMA slack while the capture task runs */ESP_ERROR_CHECK(i2s_new_channel(&ch,NULL,&audio_rx));
    i2s_std_config_t cfg={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=TB_MIC_BCLK,.ws=TB_MIC_WS,.dout=I2S_GPIO_UNUSED,.din=TB_MIC_SD}};
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(audio_rx,&cfg));ESP_ERROR_CHECK(i2s_channel_enable(audio_rx));return audio_rx;
}
void tb_audio_stop(void) {
    if(audio_rx){i2s_channel_disable(audio_rx);i2s_del_channel(audio_rx);audio_rx=NULL;}
    output(TB_MIC_BCLK,0);output(TB_MIC_WS,0);output(TB_MIC_EN,0);
}
void tb_led_power(bool on) {
    if(on && !fault_latched){gpio_set_level(TB_BOOST_EN,1);vTaskDelay(pdMS_TO_TICKS(10));gpio_set_level(TB_LED_EN,1);vTaskDelay(pdMS_TO_TICKS(5));led_powered=true;}
    else {gpio_set_level(TB_LED_EN,0);gpio_set_level(TB_BOOST_EN,0);led_powered=false;}
}
void tb_sleep(bool low_battery) {
    ESP_LOGI(TAG,"Sleeping; release button. Wake by edge button.");
    tb_voice_stop();if(audio_rx){i2s_channel_disable(audio_rx);i2s_del_channel(audio_rx);audio_rx=NULL;}
    rmt_disable(led);
    const int pins[]={TB_LED_DATA,TB_LED_EN,TB_BOOST_EN,TB_MIC_BCLK,TB_MIC_WS,TB_MIC_EN};
    for(unsigned i=0;i<sizeof(pins)/sizeof(pins[0]);i++){output(pins[i],0);rtc_gpio_init(pins[i]);rtc_gpio_set_direction(pins[i],RTC_GPIO_MODE_OUTPUT_ONLY);rtc_gpio_set_level(pins[i],0);rtc_gpio_hold_en(pins[i]);}
    /* Also enforce idle-high if sleep is entered before normal IMU setup. */
    ESP_ERROR_CHECK(wr(0x12,0x64));
    ESP_ERROR_CHECK(wr(0x11,0));ESP_ERROR_CHECK(wr(0x10,0));
    while(!gpio_get_level(TB_WAKE)){vTaskDelay(pdMS_TO_TICKS(20));}
    vTaskDelay(pdMS_TO_TICKS(100));
    /* EXT1 switches the pad to RTC control. Configure that domain explicitly;
       digital GPIO input/pull settings alone caused immediate prototype wake. */
    ESP_ERROR_CHECK(rtc_gpio_init(TB_WAKE));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(TB_WAKE,RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(TB_WAKE));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(TB_WAKE));
    uint64_t mask=1ULL<<TB_WAKE;
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(mask,ESP_EXT1_WAKEUP_ANY_LOW));
    esp_deep_sleep_start();
}
