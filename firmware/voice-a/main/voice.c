/* TiltBox voice games: hold the button while the LEDs face down -> speak ->
 * release -> the TiltBox cloud Worker (OpenRouter) returns MicroPython that runs
 * in the embedded interpreter. Secrets live in NVS, configured over USB. */
#include "board.h"
#include "voice_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include "esp_partition.h"
#include "esp_mac.h"
#include "nvs.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "font5x7.h"

#define FIRMWARE_VERSION "voice-b-2026-10-09"
#define AUDIO_SECONDS_MAX 60
#define AUDIO_SAMPLES_MAX (TB_WAV_RATE*AUDIO_SECONDS_MAX)
#define AUDIO_SETTLE_SAMPLES (TB_WAV_RATE/8)      /* 125 ms: mic settling + button click */
#define AUDIO_MIN_SAMPLES (TB_WAV_RATE/2)
#define CODE_MAX 65536
#define HTTP_MAX 262144
#define OUTPUT_TAIL 1536
#define SCRIPT_MAGIC 0x59505442u /* "TBPY" */

static const char *TAG="voice";
static EventGroupHandle_t wifi_events;
static bool wifi_running,net_ready;
static atomic_bool run_saved=true,serial_abort;
static uint8_t pixels[192],python_brightness=180;
static uint32_t down_since;static bool armed;
static uint32_t vm_flipped_at,script_started;static bool vm_flip_abort;
static char output_tail[OUTPUT_TAIL];static size_t output_len;static SemaphoreHandle_t output_lock;
static char session_id[40];
static char *usb_script; /* RUNB64: handed from the USB task to the main loop */
/* capture */
static int32_t *capture_raw;static volatile size_t capture_n;static volatile bool capture_run;static TaskHandle_t capture_task_h;
/* network task started in parallel with recording */
static volatile int net_state; /* 0 idle, 1 connecting, 2 ok, 3 failed */
static volatile bool net_cancel;
static SemaphoreHandle_t http_lock;
/* result reporting */
static volatile bool script_alive;static TaskHandle_t report_task_h;

extern void embed_run(const char *,uint32_t);
uint32_t hal_millis(void){return (uint32_t)(esp_timer_get_time()/1000);}
void hal_delay_ms(uint32_t ms){vTaskDelay(pdMS_TO_TICKS(ms?ms:1));}
void hal_serial_write(const char *s,size_t len){
    fwrite(s,1,len,stdout);
    if(!output_lock)return;
    xSemaphoreTake(output_lock,portMAX_DELAY);
    if(len>=OUTPUT_TAIL){memcpy(output_tail,s+len-(OUTPUT_TAIL-1),OUTPUT_TAIL-1);output_len=OUTPUT_TAIL-1;}
    else{if(output_len+len>=OUTPUT_TAIL){size_t drop=output_len+len-(OUTPUT_TAIL-1);memmove(output_tail,output_tail+drop,output_len-drop);output_len-=drop;}
        memcpy(output_tail+output_len,s,len);output_len+=len;}
    output_tail[output_len]=0;xSemaphoreGive(output_lock);
}
static void output_reset(void){if(!output_lock)return;xSemaphoreTake(output_lock,portMAX_DELAY);output_len=0;output_tail[0]=0;xSemaphoreGive(output_lock);}
void hal_matrix_set(int x,int y,uint8_t r,uint8_t g,uint8_t b){
    if(x<0||x>7||y<0||y>7)return;
    int n=tb_pixel_index(x,y);pixels[n*3]=g;pixels[n*3+1]=r;pixels[n*3+2]=b;
}
void hal_matrix_get(int x,int y,uint8_t *r,uint8_t *g,uint8_t *b){
    if(x<0||x>7||y<0||y>7){*r=*g=*b=0;return;}
    int n=tb_pixel_index(x,y);*g=pixels[n*3];*r=pixels[n*3+1];*b=pixels[n*3+2];
}
void hal_matrix_fill(uint8_t r,uint8_t g,uint8_t b){for(int y=0;y<8;y++)for(int x=0;x<8;x++)hal_matrix_set(x,y,r,g,b);}
void hal_matrix_brightness(uint8_t n){python_brightness=n;}
void hal_matrix_show(void){uint8_t out[192];for(int i=0;i<192;i++)out[i]=(unsigned)pixels[i]*python_brightness/255;tb_led_send(out);}
void hal_tilt(int *x,int *y,int *z){int16_t a=0,b=0,c=0;tb_imu_read(&a,&b,&c);*x=a;*y=b;*z=c;}
void hal_gyro(int *x,int *y,int *z){int16_t a=0,b=0,c=0;tb_gyro_read(&a,&b,&c);*x=a;*y=b;*z=c;}
int hal_abort_requested(void){
    static uint32_t last_check,last_yield,last_bat;
    uint32_t now=hal_millis();
    if(now-last_yield>=10){last_yield=now;hal_delay_ms(1);}
    if(serial_abort||tb_button_pressed())return 1;
    if(now-last_bat>5000){last_bat=now;int mv=tb_battery_mv();if(!gpio_get_level(TB_VBUS)&&mv>500&&mv<3200)tb_sleep(true);}
    if(now-last_check>=20){last_check=now;if(tb_face_down()){if(!vm_flipped_at)vm_flipped_at=now;}else vm_flipped_at=0;}
    vm_flip_abort=vm_flipped_at&&now-vm_flipped_at>=600;
    return vm_flip_abort;
}
#include "icons.inc"
static void indication(const char *icon,int r,int g,int b){hal_matrix_fill(0,0,0);hal_matrix_icon(icon,r,g,b);hal_matrix_show();}
static void dot(int x,int y,int r,int g,int b){hal_matrix_fill(0,0,0);hal_matrix_set(x,y,r,g,b);hal_matrix_show();}

