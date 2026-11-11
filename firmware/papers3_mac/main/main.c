#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mac.h"
#include "paper_epd.h"
#include "papers3_board.h"
#include "ui.h"

static mac_config_t config;
static uint8_t pram[MAC_PRAM_SIZE];
static uint8_t *capture, *mailbox, *frame;
static paper_presenter_t *presenter;
static SemaphoreHandle_t mailbox_lock, guest_lock;
static uint32_t sequence, last_sequence;
static atomic_bool guest_run, force_capture=true;
static atomic_uint frame_count;
static atomic_bool io_failed, halted;
static bool machine_ready, touch_ready, drag_lock;
static int mode=UI_TRACKPAD, held_key=-1;
static unsigned modifiers;
static char status_line[64]="Starting...";
static const uint8_t modifier_codes[]={0x38,0x3b,0x3a,0x37,0x39};
static paper_epd_speed_t speed=PAPER_SPEED_BALANCED;
static const char *NVS_NAMESPACE="paper_mac", *NVS_SPEED="speed";
static void redraw(void)
{
    ui_state_t s={.mode=mode,.status=status_line,.running=atomic_load(&guest_run),
        .drag=drag_lock,.modifiers=modifiers,.speed=speed};
    ui_draw(&s);ui_publish();
}
static void status(const char *text)
{
    snprintf(status_line,sizeof(status_line),"%s",text);
    ESP_LOGI("app","%s",status_line);
    redraw();
}
static void load_speed(void)
{
    nvs_handle_t h;uint8_t v;
    if(nvs_open(NVS_NAMESPACE,NVS_READONLY,&h)!=ESP_OK)return;
    if(nvs_get_u8(h,NVS_SPEED,&v)==ESP_OK && v<PAPER_SPEEDS)speed=(paper_epd_speed_t)v;
    nvs_close(h);
}
static void set_speed(paper_epd_speed_t s)
{
    speed=s;paper_epd_set_speed(s);
    nvs_handle_t h;
    if(nvs_open(NVS_NAMESPACE,NVS_READWRITE,&h)==ESP_OK){
        if(nvs_set_u8(h,NVS_SPEED,(uint8_t)s)==ESP_OK)nvs_commit(h);
        nvs_close(h);
    }
    static const char *names[PAPER_SPEEDS]={"Display: Fast","Display: Balanced","Display: Sharp"};
    status(names[s]);
}
static void save_pram(void)
{
    nvs_handle_t h;
    if(nvs_open("paper_mac",NVS_READWRITE,&h)==ESP_OK){
        if(nvs_set_blob(h,"pram",pram,sizeof(pram))==ESP_OK)nvs_commit(h);
        nvs_close(h);
    }
}
static void release_input(void)
{
    if(!machine_ready)return;
    if(held_key>=0)mac_key(held_key,false);
    held_key=-1;drag_lock=false;
    for(unsigned i=0;i<5;++i)if(modifiers&(1u<<i))mac_key(modifier_codes[i],false);
    modifiers=0;mac_mouse(0,0,false);
}
static bool pause_guest(void)
{
    release_input();atomic_store(&guest_run,false);
    if(xSemaphoreTake(guest_lock,pdMS_TO_TICKS(3000))!=pdTRUE){
        status("Pause timed out. Keep the SD card in.");return false;
    }
    xSemaphoreGive(guest_lock);return true;
}
static void guest_task(void *unused)
{
    (void)unused;
    uint32_t frames=0, signature=0;
    int64_t deadline=esp_timer_get_time();
    for(;;){
        xSemaphoreTake(guest_lock,portMAX_DELAY);
        if(!atomic_load(&guest_run)){
            xSemaphoreGive(guest_lock);vTaskDelay(1);deadline=esp_timer_get_time();continue;
        }
        mac_run_frame();++frames;atomic_store(&frame_count,frames);
        bool force=atomic_exchange(&force_capture,false);
        if(frames%3==0 || force){
            uint32_t next=mac_screen_signature();
            /* Always capture at frame boundaries: guest RAM never races renderer. */
            if(next!=signature || force || frames%60==0){
                signature=next;paper_capture_frame(mac_render_line_rgb565,capture);
                xSemaphoreTake(mailbox_lock,portMAX_DELAY);
                memcpy(mailbox,capture,PAPER_MAC_BYTES);++sequence;
                xSemaphoreGive(mailbox_lock);
            }
        }
        if(frames%60==0){
            if(!mac_disk_flush()){atomic_store(&io_failed,true);atomic_store(&guest_run,false);}
            mac_status_t s;mac_get_status(&s);
            if(s.halted){atomic_store(&halted,true);atomic_store(&guest_run,false);}
        }
        xSemaphoreGive(guest_lock);
        deadline+=16626;
        int64_t remaining=deadline-esp_timer_get_time();
        if(remaining>1000)vTaskDelay(pdMS_TO_TICKS((uint32_t)(remaining/1000)));
        else {vTaskDelay(1);if(remaining < -16626)deadline=esp_timer_get_time();}
    }
}
static bool load_rom(void)
{
    static const char *names[]={"plus","se","classic"};
    for(int model=0;model<3;++model){
        char path[64];snprintf(path,sizeof(path),"/sdcard/roms/%s.rom",names[model]);
        FILE *f=fopen(path,"rb");if(!f)continue;
        uint32_t expected=131072u<<model;
        size_t n=fread(config.rom_copy,1,expected,f);int extra=fgetc(f);fclose(f);
        mac_model_t found;
        const char *name=n==expected&&extra==EOF?mac_rom_identify(config.rom_copy,expected,&found):NULL;
        if(name && found==model){config.rom=config.rom_copy;config.rom_size=expected;
            mac_pram_defaults(pram,found);
            nvs_handle_t h;size_t len=sizeof(pram);
            if(nvs_open("paper_mac",NVS_READONLY,&h)==ESP_OK){nvs_get_blob(h,"pram",pram,&len);nvs_close(h);}
            ESP_LOGI("app","ROM: %s",name);return true;}
        ESP_LOGW("app","Unrecognized ROM: %s",path);
    }
    return false;
}
static void start_guest(void)
{
    if(!pause_guest())return;
    if(machine_ready){
        if(!mac_disk_flush()){status("Disk flush failed. Keep the SD card in.");return;}
        for(int d=0;d<MAC_DRIVES;++d)mac_disk_eject(d);
    }
    machine_ready=false;
    esp_err_t err=papers3_storage_mount();
    if(err!=ESP_OK){
        ui_message("The SD card could not be read.","Nothing was written to it.",
            "Insert a FAT32 card, then choose","Retry under System.");
        status("No SD card");return;
    }
    mkdir("/sdcard/roms",0775);mkdir("/sdcard/disks",0775);
    if(!load_rom()){
        ui_message("No Macintosh ROM was found.","Copy a 128K Macintosh Plus ROM to",
            "roms/plus.rom on the SD card, and a","startup disk to disks/system.hfv.");
        status("ROM missing");return;
    }
    if(!mac_init(&config)){
        ui_message("The ROM could not be used.","Check the serial log and the ROM file.","","");
        status("ROM not usable");return;
    }
    machine_ready=true;
    static const char *paths[]={"/sdcard/disks/system.hfv","/sdcard/disks/work.hfv","/sdcard/disks/extra.hfv"};
    bool boot_disk=false;
    for(int d=0;d<MAC_DRIVES;++d){bool ok=mac_disk_insert(d,paths[d]);if(d==0)boot_disk=ok;}
    /* Clear setup instructions, then redraw the bezel around the viewport. */
    ESP_ERROR_CHECK(paper_epd_full_clean(30000));
    ui_frame();
    paper_presenter_invalidate(presenter);
    atomic_store(&force_capture,true);atomic_store(&io_failed,false);atomic_store(&halted,false);
    atomic_store(&guest_run,true);
    status(boot_disk?"Macintosh Plus, 4 MB":"No startup disk at disks/system.hfv");
}
static void system_action(int action)
{
    if(action==UI_ACT_PAUSE && machine_ready){
        if(atomic_load(&guest_run)){if(pause_guest()){
            if(!mac_disk_flush()){status("Disk sync failed. Keep the SD card in.");return;}
            save_pram();status("Paused");}}
        else{atomic_store(&guest_run,true);status("Running");}
    }else if(action==UI_ACT_RESET && machine_ready){
        if(!pause_guest())return;
        if(!mac_disk_flush()){status("Disk sync failed. Keep the SD card in.");return;}
        {mac_reset();paper_presenter_invalidate(presenter);
            atomic_store(&force_capture,true);atomic_store(&guest_run,true);status("Restarted");}
    }else if(action==UI_ACT_CLEAN){
        bool resume=atomic_load(&guest_run);
        if(!pause_guest())return;
        if(paper_epd_full_clean(30000)!=ESP_OK){status("Screen clean timed out. Power cycle.");return;}
        ui_frame();paper_presenter_invalidate(presenter);last_sequence=0;atomic_store(&force_capture,true);
        atomic_store(&guest_run,resume);status("Screen cleaned");
    }else if(action==UI_ACT_STOP){
        if(!pause_guest())return;
        if(machine_ready){
            if(!mac_disk_flush()){status("Disk flush failed. Keep the SD card in.");return;}
            save_pram();for(int d=0;d<MAC_DRIVES;++d)mac_disk_eject(d);
        }
        papers3_storage_unmount();machine_ready=false;
        ui_message("It is now safe to remove the SD card.","",
            "Insert a card and choose Retry under","System to start again.");
        status("Safe to remove the SD card");
    }else if(action==UI_ACT_RETRY)start_guest();
}
static void keyboard_press(int code)
{
    if(code<0||!machine_ready||!atomic_load(&guest_run))return;
    for(unsigned i=0;i<5;++i)if(code==modifier_codes[i]){
        modifiers^=1u<<i;mac_key(code,(modifiers&(1u<<i))!=0);
        redraw();return;
    }
    held_key=code;mac_key(code,true);
}
static void touch_update(void)
{
    static bool down, moved, holding;
    static int x0,y0,lastx,lasty,region;
    static int64_t began;
    papers3_touch_t t;
    bool fresh=touch_ready && papers3_touch_read(&t);
    int64_t now=esp_timer_get_time();
    if(fresh){
        bool pressed=t.contacts==1;
        if(pressed && !down){
            down=true;moved=false;holding=false;began=now;
            x0=lastx=t.x;y0=lasty=t.y;region=-1;
            ui_hit_t hit=ui_hit(mode,t.x,t.y);
            bool guest_live=machine_ready && atomic_load(&guest_run);
            switch(hit.kind){
            case UI_HIT_TAB:{
                bool changed=hit.value!=mode;release_input();mode=hit.value;
                redraw();
                if(changed)paper_epd_clean_ui_async();
                ESP_LOGI("touch","Mode=%d portrait x=%u y=%u",mode,t.x,t.y);
                break;}
            case UI_HIT_ACTION:system_action(hit.value);break;
            case UI_HIT_SPEED:set_speed((paper_epd_speed_t)hit.value);break;
            case UI_HIT_KEY:keyboard_press(hit.value);break;
            case UI_HIT_PAD:if(guest_live)region=0;break;
            case UI_HIT_CLICK:if(guest_live){region=1;mac_mouse(0,0,true);}break;
            case UI_HIT_DRAG:
                if(guest_live){drag_lock=!drag_lock;mac_mouse(0,0,drag_lock);
                    redraw();}
                break;
            case UI_HIT_NONE:break;
            }
        }else if(pressed && down){
            if(region==0){
                if(abs((int)t.x-x0)>5 || abs((int)t.y-y0)>5)moved=true;
                if(moved)mac_mouse((int)t.x-lastx,(int)t.y-lasty,holding||drag_lock);
                lastx=t.x;lasty=t.y;
            }
        }else if(!pressed && down){
            if(held_key>=0){mac_key(held_key,false);held_key=-1;}
            if(region==0 || region==1){
                if(region==0 && !moved && !holding && !drag_lock && t.contacts==0 && !t.cancelled){
                    mac_mouse(0,0,true);vTaskDelay(pdMS_TO_TICKS(60));
                }
                mac_mouse(0,0,drag_lock);
            }
            down=false;holding=false;region=-1;
        }
    }
    if(down && region==0 && !moved && !holding && now-began>=500000){
        holding=true;mac_mouse(0,0,true);
    }
}
void app_main(void)
{
    /* Allocate the guest first, before display services fragment PSRAM. */
    config.ram_size=4*1024*1024;
    config.ram=heap_caps_malloc(config.ram_size,MALLOC_CAP_SPIRAM);
    config.rom_copy=heap_caps_malloc(512*1024,MALLOC_CAP_SPIRAM);
    config.opcode_tables=heap_caps_malloc(MAC_OPCODE_TABLES_SIZE,MALLOC_CAP_SPIRAM);
    config.pram=pram;
    capture=heap_caps_malloc(PAPER_MAC_BYTES,MALLOC_CAP_SPIRAM);
    mailbox=heap_caps_malloc(PAPER_MAC_BYTES,MALLOC_CAP_SPIRAM);
    frame=heap_caps_malloc(PAPER_MAC_BYTES,MALLOC_CAP_SPIRAM);
    presenter=heap_caps_malloc(sizeof(*presenter),MALLOC_CAP_SPIRAM);
    assert(config.ram && config.rom_copy && config.opcode_tables && capture && mailbox && frame && presenter);
    mailbox_lock=xSemaphoreCreateMutex();guest_lock=xSemaphoreCreateMutex();
    assert(mailbox_lock && guest_lock);
    esp_err_t nvs=nvs_flash_init();
    if(nvs!=ESP_OK)ESP_LOGW("app","NVS unavailable: %s; PRAM persistence disabled",esp_err_to_name(nvs));
    load_speed();
    ESP_ERROR_CHECK(paper_epd_init());paper_epd_set_speed(speed);ui_init();paper_presenter_init(presenter);
    touch_ready=papers3_touch_init()==ESP_OK;
    assert(xTaskCreatePinnedToCore(guest_task,"mac68k",8192,NULL,5,NULL,1)==pdPASS);
    start_guest();
    int64_t last_present=0,last_stats=esp_timer_get_time();uint32_t previous_frames=0;
    for(;;){
        touch_update();int64_t now=esp_timer_get_time();
        /* One guest frame; the panel worker paces itself on paint time. */
        if(machine_ready && now-last_present>=16000){
            last_present=now;bool changed=false;
            if(xSemaphoreTake(mailbox_lock,0)==pdTRUE){
                if(sequence!=last_sequence){memcpy(frame,mailbox,PAPER_MAC_BYTES);last_sequence=sequence;changed=true;}
                xSemaphoreGive(mailbox_lock);
            }
            if(changed){
                if(!paper_presenter_present(presenter,frame,paper_epd_submit,NULL))last_sequence=0;
                paper_epd_flush_async();
            }
        }
        if(atomic_exchange(&io_failed,false))status("Disk write failed. Paused.");
        if(atomic_exchange(&halted,false))status("CPU halted. See the serial log or Restart.");
        if(now-last_stats>=5000000){
            uint32_t frames=atomic_load(&frame_count);
            ESP_LOGI("app","frames=%lu fps=%.1f paint=%lums internal=%u largest=%u psram=%u touch=%s status=%s",
                (unsigned long)frames,(frames-previous_frames)*1000000.0/(now-last_stats),
                (unsigned long)paper_epd_last_paint_ms(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),touch_ready?"ready":"missing",status_line);
            previous_frames=frames;last_stats=now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
