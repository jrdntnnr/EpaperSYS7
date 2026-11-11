#include "papers3_board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
static i2c_master_dev_handle_t device;
static unsigned failures;
static esp_err_t read_reg(uint16_t reg, uint8_t *data, size_t size)
{
    uint8_t address[] = {reg >> 8, reg & 255};
    return i2c_master_transmit_receive(device, address, 2, data, size, 30);
}
esp_err_t papers3_touch_init(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t cfg = {.i2c_port=I2C_NUM_0, .sda_io_num=41,
        .scl_io_num=42, .clk_source=I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt=7,
        .flags.enable_internal_pullup=true};
    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK) return err;
    const uint8_t addresses[] = {0x5d,0x14};
    for (unsigned i=0; i<2; ++i) {
        if (i2c_master_probe(bus, addresses[i], 50) != ESP_OK) continue;
        i2c_device_config_t dev = {.dev_addr_length=I2C_ADDR_BIT_LEN_7,
            .device_address=addresses[i], .scl_speed_hz=400000};
        if (i2c_master_bus_add_device(bus, &dev, &device) != ESP_OK) continue;
        uint8_t id[4];
        if (read_reg(0x8140,id,4)==ESP_OK && id[0]=='9' && id[1]=='1' && id[2]=='1') {
            ESP_LOGI("touch", "GT911 at 0x%02x",addresses[i]); return ESP_OK;
        }
        i2c_master_bus_rm_device(device); device=NULL;
    }
    i2c_del_master_bus(bus); return ESP_ERR_NOT_FOUND;
}
bool papers3_touch_read(papers3_touch_t *event)
{
    if (!device) return false;
    uint8_t status;
    if (read_reg(0x814e,&status,1)!=ESP_OK) {
        if (++failures==3) { *event=(papers3_touch_t){.cancelled=true}; return true; }
        return false;
    }
    failures=0;
    if (!(status & 0x80)) return false;
    *event=(papers3_touch_t){.contacts=status & 15};
    if (event->contacts==1) {
        uint8_t p[8];
        if (read_reg(0x814f,p,8)!=ESP_OK) {event->contacts=0;event->cancelled=true;}
        else { event->x=p[1]|p[2]<<8; event->y=p[3]|p[4]<<8; }
        if (event->x>=540 || event->y>=960) {event->contacts=0;event->cancelled=true;}
    }
    uint8_t ack[]={0x81,0x4e,0};
    if (i2c_master_transmit(device,ack,3,30)!=ESP_OK) {event->contacts=0;event->cancelled=true;}
    return true;
}