/* ---------- NVS / script partition ---------- */
static bool nvs_string(nvs_handle_t h,const char *key,char *dst,size_t cap){size_t n=cap;return nvs_get_str(h,key,dst,&n)==ESP_OK;}
static bool nvs_get_cfg(const char *key,char *dst,size_t cap){nvs_handle_t h;if(nvs_open("voice",NVS_READONLY,&h)!=ESP_OK)return false;bool ok=nvs_string(h,key,dst,cap);nvs_close(h);return ok;}
static void nvs_set_cfg(const char *key,const char *value){nvs_handle_t h;if(nvs_open("voice",NVS_READWRITE,&h)==ESP_OK){if(value)nvs_set_str(h,key,value);else nvs_erase_key(h,key);nvs_commit(h);nvs_close(h);}}
static void script_running_flag(uint8_t value){nvs_handle_t h;if(nvs_open("voice",NVS_READWRITE,&h)==ESP_OK){nvs_set_u8(h,"running",value);nvs_commit(h);nvs_close(h);}}
static bool credentials_ready(void){char b[200];return nvs_get_cfg("ssid",b,sizeof b)&&strlen(b)>0&&nvs_get_cfg("url",b,sizeof b)&&strlen(b)>8&&nvs_get_cfg("key",b,sizeof b)&&strlen(b)>7;}
typedef struct{uint32_t magic,len,crc;char id[40];} script_hdr_t;
static const esp_partition_t *script_part(void){static const esp_partition_t *p;if(!p)p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"script");return p;}
static bool script_save(const char *code,const char *id){
    const esp_partition_t *p=script_part();size_t len=strlen(code);if(!p||len+sizeof(script_hdr_t)>p->size)return false;
    script_hdr_t h={SCRIPT_MAGIC,(uint32_t)len,tb_crc32((const uint8_t*)code,len),{0}};strlcpy(h.id,id?id:"",sizeof h.id);
    size_t total=(sizeof h+len+4095)&~4095u;
    return esp_partition_erase_range(p,0,total)==ESP_OK&&esp_partition_write(p,0,&h,sizeof h)==ESP_OK&&esp_partition_write(p,sizeof h,code,len)==ESP_OK;
}
static char *script_load(char *id_out,size_t id_cap){
    const esp_partition_t *p=script_part();script_hdr_t h;if(!p||esp_partition_read(p,0,&h,sizeof h)!=ESP_OK||h.magic!=SCRIPT_MAGIC||h.len==0||h.len>CODE_MAX)return NULL;
    char *code=heap_caps_malloc(h.len+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!code)return NULL;
    if(esp_partition_read(p,sizeof h,code,h.len)!=ESP_OK||tb_crc32((const uint8_t*)code,h.len)!=h.crc){free(code);return NULL;}
    code[h.len]=0;if(id_out){h.id[sizeof h.id-1]=0;strlcpy(id_out,h.id,id_cap);}return code;
}
static void script_clear(void){const esp_partition_t *p=script_part();if(p)esp_partition_erase_range(p,0,4096);}

/* ---------- Wi-Fi ---------- */
static void network_event(void *arg,esp_event_base_t base,int32_t id,void *data){
    (void)arg;(void)data;
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)xEventGroupSetBits(wifi_events,1);
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED)xEventGroupClearBits(wifi_events,1);
}
static void network_stop(void){
    net_cancel=true;for(int i=0;i<50&&net_state==1;i++)hal_delay_ms(20); /* let a connecting task bail out */
    if(wifi_running){esp_wifi_stop();wifi_running=false;}net_state=0;net_cancel=false;
}
static bool network_connect(void){
    if(!net_ready){
        ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&cfg));ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        wifi_events=xEventGroupCreate();ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,network_event,NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,network_event,NULL));ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));net_ready=true;
    }
    if(wifi_running&&(xEventGroupGetBits(wifi_events)&1))return true;
    char ssid[33]={0},password[65]={0};
    if(!nvs_get_cfg("ssid",ssid,sizeof ssid))return false;
    nvs_get_cfg("password",password,sizeof password);
    wifi_config_t cfg={0};memcpy(cfg.sta.ssid,ssid,strlen(ssid));memcpy(cfg.sta.password,password,strlen(password));cfg.sta.pmf_cfg.capable=true;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&cfg));memset(password,0,sizeof password);memset(&cfg,0,sizeof cfg);
    xEventGroupClearBits(wifi_events,1);ESP_ERROR_CHECK(esp_wifi_start());wifi_running=true;ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(60)); /* 15 dBm prototype cap, not a certification claim. */
    for(int attempt=0;attempt<3;attempt++){
        esp_wifi_connect();
        for(int i=0;i<100;i++){if(xEventGroupGetBits(wifi_events)&1)goto connected;if(serial_abort||net_cancel)return false;hal_delay_ms(100);}
    }
    return false;
connected:
    if(time(NULL)<1735689600){
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,"pool.ntp.org");esp_sntp_init();
        for(int i=0;i<200&&time(NULL)<1735689600;i++){if(serial_abort||net_cancel)break;hal_delay_ms(100);}
        esp_sntp_stop();
    }
    return time(NULL)>=1735689600;
}
static void network_task(void *arg){(void)arg;bool ok=network_connect();net_state=ok?2:3;vTaskDelete(NULL);}
static void network_start_async(void){if(net_state==1)return;net_cancel=false;net_state=1;if(xTaskCreatePinnedToCore(network_task,"net",8192,NULL,4,NULL,0)!=pdPASS)net_state=3;}
static bool network_wait(uint32_t ms){uint32_t t=hal_millis();while(net_state==1&&hal_millis()-t<ms){if(serial_abort)return false;hal_delay_ms(50);}return net_state==2;}

/* ---------- HTTP ---------- */
static bool http_write(esp_http_client_handle_t h,const void *data,size_t n){
    const char *p=data;
    while(n){if(serial_abort)return false;int count=esp_http_client_write(h,p,n>2048?2048:n);if(count<=0)return false;p+=count;n-=count;}
    return true;
}
static char *http_response(esp_http_client_handle_t h,int *status){
    if(esp_http_client_fetch_headers(h)<0)return NULL;
    *status=esp_http_client_get_status_code(h);
    char *buf=heap_caps_malloc(HTTP_MAX,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buf)return NULL;
    int used=0;
    uint32_t start=hal_millis();
    while(used<HTTP_MAX-1){
        if(hal_millis()-start>90000||serial_abort){free(buf);return NULL;}
        int n=esp_http_client_read(h,buf+used,HTTP_MAX-1-used);
        if(n<0){free(buf);return NULL;}
        if(!n){if(esp_http_client_is_complete_data_received(h))break;hal_delay_ms(10);continue;}used+=n;
    }
    if(used==HTTP_MAX-1){free(buf);return NULL;}buf[used]=0;return buf;
}
static esp_http_client_handle_t client(const char *path,const char *type){
    char base[200]={0},key[200]={0},url[260],auth[220];
    if(!nvs_get_cfg("url",base,sizeof base)||!nvs_get_cfg("key",key,sizeof key))return NULL;
    size_t bl=strlen(base);while(bl&&base[bl-1]=='/')base[--bl]=0;
    snprintf(url,sizeof url,"%s%s",base,path);
    bool https=!strncmp(url,"https://",8);
    if(!https)ESP_LOGW(TAG,"Plain HTTP endpoint: only for LAN testing against wrangler dev");
    esp_http_client_config_t cfg={.url=url,.method=HTTP_METHOD_POST,.timeout_ms=30000,.crt_bundle_attach=https?esp_crt_bundle_attach:NULL,.buffer_size=4096,.buffer_size_tx=4096,.disable_auto_redirect=true};
    esp_http_client_handle_t c=esp_http_client_init(&cfg);
    if(c){snprintf(auth,sizeof auth,"Bearer %s",key);esp_http_client_set_header(c,"Authorization",auth);esp_http_client_set_header(c,"Content-Type",type);
        esp_http_client_set_header(c,"X-TiltBox-Firmware",FIRMWARE_VERSION);
        static char dev[20];if(!dev[0]){uint8_t m[6];esp_read_mac(m,ESP_MAC_WIFI_STA);snprintf(dev,sizeof dev,"tb-%02x%02x%02x",m[3],m[4],m[5]);}
        esp_http_client_set_header(c,"X-TiltBox-Device",dev);
        char bat[16];snprintf(bat,sizeof bat,"%d",tb_battery_mv());esp_http_client_set_header(c,"X-TiltBox-Battery",bat);}
    memset(key,0,sizeof key);memset(auth,0,sizeof auth);return c;
}
/* Returns the response body (caller frees), NULL on transport failure. */
static char *http_post(const char *path,const char *type,const void *head,size_t head_n,const void *body,size_t body_n,int *status){
    *status=0;char *response=NULL;
    if(!http_lock||xSemaphoreTake(http_lock,pdMS_TO_TICKS(60000))!=pdTRUE)return NULL;
    esp_http_client_handle_t c=client(path,type);
    if(c){
        if(esp_http_client_open(c,head_n+body_n)==ESP_OK&&(!head_n||http_write(c,head,head_n))&&(!body_n||http_write(c,body,body_n)))response=http_response(c,status);
        esp_http_client_close(c);esp_http_client_cleanup(c);
    }
    xSemaphoreGive(http_lock);return response;
}
static void report_result(const char *id,const char *status,uint32_t ms){
    if(!id||!id[0])return;
    cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"id",id);cJSON_AddStringToObject(j,"status",status);cJSON_AddNumberToObject(j,"ms",ms);
    if(output_lock){xSemaphoreTake(output_lock,portMAX_DELAY);cJSON_AddStringToObject(j,"output",output_tail);xSemaphoreGive(output_lock);}
    char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return;
    int code=0;char *r=http_post("/v1/result","application/json",NULL,0,body,strlen(body),&code);
    ESP_LOGI(TAG,"Result '%s' for %s reported: HTTP %d",status,id,code);free(r);free(body);
}
static void flush_pending_result(void){
    char id[40],st[24],ms[16];
    if(nvs_get_cfg("pend_id",id,sizeof id)&&nvs_get_cfg("pend_st",st,sizeof st)){
        nvs_get_cfg("pend_ms",ms,sizeof ms);
        char out[OUTPUT_TAIL];if(nvs_get_cfg("pend_out",out,sizeof out)&&output_lock){xSemaphoreTake(output_lock,portMAX_DELAY);strlcpy(output_tail,out,OUTPUT_TAIL);output_len=strlen(output_tail);xSemaphoreGive(output_lock);}
        report_result(id,st,(uint32_t)atoi(ms));
        nvs_set_cfg("pend_id",NULL);nvs_set_cfg("pend_st",NULL);nvs_set_cfg("pend_ms",NULL);nvs_set_cfg("pend_out",NULL);
    }
}
static void store_pending_result(const char *id,const char *status,uint32_t ms){
    if(!id||!id[0]||!strcmp(id,"usb"))return;
    char b[16];snprintf(b,sizeof b,"%lu",(unsigned long)ms);
    nvs_set_cfg("pend_id",id);nvs_set_cfg("pend_st",status);nvs_set_cfg("pend_ms",b);
    if(output_lock){xSemaphoreTake(output_lock,portMAX_DELAY);char out[OUTPUT_TAIL];strlcpy(out,output_tail,sizeof out);xSemaphoreGive(output_lock);
        if(strlen(out)>1900)memmove(out,out+strlen(out)-1900,1901);
        nvs_set_cfg("pend_out",out);}
}
/* While a fresh script runs, Wi-Fi stays up for 4 s: an immediate crash is
   reported right away; otherwise "running" is reported and Wi-Fi goes off. */
static void report_task(void *arg){
    (void)arg;
    for(int i=0;i<40&&script_alive;i++)hal_delay_ms(100);
    if(script_alive){report_result(session_id,"running",hal_millis()-script_started);network_stop();}
    report_task_h=NULL;vTaskDelete(NULL);
}

/* ---------- script execution ---------- */
static const char *classify_output(void){
    if(!output_lock)return "ok";
    const char *o=output_tail;
    if(strstr(o,"KeyboardInterrupt"))return "aborted";
    if(strstr(o,"Traceback")||strstr(o,"Error:")||strstr(o,"Exception:"))return "error";
    return "ok";
}
/* Neutralise "import time/random/math" lines in place: time/random are
   prelude shims, math is built in. Other imports fail visibly on purpose. */
static void neutralise_imports(char *code){
    static const char *mods[]={"time","utime","random","urandom","math"};
    for(char *line=code;line&&*line;){
        char *next=strchr(line,'\n');char *p=line;while(*p==' '||*p=='\t')p++;
        const char *mod=NULL;
        if(!strncmp(p,"import ",7))mod=p+7;else if(!strncmp(p,"from ",5))mod=p+5;
        if(mod){size_t len=strcspn(mod," .\t\r\n");for(unsigned i=0;i<sizeof mods/sizeof mods[0];i++)if(len==strlen(mods[i])&&!strncmp(mod,mods[i],len)){*p='#';break;}}
        line=next?next+1:NULL;
    }
}
static void run_code(char *code,const char *id,bool report_live){ /* code is heap-owned by the caller and edited in place */
    neutralise_imports(code);
    vm_flipped_at=0;vm_flip_abort=false;serial_abort=false;output_reset();
    strlcpy(session_id,id?id:"",sizeof session_id);
    script_running_flag(1);script_started=hal_millis();script_alive=true;
    if(report_live&&id&&id[0])xTaskCreatePinnedToCore(report_task,"report",6144,NULL,3,&report_task_h,0);
    ESP_LOGI(TAG,"Running script %s (%u chars)",session_id[0]?session_id:"-",(unsigned)strlen(code));
    embed_run(code,0);
    script_alive=false;uint32_t ms=hal_millis()-script_started;
    script_running_flag(0);
    const char *status=classify_output();
    if(vm_flip_abort||tb_button_pressed())status="aborted";
    ESP_LOGI(TAG,"Script ended after %lu ms: %s",(unsigned long)ms,status);
    if(report_live&&id&&id[0]){
        while(report_task_h)hal_delay_ms(20); /* let the 4 s watcher finish */
        if(ms<4500&&net_state==2){report_result(id,status,ms);}
        else store_pending_result(id,status,ms);
    }else store_pending_result(id,status,ms);
    network_stop();
    if(!strcmp(status,"error")){indication("cross",80,10,0);hal_delay_ms(800);}
    if(vm_flip_abort){armed=false;down_since=0;}
}

/* ---------- audio capture ---------- */
static void capture_task(void *arg){
    (void)arg;i2s_chan_handle_t rx=tb_audio_channel();static int32_t frame[960]; /* 480 stereo frames */
    size_t skip=AUDIO_SETTLE_SAMPLES;
    while(capture_run&&capture_n<AUDIO_SAMPLES_MAX){
        size_t got=0;
        if(i2s_channel_read(rx,frame,sizeof frame,&got,200)!=ESP_OK)continue;
        size_t n=capture_n;
        for(size_t i=0;i+1<got/4&&n<AUDIO_SAMPLES_MAX;i+=2){if(skip){skip--;continue;}capture_raw[n++]=frame[i];}
        capture_n=n;
    }
    tb_audio_stop();capture_task_h=NULL;vTaskDelete(NULL);
}
static bool capture_start(void){
    if(!capture_raw)capture_raw=heap_caps_malloc(AUDIO_SAMPLES_MAX*sizeof(int32_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!capture_raw){ESP_LOGE(TAG,"Audio PSRAM unavailable");return false;}
    capture_n=0;capture_run=true;
    return xTaskCreatePinnedToCore(capture_task,"mic",4096,NULL,6,&capture_task_h,1)==pdPASS;
}
static size_t capture_stop(void){capture_run=false;while(capture_task_h)hal_delay_ms(10);return capture_n;}

/* ---------- the voice session ---------- */
static void fail(const char *why){ESP_LOGW(TAG,"%s",why);tb_led_power(true);indication("cross",80,10,0);hal_delay_ms(1200);}
static void session(void){
    if(!credentials_ready()){ESP_LOGW(TAG,"No cloud configuration: send CONFIG over USB (see provision.py).");tb_led_power(true);indication("cross",80,10,0);hal_delay_ms(600);while(tb_button_pressed())hal_delay_ms(20);return;}
    serial_abort=false;tb_led_power(false);
    network_start_async();
    if(!capture_start()){fail("Capture task failed");return;}
    ESP_LOGI(TAG,"Recording while the button is held (max %d s)",AUDIO_SECONDS_MAX);
    uint32_t t0=hal_millis(),released_at=0;
    while(hal_millis()-t0<(AUDIO_SECONDS_MAX+1)*1000UL&&capture_n<AUDIO_SAMPLES_MAX){
        if(serial_abort)break;
        if(!tb_button_pressed()){if(!released_at)released_at=hal_millis();if(hal_millis()-released_at>60)break;}else released_at=0;
        hal_delay_ms(10);
    }
    size_t n=capture_stop();
    ESP_LOGI(TAG,"Recorded %.1f s",(double)n/TB_WAV_RATE);
    tb_led_power(true);
    if(n<AUDIO_MIN_SAMPLES||serial_abort){dot(3,3,60,40,0);hal_delay_ms(300);network_stop();return;}
    dot(3,3,0,0,70); /* thinking */
    int16_t *pcm=(int16_t*)capture_raw;unsigned gain=tb_audio_prepare(capture_raw,n,pcm);
    ESP_LOGI(TAG,"Normalised, gain %u.%02ux",gain>>8,(gain&255)*100/256);
    if(!network_wait(20000)){fail("Wi-Fi or network time unavailable");network_stop();return;}
    flush_pending_result();
    uint8_t wav[44];tb_wav_header(wav,n*2);int status=0;
    dot(3,3,0,0,120);
    uint32_t up=hal_millis();
    char *response=http_post("/v1/game?lang=nl","audio/wav",wav,sizeof wav,pcm,n*2,&status);
    ESP_LOGI(TAG,"Cloud round trip %lu ms, HTTP %d",(unsigned long)(hal_millis()-up),status);
    if(!response){fail("Cloud request failed (network)");network_stop();return;}
    cJSON *j=cJSON_Parse(response);free(response);
    cJSON *id=cJSON_GetObjectItemCaseSensitive(j,"id"),*code=cJSON_GetObjectItemCaseSensitive(j,"code"),*text=cJSON_GetObjectItemCaseSensitive(j,"transcript"),*err=cJSON_GetObjectItemCaseSensitive(j,"error");
    if(cJSON_IsString(text))ESP_LOGI(TAG,"Transcript: %s",text->valuestring);
    if(cJSON_IsString(err))ESP_LOGW(TAG,"Cloud error: %s",err->valuestring);
    if(status!=200||!cJSON_IsString(code)||strlen(code->valuestring)<10||strlen(code->valuestring)>=CODE_MAX){cJSON_Delete(j);fail("No usable game returned");network_stop();return;}
    char *src=strdup(code->valuestring);char sid[40]={0};if(cJSON_IsString(id))strlcpy(sid,id->valuestring,sizeof sid);cJSON_Delete(j);
    if(!src){network_stop();return;}
    if(!script_save(src,sid))ESP_LOGW(TAG,"Script partition write failed; running without persistence");
    indication("check",0,70,20);hal_delay_ms(350);
    while(tb_button_pressed())hal_delay_ms(20);
    run_code(src,sid,true);
    free(src);serial_abort=false;
}

/* ---------- USB configuration ---------- */
static void config_task(void *arg){
    (void)arg;static char line[8192];puts("USB setup: CONFIG {\"ssid\",\"password\",\"url\",\"key\"} | CLOUD {\"url\",\"key\"} | STATUS | NEXT | CLEAR | RUNB64 <base64 script> | X. Input is never echoed.");
    while(1){
        if(!fgets(line,sizeof line,stdin)){clearerr(stdin);hal_delay_ms(50);continue;}
        if(!strncmp(line,"CONFIG ",7)){
            cJSON *j=cJSON_Parse(line+7),*s=cJSON_GetObjectItemCaseSensitive(j,"ssid"),*p=cJSON_GetObjectItemCaseSensitive(j,"password"),*u=cJSON_GetObjectItemCaseSensitive(j,"url"),*k=cJSON_GetObjectItemCaseSensitive(j,"key");
            if(cJSON_IsString(s)&&cJSON_IsString(p)&&cJSON_IsString(u)&&cJSON_IsString(k)&&strlen(s->valuestring)>0&&strlen(s->valuestring)<=32&&strlen(p->valuestring)<=64
               &&strlen(u->valuestring)>8&&strlen(u->valuestring)<190&&(!strncmp(u->valuestring,"https://",8)||!strncmp(u->valuestring,"http://",7))&&strlen(k->valuestring)>7&&strlen(k->valuestring)<190){
                nvs_handle_t h;
                if(nvs_open("voice",NVS_READWRITE,&h)==ESP_OK){
                    esp_err_t e=nvs_set_str(h,"ssid",s->valuestring);
                    if(e==ESP_OK)e=nvs_set_str(h,"password",p->valuestring);
                    if(e==ESP_OK)e=nvs_set_str(h,"url",u->valuestring);
                    if(e==ESP_OK)e=nvs_set_str(h,"key",k->valuestring);
                    if(e==ESP_OK)e=nvs_commit(h);
                    nvs_close(h);
                    puts(e==ESP_OK?"Configuration saved. Hold the button with the LEDs facing down to record.":"Configuration save failed.");
                }
            }else puts("Invalid configuration: need ssid (1-32), password (<=64), url (http(s)://..., <190), key (8-189).");
            cJSON_Delete(j);
        }else if(!strncmp(line,"CLOUD ",6)){ /* change only the Worker URL/token, keep Wi-Fi */
            cJSON *j=cJSON_Parse(line+6),*u=cJSON_GetObjectItemCaseSensitive(j,"url"),*k=cJSON_GetObjectItemCaseSensitive(j,"key");
            if(cJSON_IsString(u)&&cJSON_IsString(k)&&strlen(u->valuestring)>8&&strlen(u->valuestring)<190&&(!strncmp(u->valuestring,"https://",8)||!strncmp(u->valuestring,"http://",7))&&strlen(k->valuestring)>7&&strlen(k->valuestring)<190){
                nvs_set_cfg("url",u->valuestring);nvs_set_cfg("key",k->valuestring);puts("Cloud settings saved.");
            }else puts("Invalid cloud settings: need url (http(s)://..., <190) and key (8-189).");
            cJSON_Delete(j);
        }else if(!strncmp(line,"RUNB64 ",7)){ /* run a base64-encoded script from USB (testing without cloud) */
            size_t n=strlen(line+7);while(n&&(line[7+n-1]=='\n'||line[7+n-1]=='\r'))n--;
            char *code=heap_caps_malloc(n+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);size_t out=0;
            if(code&&mbedtls_base64_decode((unsigned char*)code,n+1,&out,(const unsigned char*)line+7,n)==0){code[out]=0;if(usb_script)free(usb_script);usb_script=code;puts("Script queued.");}
            else{puts("RUNB64: invalid base64.");free(code);}
        }else if(!strncmp(line,"CLEAR",5)){serial_abort=true;script_clear();run_saved=false;puts("Saved script cleared.");}
        else if(!strncmp(line,"NEXT",4)){serial_abort=true;tb_queue_command(CMD_NEXT);}
        else if(line[0]=='x'||line[0]=='X')serial_abort=true;
        else if(!strncmp(line,"STATUS",6)){char u[200]="(not set)";nvs_get_cfg("url",u,sizeof u);int16_t x,y,z,gx,gy,gz;tb_imu_read(&x,&y,&z);tb_gyro_read(&gx,&gy,&gz);
            printf("%s; VBAT %d mV; USB %d; tilt %d %d %d; gyro %d %d %d; cloud %s; configured %d; max %d s audio.\n",FIRMWARE_VERSION,tb_battery_mv(),gpio_get_level(TB_VBUS),x,y,z,gx,gy,gz,u,credentials_ready(),AUDIO_SECONDS_MAX);}
        memset(line,0,sizeof line);
    }
}
void tb_voice_start(void){
    output_lock=xSemaphoreCreateMutex();http_lock=xSemaphoreCreateMutex();
    /* Interrupt-driven USB console with a large RX buffer: the ROM path only has a
       64-byte FIFO, which loses bytes of CONFIG/RUNB64 lines sent in one burst.
       TX falls back to dropping after one short wait when no host is listening. */
    usb_serial_jtag_driver_config_t usb={.rx_buffer_size=8192,.tx_buffer_size=4096};
    if(usb_serial_jtag_driver_install(&usb)==ESP_OK)usb_serial_jtag_vfs_use_driver();
    nvs_handle_t h;uint8_t running=0;if(nvs_open("voice",NVS_READONLY,&h)==ESP_OK){nvs_get_u8(h,"running",&running);nvs_close(h);}
    if(running){run_saved=false;script_running_flag(0);ESP_LOGW(TAG,"Previous script reset unexpectedly; booting built-in games. CLEAR removes the saved script.");
        char id[40];char *c=script_load(id,sizeof id);if(c){store_pending_result(id,"crash",0);free(c);}}
    ESP_LOGI(TAG,"%s: hold the button with LEDs down to record; flip to switch games.",FIRMWARE_VERSION);
    assert(xTaskCreate(config_task,"usb_config",6144,NULL,3,NULL)==pdPASS);
}
void tb_voice_stop(void){serial_abort=true;capture_run=false;script_running_flag(0);tb_audio_stop();network_stop();}
void tb_voice_poll(void){
    if(run_saved){
        run_saved=false;char id[40]={0};char *code=script_load(id,sizeof id);
        if(code){run_code(code,id,false);free(code);}
    }
    if(usb_script){char *c=usb_script;usb_script=NULL;run_code(c,"usb",false);free(c);}
    bool down=tb_face_down();
    if(tb_button_pressed()&&down){session();return;}
    /* Flip gesture without button: next built-in game. */
    uint32_t now=hal_millis();
    if(!down){down_since=0;armed=true;}
    else if(armed){if(!down_since)down_since=now;if(now-down_since>=600){armed=false;down_since=0;tb_queue_command(CMD_NEXT);}}
    serial_abort=false;
}
