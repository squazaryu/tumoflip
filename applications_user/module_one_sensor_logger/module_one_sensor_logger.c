#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_i2c.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODULE_ONE_SENSOR_DATA_DIR     EXT_PATH("apps_data/module_one_sensor_logger")
#define MODULE_ONE_SENSOR_SESSIONS_DIR EXT_PATH("apps_data/module_one_sensor_logger/sessions")

#define MODULE_ONE_SENSOR_I2C_TIMEOUT_MS 5U
#define MODULE_ONE_SENSOR_RX_BUFFER_SIZE 1024U
#define MODULE_ONE_SENSOR_NMEA_LINE_SIZE 96U
#define MODULE_ONE_SENSOR_PATH_SIZE      128U
#define MODULE_ONE_SENSOR_STATUS_SIZE    48U
#define MODULE_ONE_SENSOR_TICK_MS        250U

#define MODULE_ONE_SENSOR_BME280_CHIP_ID 0x60U
#define MODULE_ONE_SENSOR_BME280_ADDR_A  0x76U
#define MODULE_ONE_SENSOR_BME280_ADDR_B  0x77U

typedef enum {
    ModuleOneSensorViewMenu,
    ModuleOneSensorViewRun,
    ModuleOneSensorViewText,
} ModuleOneSensorView;

typedef enum {
    ModuleOneSensorMenuStart,
    ModuleOneSensorMenuInterval,
    ModuleOneSensorMenuBaud,
    ModuleOneSensorMenuStatus,
    ModuleOneSensorMenuAbout,
} ModuleOneSensorMenu;

typedef struct {
    bool ready;
    uint8_t address;
    uint16_t dig_t1;
    int16_t dig_t2;
    int16_t dig_t3;
    uint16_t dig_p1;
    int16_t dig_p2;
    int16_t dig_p3;
    int16_t dig_p4;
    int16_t dig_p5;
    int16_t dig_p6;
    int16_t dig_p7;
    int16_t dig_p8;
    int16_t dig_p9;
    uint8_t dig_h1;
    int16_t dig_h2;
    uint8_t dig_h3;
    int16_t dig_h4;
    int16_t dig_h5;
    int8_t dig_h6;
    int32_t t_fine;
} ModuleOneSensorBme280;

typedef struct {
    bool valid;
    int32_t temperature_c_x100;
    uint32_t pressure_hpa_x100;
    uint32_t humidity_x100;
} ModuleOneSensorBmeSample;

typedef struct {
    bool serial_ready;
    bool seen_sentence;
    bool has_fix;
    uint8_t fix_quality;
    uint8_t satellites;
    char utc_time[16];
    int32_t latitude_e7;
    int32_t longitude_e7;
    int32_t altitude_m_x100;
    uint32_t speed_knots_x100;
} ModuleOneSensorGps;

typedef struct {
    bool logging;
    uint32_t interval_ms;
    uint32_t gps_baud;
    uint32_t next_sample_tick;
    uint32_t sample_count;
    uint32_t write_errors;
    char status[MODULE_ONE_SENSOR_STATUS_SIZE];
    char session_name[48];
    char csv_path[MODULE_ONE_SENSOR_PATH_SIZE];
    char jsonl_path[MODULE_ONE_SENSOR_PATH_SIZE];
    char gpx_path[MODULE_ONE_SENSOR_PATH_SIZE];
    ModuleOneSensorBme280 bme;
    ModuleOneSensorBmeSample last_bme;
    ModuleOneSensorGps gps;
} ModuleOneSensorModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notification;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    View* run_view;
    FuriString* text;
    FuriString* line;
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial_handle;
    File* csv_file;
    File* jsonl_file;
    File* gpx_file;
    uint32_t interval_index;
    uint32_t baud_index;
    char nmea_line[MODULE_ONE_SENSOR_NMEA_LINE_SIZE];
    size_t nmea_line_len;
} ModuleOneSensorApp;

static const uint32_t module_one_sensor_intervals_ms[] = {
    1000U,
    5000U,
    10000U,
    30000U,
    60000U,
};

static const uint32_t module_one_sensor_baud_rates[] = {
    9600U,
    115200U,
};

static const NotificationSequence module_one_sensor_sequence_ok = {
    &message_display_backlight_on,
    &message_green_255,
    &message_delay_10,
    NULL,
};

static const NotificationSequence module_one_sensor_sequence_error = {
    &message_display_backlight_on,
    &message_red_255,
    &message_delay_10,
    NULL,
};

static uint32_t module_one_sensor_tick_ms(void) {
    return (furi_get_tick() * 1000U) / furi_kernel_get_tick_frequency();
}

static uint16_t module_one_sensor_u16_le(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int16_t module_one_sensor_i16_le(const uint8_t* data) {
    return (int16_t)module_one_sensor_u16_le(data);
}

static bool module_one_sensor_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static void module_one_sensor_format_timestamp(FuriString* output) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    furi_string_printf(
        output,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static void module_one_sensor_append_scaled(
    FuriString* output,
    int32_t value,
    uint32_t scale,
    uint8_t fractional_digits) {
    if(value < 0) {
        furi_string_push_back(output, '-');
        value = -value;
    }

    const uint32_t whole = (uint32_t)value / scale;
    const uint32_t fraction = (uint32_t)value % scale;
    furi_string_cat_printf(output, "%lu", (unsigned long)whole);
    if(fractional_digits > 0U) {
        furi_string_push_back(output, '.');
        char format[8];
        snprintf(format, sizeof(format), "%%0%ulu", (unsigned)fractional_digits);
        furi_string_cat_printf(output, format, (unsigned long)fraction);
    }
}

static void module_one_sensor_append_nullable_scaled(
    FuriString* output,
    bool valid,
    int32_t value,
    uint32_t scale,
    uint8_t fractional_digits) {
    if(!valid) {
        furi_string_cat(output, "null");
        return;
    }
    module_one_sensor_append_scaled(output, value, scale, fractional_digits);
}

static bool module_one_sensor_i2c_read(uint8_t addr_7bit, uint8_t reg, uint8_t* data, size_t len) {
    return furi_hal_i2c_trx(
        &furi_hal_i2c_handle_external,
        (uint8_t)(addr_7bit << 1),
        &reg,
        1,
        data,
        len,
        MODULE_ONE_SENSOR_I2C_TIMEOUT_MS);
}

static bool module_one_sensor_i2c_write8(uint8_t addr_7bit, uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return furi_hal_i2c_tx(
        &furi_hal_i2c_handle_external,
        (uint8_t)(addr_7bit << 1),
        data,
        sizeof(data),
        MODULE_ONE_SENSOR_I2C_TIMEOUT_MS);
}

static bool module_one_sensor_bme280_read_calibration(ModuleOneSensorBme280* bme) {
    uint8_t calib1[26];
    uint8_t calib2[7];

    if(!module_one_sensor_i2c_read(bme->address, 0x88, calib1, sizeof(calib1))) {
        return false;
    }
    if(!module_one_sensor_i2c_read(bme->address, 0xE1, calib2, sizeof(calib2))) {
        return false;
    }

    bme->dig_t1 = module_one_sensor_u16_le(&calib1[0]);
    bme->dig_t2 = module_one_sensor_i16_le(&calib1[2]);
    bme->dig_t3 = module_one_sensor_i16_le(&calib1[4]);
    bme->dig_p1 = module_one_sensor_u16_le(&calib1[6]);
    bme->dig_p2 = module_one_sensor_i16_le(&calib1[8]);
    bme->dig_p3 = module_one_sensor_i16_le(&calib1[10]);
    bme->dig_p4 = module_one_sensor_i16_le(&calib1[12]);
    bme->dig_p5 = module_one_sensor_i16_le(&calib1[14]);
    bme->dig_p6 = module_one_sensor_i16_le(&calib1[16]);
    bme->dig_p7 = module_one_sensor_i16_le(&calib1[18]);
    bme->dig_p8 = module_one_sensor_i16_le(&calib1[20]);
    bme->dig_p9 = module_one_sensor_i16_le(&calib1[22]);
    bme->dig_h1 = calib1[25];
    bme->dig_h2 = module_one_sensor_i16_le(&calib2[0]);
    bme->dig_h3 = calib2[2];

    int16_t h4 = ((int16_t)calib2[3] << 4) | (calib2[4] & 0x0F);
    if(h4 & 0x0800) h4 |= 0xF000;
    bme->dig_h4 = h4;

    int16_t h5 = ((int16_t)calib2[5] << 4) | (calib2[4] >> 4);
    if(h5 & 0x0800) h5 |= 0xF000;
    bme->dig_h5 = h5;
    bme->dig_h6 = (int8_t)calib2[6];
    return true;
}

static bool module_one_sensor_bme280_probe(ModuleOneSensorBme280* bme) {
    memset(bme, 0, sizeof(*bme));
    const uint8_t candidates[] = {
        MODULE_ONE_SENSOR_BME280_ADDR_A, MODULE_ONE_SENSOR_BME280_ADDR_B};

    bool found = false;
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    for(size_t i = 0; i < COUNT_OF(candidates); i++) {
        const uint8_t address = candidates[i];
        if(!furi_hal_i2c_is_device_ready(
               &furi_hal_i2c_handle_external,
               (uint8_t)(address << 1),
               MODULE_ONE_SENSOR_I2C_TIMEOUT_MS)) {
            continue;
        }

        uint8_t chip_id = 0;
        if(!module_one_sensor_i2c_read(address, 0xD0, &chip_id, sizeof(chip_id)) ||
           (chip_id != MODULE_ONE_SENSOR_BME280_CHIP_ID)) {
            continue;
        }

        bme->address = address;
        if(!module_one_sensor_bme280_read_calibration(bme)) {
            continue;
        }

        module_one_sensor_i2c_write8(address, 0xF2, 0x01);
        module_one_sensor_i2c_write8(address, 0xF5, 0xA0);
        module_one_sensor_i2c_write8(address, 0xF4, 0x27);
        bme->ready = true;
        found = true;
        break;
    }
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);
    return found;
}

static bool
    module_one_sensor_bme280_sample(ModuleOneSensorBme280* bme, ModuleOneSensorBmeSample* sample) {
    memset(sample, 0, sizeof(*sample));
    if(!bme->ready) {
        return false;
    }

    uint8_t data[8];
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    const bool read_ok = module_one_sensor_i2c_read(bme->address, 0xF7, data, sizeof(data));
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);
    if(!read_ok) {
        return false;
    }

    const int32_t adc_p = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) |
                          ((int32_t)data[2] >> 4);
    const int32_t adc_t = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) |
                          ((int32_t)data[5] >> 4);
    const int32_t adc_h = ((int32_t)data[6] << 8) | data[7];

    int32_t var1 = ((((adc_t >> 3) - ((int32_t)bme->dig_t1 << 1))) * ((int32_t)bme->dig_t2)) >> 11;
    int32_t var2 =
        (((((adc_t >> 4) - ((int32_t)bme->dig_t1)) * ((adc_t >> 4) - ((int32_t)bme->dig_t1))) >>
          12) *
         ((int32_t)bme->dig_t3)) >>
        14;
    bme->t_fine = var1 + var2;
    sample->temperature_c_x100 = ((bme->t_fine * 5) + 128) >> 8;

    int64_t p_var1 = ((int64_t)bme->t_fine) - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)bme->dig_p6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)bme->dig_p5) << 17);
    p_var2 = p_var2 + (((int64_t)bme->dig_p4) << 35);
    p_var1 =
        ((p_var1 * p_var1 * (int64_t)bme->dig_p3) >> 8) + ((p_var1 * (int64_t)bme->dig_p2) << 12);
    p_var1 = (((((int64_t)1) << 47) + p_var1) * ((int64_t)bme->dig_p1)) >> 33;
    if(p_var1 == 0) {
        return false;
    }
    int64_t pressure = 1048576 - adc_p;
    pressure = (((pressure << 31) - p_var2) * 3125) / p_var1;
    p_var1 = (((int64_t)bme->dig_p9) * (pressure >> 13) * (pressure >> 13)) >> 25;
    p_var2 = (((int64_t)bme->dig_p8) * pressure) >> 19;
    pressure = ((pressure + p_var1 + p_var2) >> 8) + (((int64_t)bme->dig_p7) << 4);
    sample->pressure_hpa_x100 = (uint32_t)(pressure / 256);

    int32_t humidity = bme->t_fine - 76800;
    humidity =
        (((((adc_h << 14) - (((int32_t)bme->dig_h4) << 20) - (((int32_t)bme->dig_h5) * humidity)) +
           16384) >>
          15) *
         (((((((humidity * ((int32_t)bme->dig_h6)) >> 10) *
              (((humidity * ((int32_t)bme->dig_h3)) >> 11) + 32768)) >>
             10) +
            2097152) *
               ((int32_t)bme->dig_h2) +
           8192) >>
          14));
    humidity =
        humidity - (((((humidity >> 15) * (humidity >> 15)) >> 7) * ((int32_t)bme->dig_h1)) >> 4);
    if(humidity < 0) humidity = 0;
    if(humidity > 419430400) humidity = 419430400;
    humidity >>= 12;
    sample->humidity_x100 = (uint32_t)((humidity * 100 + 512) / 1024);
    sample->valid = true;
    return true;
}

static bool module_one_sensor_parse_u32(const char* text, uint32_t* value) {
    if(!text || !*text) return false;
    uint32_t result = 0;
    for(const char* cursor = text; *cursor; cursor++) {
        if(!isdigit((unsigned char)*cursor)) return false;
        result = (result * 10U) + (uint32_t)(*cursor - '0');
    }
    *value = result;
    return true;
}

static bool module_one_sensor_parse_decimal_x100(const char* text, int32_t* value) {
    if(!text || !*text) return false;

    bool negative = false;
    if(*text == '-') {
        negative = true;
        text++;
    }

    uint32_t whole = 0;
    bool has_digit = false;
    while(isdigit((unsigned char)*text)) {
        whole = whole * 10U + (uint32_t)(*text - '0');
        text++;
        has_digit = true;
    }

    uint32_t fraction = 0;
    uint8_t fraction_digits = 0;
    if(*text == '.') {
        text++;
        while(isdigit((unsigned char)*text) && (fraction_digits < 2U)) {
            fraction = fraction * 10U + (uint32_t)(*text - '0');
            text++;
            fraction_digits++;
            has_digit = true;
        }
        while(isdigit((unsigned char)*text)) {
            text++;
        }
    }

    if(!has_digit || *text) return false;
    while(fraction_digits < 2U) {
        fraction *= 10U;
        fraction_digits++;
    }

    int32_t result = (int32_t)(whole * 100U + fraction);
    if(negative) result = -result;
    *value = result;
    return true;
}

static bool module_one_sensor_parse_nmea_coordinate(
    const char* value,
    const char* hemisphere,
    uint8_t degree_digits,
    int32_t* coordinate_e7) {
    if(!value || !hemisphere || strlen(value) < (size_t)(degree_digits + 2U)) {
        return false;
    }

    uint32_t degrees = 0;
    for(uint8_t i = 0; i < degree_digits; i++) {
        if(!isdigit((unsigned char)value[i])) return false;
        degrees = (degrees * 10U) + (uint32_t)(value[i] - '0');
    }

    const char* minutes_text = value + degree_digits;
    uint32_t minute_whole = 0;
    uint32_t minute_fraction = 0;
    uint8_t fraction_digits = 0;
    bool has_digit = false;

    while(isdigit((unsigned char)*minutes_text)) {
        minute_whole = (minute_whole * 10U) + (uint32_t)(*minutes_text - '0');
        minutes_text++;
        has_digit = true;
    }

    if(*minutes_text == '.') {
        minutes_text++;
        while(isdigit((unsigned char)*minutes_text) && (fraction_digits < 7U)) {
            minute_fraction = (minute_fraction * 10U) + (uint32_t)(*minutes_text - '0');
            minutes_text++;
            fraction_digits++;
            has_digit = true;
        }
        while(isdigit((unsigned char)*minutes_text)) {
            minutes_text++;
        }
    }

    if(!has_digit || *minutes_text) return false;
    while(fraction_digits < 7U) {
        minute_fraction *= 10U;
        fraction_digits++;
    }

    const uint32_t minutes_e7 = (minute_whole * 10000000U) + minute_fraction;
    int32_t result = (int32_t)((degrees * 10000000U) + (minutes_e7 / 60U));
    if((*hemisphere == 'S') || (*hemisphere == 'W')) {
        result = -result;
    } else if((*hemisphere != 'N') && (*hemisphere != 'E')) {
        return false;
    }

    *coordinate_e7 = result;
    return true;
}

static uint8_t module_one_sensor_split_nmea(char* line, char* fields[], uint8_t max_fields) {
    uint8_t count = 0;
    fields[count++] = line;

    for(char* cursor = line; *cursor && (count < max_fields); cursor++) {
        if((*cursor == ',') || (*cursor == '*')) {
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
    }

    return count;
}

static bool module_one_sensor_nmea_type_is(const char* sentence, const char* type) {
    if(!sentence || (sentence[0] != '$')) return false;
    const size_t sentence_len = strlen(sentence);
    const size_t type_len = strlen(type);
    return (sentence_len >= type_len) && (strcmp(sentence + sentence_len - type_len, type) == 0);
}

static void module_one_sensor_parse_rmc(ModuleOneSensorGps* gps, char* fields[], uint8_t count) {
    if(count < 10U) return;

    gps->seen_sentence = true;
    strlcpy(gps->utc_time, fields[1], sizeof(gps->utc_time));
    gps->has_fix = fields[2][0] == 'A';

    int32_t latitude = 0;
    int32_t longitude = 0;
    if(module_one_sensor_parse_nmea_coordinate(fields[3], fields[4], 2, &latitude) &&
       module_one_sensor_parse_nmea_coordinate(fields[5], fields[6], 3, &longitude)) {
        gps->latitude_e7 = latitude;
        gps->longitude_e7 = longitude;
    }

    int32_t speed_x100 = 0;
    if(module_one_sensor_parse_decimal_x100(fields[7], &speed_x100) && (speed_x100 >= 0)) {
        gps->speed_knots_x100 = (uint32_t)speed_x100;
    }
}

static void module_one_sensor_parse_gga(ModuleOneSensorGps* gps, char* fields[], uint8_t count) {
    if(count < 10U) return;

    gps->seen_sentence = true;
    strlcpy(gps->utc_time, fields[1], sizeof(gps->utc_time));

    uint32_t fix_quality = 0;
    if(module_one_sensor_parse_u32(fields[6], &fix_quality)) {
        gps->fix_quality = (uint8_t)fix_quality;
        gps->has_fix = fix_quality > 0U;
    }

    uint32_t satellites = 0;
    if(module_one_sensor_parse_u32(fields[7], &satellites)) {
        gps->satellites = (uint8_t)satellites;
    }

    int32_t latitude = 0;
    int32_t longitude = 0;
    if(module_one_sensor_parse_nmea_coordinate(fields[2], fields[3], 2, &latitude) &&
       module_one_sensor_parse_nmea_coordinate(fields[4], fields[5], 3, &longitude)) {
        gps->latitude_e7 = latitude;
        gps->longitude_e7 = longitude;
    }

    int32_t altitude_x100 = 0;
    if(module_one_sensor_parse_decimal_x100(fields[9], &altitude_x100)) {
        gps->altitude_m_x100 = altitude_x100;
    }
}

static void module_one_sensor_parse_nmea_line(ModuleOneSensorGps* gps, char* line) {
    if((line[0] != '$') || (strlen(line) < 6U)) return;

    char* fields[20];
    const uint8_t count = module_one_sensor_split_nmea(line, fields, COUNT_OF(fields));
    if(module_one_sensor_nmea_type_is(fields[0], "RMC")) {
        module_one_sensor_parse_rmc(gps, fields, count);
    } else if(module_one_sensor_nmea_type_is(fields[0], "GGA")) {
        module_one_sensor_parse_gga(gps, fields, count);
    }
}

static void module_one_sensor_process_gps_byte(ModuleOneSensorApp* app, char byte) {
    if((byte == '\r') || (byte == '\n')) {
        if(app->nmea_line_len > 0U) {
            app->nmea_line[app->nmea_line_len] = '\0';
            char parse_line[MODULE_ONE_SENSOR_NMEA_LINE_SIZE];
            strlcpy(parse_line, app->nmea_line, sizeof(parse_line));
            with_view_model(
                app->run_view,
                ModuleOneSensorModel * model,
                { module_one_sensor_parse_nmea_line(&model->gps, parse_line); },
                false);
            app->nmea_line_len = 0;
        }
        return;
    }

    if((byte >= ' ') && (byte <= '~')) {
        if(app->nmea_line_len < (sizeof(app->nmea_line) - 1U)) {
            app->nmea_line[app->nmea_line_len++] = byte;
        } else {
            app->nmea_line_len = 0;
        }
    }
}

static void module_one_sensor_drain_gps(ModuleOneSensorApp* app) {
    uint8_t data[64];
    size_t length = 0;
    do {
        length = furi_stream_buffer_receive(app->rx_stream, data, sizeof(data), 0);
        for(size_t i = 0; i < length; i++) {
            module_one_sensor_process_gps_byte(app, (char)data[i]);
        }
    } while(length > 0U);
}

static void module_one_sensor_serial_irq(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    UNUSED(handle);
    ModuleOneSensorApp* app = context;
    if(event & FuriHalSerialRxEventData) {
        const uint8_t data = furi_hal_serial_async_rx(app->serial_handle);
        furi_stream_buffer_send(app->rx_stream, &data, 1, 0);
    }
}

static const char* module_one_sensor_source_name(
    const ModuleOneSensorGps* gps,
    const ModuleOneSensorBmeSample* bme) {
    if(gps->seen_sentence && bme->valid) return "gps+bme280";
    if(gps->seen_sentence) return "gps";
    if(bme->valid) return "bme280";
    return "none";
}

static bool module_one_sensor_write_all(File* file, const char* text) {
    if(!file || !storage_file_is_open(file)) {
        return false;
    }
    const size_t size = strlen(text);
    return storage_file_write(file, text, size) == size;
}

static void module_one_sensor_write_csv(
    ModuleOneSensorApp* app,
    const FuriString* timestamp,
    uint32_t uptime_ms,
    const ModuleOneSensorModel* model) {
    FuriString* row = app->line;
    furi_string_reset(row);
    furi_string_cat_printf(
        row,
        "%s,%lu,%lu,%s,%u,%s,",
        furi_string_get_cstr(timestamp),
        (unsigned long)uptime_ms,
        (unsigned long)model->sample_count,
        module_one_sensor_source_name(&model->gps, &model->last_bme),
        model->gps.has_fix ? 1U : 0U,
        model->gps.utc_time);

    if(model->gps.seen_sentence) {
        module_one_sensor_append_scaled(row, model->gps.latitude_e7, 10000000U, 7);
        furi_string_push_back(row, ',');
        module_one_sensor_append_scaled(row, model->gps.longitude_e7, 10000000U, 7);
        furi_string_push_back(row, ',');
        module_one_sensor_append_scaled(row, model->gps.altitude_m_x100, 100U, 2);
        furi_string_push_back(row, ',');
        module_one_sensor_append_scaled(row, (int32_t)model->gps.speed_knots_x100, 100U, 2);
    } else {
        furi_string_cat(row, ",,,");
    }

    furi_string_push_back(row, ',');
    furi_string_cat_printf(row, "%u,", model->last_bme.valid ? 1U : 0U);
    if(model->last_bme.valid) {
        module_one_sensor_append_scaled(row, model->last_bme.temperature_c_x100, 100U, 2);
        furi_string_push_back(row, ',');
        module_one_sensor_append_scaled(row, (int32_t)model->last_bme.pressure_hpa_x100, 100U, 2);
        furi_string_push_back(row, ',');
        module_one_sensor_append_scaled(row, (int32_t)model->last_bme.humidity_x100, 100U, 2);
    } else {
        furi_string_cat(row, ",,");
    }
    furi_string_push_back(row, '\n');

    if(!module_one_sensor_write_all(app->csv_file, furi_string_get_cstr(row))) {
        with_view_model(
            app->run_view, ModuleOneSensorModel * model_mut, { model_mut->write_errors++; }, false);
    }
}

static void module_one_sensor_write_jsonl(
    ModuleOneSensorApp* app,
    const FuriString* timestamp,
    uint32_t uptime_ms,
    const ModuleOneSensorModel* model) {
    FuriString* row = app->line;
    furi_string_reset(row);
    furi_string_cat_printf(
        row,
        "{\"timestamp\":\"%s\",\"uptime_ms\":%lu,\"sample\":%lu,\"source\":\"%s\","
        "\"gps\":{\"serial\":%s,\"seen\":%s,\"fix\":%s,\"quality\":%u,\"satellites\":%u,"
        "\"utc\":\"%s\",\"lat\":",
        furi_string_get_cstr(timestamp),
        (unsigned long)uptime_ms,
        (unsigned long)model->sample_count,
        module_one_sensor_source_name(&model->gps, &model->last_bme),
        model->gps.serial_ready ? "true" : "false",
        model->gps.seen_sentence ? "true" : "false",
        model->gps.has_fix ? "true" : "false",
        model->gps.fix_quality,
        model->gps.satellites,
        model->gps.utc_time);
    module_one_sensor_append_nullable_scaled(
        row, model->gps.seen_sentence, model->gps.latitude_e7, 10000000U, 7);
    furi_string_cat(row, ",\"lon\":");
    module_one_sensor_append_nullable_scaled(
        row, model->gps.seen_sentence, model->gps.longitude_e7, 10000000U, 7);
    furi_string_cat(row, ",\"altitude_m\":");
    module_one_sensor_append_nullable_scaled(
        row, model->gps.seen_sentence, model->gps.altitude_m_x100, 100U, 2);
    furi_string_cat(row, ",\"speed_knots\":");
    module_one_sensor_append_nullable_scaled(
        row, model->gps.seen_sentence, (int32_t)model->gps.speed_knots_x100, 100U, 2);
    furi_string_cat(row, "},\"bme280\":{\"ready\":");
    furi_string_cat(row, model->bme.ready ? "true" : "false");
    furi_string_cat(row, ",\"address\":\"");
    if(model->bme.ready) {
        furi_string_cat_printf(row, "0x%02X", model->bme.address);
    }
    furi_string_cat(row, "\",\"temperature_c\":");
    module_one_sensor_append_nullable_scaled(
        row, model->last_bme.valid, model->last_bme.temperature_c_x100, 100U, 2);
    furi_string_cat(row, ",\"pressure_hpa\":");
    module_one_sensor_append_nullable_scaled(
        row, model->last_bme.valid, (int32_t)model->last_bme.pressure_hpa_x100, 100U, 2);
    furi_string_cat(row, ",\"humidity_percent\":");
    module_one_sensor_append_nullable_scaled(
        row, model->last_bme.valid, (int32_t)model->last_bme.humidity_x100, 100U, 2);
    furi_string_cat(row, "}}\n");

    if(!module_one_sensor_write_all(app->jsonl_file, furi_string_get_cstr(row))) {
        with_view_model(
            app->run_view, ModuleOneSensorModel * model_mut, { model_mut->write_errors++; }, false);
    }
}

static void module_one_sensor_write_gpx(
    ModuleOneSensorApp* app,
    const FuriString* timestamp,
    const ModuleOneSensorModel* model) {
    if(!model->gps.has_fix || !model->gps.seen_sentence) {
        return;
    }

    FuriString* row = app->line;
    furi_string_reset(row);
    furi_string_cat(row, "  <trkpt lat=\"");
    module_one_sensor_append_scaled(row, model->gps.latitude_e7, 10000000U, 7);
    furi_string_cat(row, "\" lon=\"");
    module_one_sensor_append_scaled(row, model->gps.longitude_e7, 10000000U, 7);
    furi_string_cat(row, "\">\n    <time>");
    furi_string_cat(row, furi_string_get_cstr(timestamp));
    furi_string_cat(row, "Z</time>\n    <ele>");
    module_one_sensor_append_scaled(row, model->gps.altitude_m_x100, 100U, 2);
    furi_string_cat(row, "</ele>\n  </trkpt>\n");

    if(!module_one_sensor_write_all(app->gpx_file, furi_string_get_cstr(row))) {
        with_view_model(
            app->run_view, ModuleOneSensorModel * model_mut, { model_mut->write_errors++; }, false);
    }
}

static bool module_one_sensor_sync_files(ModuleOneSensorApp* app) {
    bool ok = app->csv_file && storage_file_is_open(app->csv_file) &&
              storage_file_sync(app->csv_file);
    ok = app->jsonl_file && storage_file_is_open(app->jsonl_file) &&
         storage_file_sync(app->jsonl_file) && ok;
    ok = app->gpx_file && storage_file_is_open(app->gpx_file) &&
         storage_file_sync(app->gpx_file) && ok;
    return ok;
}

static void module_one_sensor_take_sample(ModuleOneSensorApp* app) {
    FuriString* timestamp = furi_string_alloc();
    module_one_sensor_format_timestamp(timestamp);
    const uint32_t uptime_ms = module_one_sensor_tick_ms();

    ModuleOneSensorModel snapshot;
    with_view_model(
        app->run_view,
        ModuleOneSensorModel * model,
        {
            if(model->bme.ready) {
                module_one_sensor_bme280_sample(&model->bme, &model->last_bme);
            } else {
                memset(&model->last_bme, 0, sizeof(model->last_bme));
            }

            model->sample_count++;
            snapshot = *model;
            snprintf(
                model->status,
                sizeof(model->status),
                "%s %lu",
                snapshot.last_bme.valid || snapshot.gps.seen_sentence ? "Logged" : "No data",
                (unsigned long)snapshot.sample_count);
        },
        false);

    module_one_sensor_write_csv(app, timestamp, uptime_ms, &snapshot);
    module_one_sensor_write_jsonl(app, timestamp, uptime_ms, &snapshot);
    module_one_sensor_write_gpx(app, timestamp, &snapshot);
    if(!module_one_sensor_sync_files(app)) {
        with_view_model(
            app->run_view,
            ModuleOneSensorModel * model,
            {
                model->write_errors++;
                strlcpy(model->status, "Sync error", sizeof(model->status));
            },
            false);
        notification_message(app->notification, &module_one_sensor_sequence_error);
    }
    with_view_model(app->run_view, ModuleOneSensorModel * model, { UNUSED(model); }, true);
    furi_string_free(timestamp);
}

static bool module_one_sensor_make_paths(ModuleOneSensorApp* app) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);

    with_view_model(
        app->run_view,
        ModuleOneSensorModel * model,
        {
            snprintf(
                model->session_name,
                sizeof(model->session_name),
                "sensor_%04u%02u%02u_%02u%02u%02u_%lu",
                now.year,
                now.month,
                now.day,
                now.hour,
                now.minute,
                now.second,
                (unsigned long)(module_one_sensor_tick_ms() % 100000U));
            snprintf(
                model->csv_path,
                sizeof(model->csv_path),
                MODULE_ONE_SENSOR_SESSIONS_DIR "/%s.csv",
                model->session_name);
            snprintf(
                model->jsonl_path,
                sizeof(model->jsonl_path),
                MODULE_ONE_SENSOR_SESSIONS_DIR "/%s.jsonl",
                model->session_name);
            snprintf(
                model->gpx_path,
                sizeof(model->gpx_path),
                MODULE_ONE_SENSOR_SESSIONS_DIR "/%s.gpx",
                model->session_name);
        },
        false);
    return true;
}

static bool module_one_sensor_open_files(ModuleOneSensorApp* app) {
    module_one_sensor_mkdir(app->storage, MODULE_ONE_SENSOR_DATA_DIR);
    module_one_sensor_mkdir(app->storage, MODULE_ONE_SENSOR_SESSIONS_DIR);
    module_one_sensor_make_paths(app);

    ModuleOneSensorModel snapshot;
    with_view_model(app->run_view, ModuleOneSensorModel * model, { snapshot = *model; }, false);

    app->csv_file = storage_file_alloc(app->storage);
    app->jsonl_file = storage_file_alloc(app->storage);
    app->gpx_file = storage_file_alloc(app->storage);

    if(!storage_file_open(app->csv_file, snapshot.csv_path, FSAM_WRITE, FSOM_CREATE_ALWAYS) ||
       !storage_file_open(app->jsonl_file, snapshot.jsonl_path, FSAM_WRITE, FSOM_CREATE_ALWAYS) ||
       !storage_file_open(app->gpx_file, snapshot.gpx_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        return false;
    }

    const char* csv_header =
        "timestamp,uptime_ms,sample,source,gps_fix,gps_time_utc,latitude,longitude,"
        "altitude_m,speed_knots,bme280,temperature_c,pressure_hpa,humidity_percent\n";
    module_one_sensor_write_all(app->csv_file, csv_header);

    FuriString* header = app->line;
    furi_string_reset(header);
    furi_string_cat_printf(
        header,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"Tumoflip Sensor Logger\" "
        "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        "<trk><name>%s</name><trkseg>\n",
        snapshot.session_name);
    module_one_sensor_write_all(app->gpx_file, furi_string_get_cstr(header));
    return module_one_sensor_sync_files(app);
}

static void module_one_sensor_close_files(ModuleOneSensorApp* app) {
    if(app->gpx_file) {
        if(storage_file_is_open(app->gpx_file)) {
            module_one_sensor_write_all(app->gpx_file, "</trkseg></trk>\n</gpx>\n");
            storage_file_sync(app->gpx_file);
            storage_file_close(app->gpx_file);
        }
        storage_file_free(app->gpx_file);
        app->gpx_file = NULL;
    }
    if(app->jsonl_file) {
        if(storage_file_is_open(app->jsonl_file)) {
            storage_file_sync(app->jsonl_file);
            storage_file_close(app->jsonl_file);
        }
        storage_file_free(app->jsonl_file);
        app->jsonl_file = NULL;
    }
    if(app->csv_file) {
        if(storage_file_is_open(app->csv_file)) {
            storage_file_sync(app->csv_file);
            storage_file_close(app->csv_file);
        }
        storage_file_free(app->csv_file);
        app->csv_file = NULL;
    }
}

static void module_one_sensor_close_serial(ModuleOneSensorApp* app) {
    if(app->serial_handle) {
        furi_hal_serial_async_rx_stop(app->serial_handle);
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
        app->serial_handle = NULL;
    }
    app->nmea_line_len = 0;
}

static void module_one_sensor_start_serial(ModuleOneSensorApp* app, uint32_t baud) {
    app->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial_handle) {
        with_view_model(
            app->run_view,
            ModuleOneSensorModel * model,
            {
                model->gps.serial_ready = false;
                strlcpy(model->status, "GPS UART busy", sizeof(model->status));
            },
            false);
        return;
    }

    furi_hal_serial_init(app->serial_handle, baud);
    furi_hal_serial_configure_framing(
        app->serial_handle,
        FuriHalSerialDataBits8,
        FuriHalSerialParityNone,
        FuriHalSerialStopBits1);
    furi_hal_serial_async_rx_start(app->serial_handle, module_one_sensor_serial_irq, app, true);

    with_view_model(
        app->run_view,
        ModuleOneSensorModel * model,
        {
            model->gps.serial_ready = true;
            model->gps_baud = baud;
        },
        false);
}

static void module_one_sensor_stop_logging(ModuleOneSensorApp* app) {
    bool was_logging = false;
    with_view_model(
        app->run_view,
        ModuleOneSensorModel * model,
        {
            was_logging = model->logging;
            model->logging = false;
            if(was_logging) {
                snprintf(
                    model->status,
                    sizeof(model->status),
                    "Stopped %lu",
                    (unsigned long)model->sample_count);
            }
        },
        true);

    module_one_sensor_close_serial(app);
    module_one_sensor_close_files(app);
    if(was_logging) {
        notification_message(app->notification, &module_one_sensor_sequence_ok);
    }
}

static bool module_one_sensor_start_logging(ModuleOneSensorApp* app) {
    module_one_sensor_stop_logging(app);

    with_view_model(
        app->run_view,
        ModuleOneSensorModel * model,
        {
            memset(&model->bme, 0, sizeof(model->bme));
            memset(&model->last_bme, 0, sizeof(model->last_bme));
            memset(&model->gps, 0, sizeof(model->gps));
            model->sample_count = 0;
            model->write_errors = 0;
            model->interval_ms = module_one_sensor_intervals_ms[app->interval_index];
            model->gps_baud = module_one_sensor_baud_rates[app->baud_index];
            model->next_sample_tick = module_one_sensor_tick_ms() + model->interval_ms;
            strlcpy(model->status, "Opening files", sizeof(model->status));
        },
        true);

    if(!module_one_sensor_open_files(app)) {
        module_one_sensor_close_files(app);
        with_view_model(
            app->run_view,
            ModuleOneSensorModel * model,
            { strlcpy(model->status, "File open failed", sizeof(model->status)); },
            true);
        notification_message(app->notification, &module_one_sensor_sequence_error);
        return false;
    }

    with_view_model(
        app->run_view,
        ModuleOneSensorModel * model,
        {
            model->bme.ready = module_one_sensor_bme280_probe(&model->bme);
            strlcpy(model->status, "Logging", sizeof(model->status));
            model->logging = true;
        },
        false);
    module_one_sensor_start_serial(app, module_one_sensor_baud_rates[app->baud_index]);
    module_one_sensor_take_sample(app);
    notification_message(app->notification, &module_one_sensor_sequence_ok);
    return true;
}

static void module_one_sensor_draw_top_action(Canvas* canvas, const char* label) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_rbox(canvas, 96, 1, 32, 12, 2);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str_aligned(canvas, 112, 10, AlignCenter, AlignBottom, label);
    canvas_set_color(canvas, ColorBlack);
}

static void module_one_sensor_draw_callback(Canvas* canvas, void* context) {
    ModuleOneSensorModel* model = context;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Sensor Logger");
    module_one_sensor_draw_top_action(canvas, "Stop");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 21, model->status);
    canvas_draw_str(canvas, 0, 31, model->session_name[0] ? model->session_name : "No session");

    char line[64];
    snprintf(
        line,
        sizeof(line),
        "Int %lus  Samples %lu  Err %lu",
        (unsigned long)(model->interval_ms / 1000U),
        (unsigned long)model->sample_count,
        (unsigned long)model->write_errors);
    canvas_draw_str(canvas, 0, 41, line);

    snprintf(
        line,
        sizeof(line),
        "GPS %s %lu",
        model->gps.serial_ready ? (model->gps.has_fix ? "fix" : "wait") : "off",
        (unsigned long)model->gps_baud);
    canvas_draw_str(canvas, 0, 51, line);

    if(model->last_bme.valid) {
        const bool temp_negative = model->last_bme.temperature_c_x100 < 0;
        const int32_t temp_abs = temp_negative ? -model->last_bme.temperature_c_x100 :
                                                 model->last_bme.temperature_c_x100;
        snprintf(
            line,
            sizeof(line),
            "BME %s%ld.%02ldC %lu.%02luhPa",
            temp_negative ? "-" : "",
            (long)(temp_abs / 100),
            (long)(temp_abs % 100),
            (unsigned long)(model->last_bme.pressure_hpa_x100 / 100U),
            (unsigned long)(model->last_bme.pressure_hpa_x100 % 100U));
    } else {
        snprintf(line, sizeof(line), "BME %s", model->bme.ready ? "wait" : "not found");
    }
    canvas_draw_str(canvas, 0, 61, line);
}

static bool module_one_sensor_input_callback(InputEvent* event, void* context) {
    ModuleOneSensorApp* app = context;
    if(event->type != InputTypeShort) return false;

    if((event->key == InputKeyOk) || (event->key == InputKeyBack)) {
        module_one_sensor_stop_logging(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, ModuleOneSensorViewMenu);
        return true;
    }

    return false;
}

static uint32_t module_one_sensor_run_previous_callback(void* context) {
    ModuleOneSensorApp* app = context;
    module_one_sensor_stop_logging(app);
    return ModuleOneSensorViewMenu;
}

static uint32_t module_one_sensor_text_previous_callback(void* context) {
    UNUSED(context);
    return ModuleOneSensorViewMenu;
}

static void module_one_sensor_tick_callback(void* context) {
    ModuleOneSensorApp* app = context;
    module_one_sensor_drain_gps(app);

    bool should_sample = false;
    with_view_model(
        app->run_view,
        ModuleOneSensorModel * model,
        {
            if(model->logging) {
                const uint32_t now = module_one_sensor_tick_ms();
                if((int32_t)(now - model->next_sample_tick) >= 0) {
                    should_sample = true;
                    model->next_sample_tick = now + model->interval_ms;
                }
            }
        },
        false);

    if(should_sample) {
        module_one_sensor_take_sample(app);
    }
}

static void module_one_sensor_update_menu_labels(ModuleOneSensorApp* app) {
    char label[40];
    snprintf(
        label,
        sizeof(label),
        "Interval: %lus",
        (unsigned long)(module_one_sensor_intervals_ms[app->interval_index] / 1000U));
    submenu_change_item_label(app->submenu, ModuleOneSensorMenuInterval, label);

    snprintf(
        label,
        sizeof(label),
        "GPS baud: %lu",
        (unsigned long)module_one_sensor_baud_rates[app->baud_index]);
    submenu_change_item_label(app->submenu, ModuleOneSensorMenuBaud, label);
}

static void module_one_sensor_show_text(ModuleOneSensorApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, ModuleOneSensorViewText);
}

static void module_one_sensor_build_status(ModuleOneSensorApp* app) {
    ModuleOneSensorBme280 bme;
    const bool bme_ready = module_one_sensor_bme280_probe(&bme);
    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "Sensor Logger Status\n\n"
        "GPS UART: %s\n"
        "GPS baud: %lu\n"
        "BME280: %s",
        furi_hal_serial_control_is_busy(FuriHalSerialIdUsart) ? "busy" : "free",
        (unsigned long)module_one_sensor_baud_rates[app->baud_index],
        bme_ready ? "found " : "not found\n");
    if(bme_ready) {
        furi_string_cat_printf(app->text, "0x%02X\n", bme.address);
    }
    furi_string_cat_printf(
        app->text,
        "Interval: %lu s\n\n"
        "Files are written to:\n%s\n",
        (unsigned long)(module_one_sensor_intervals_ms[app->interval_index] / 1000U),
        MODULE_ONE_SENSOR_SESSIONS_DIR);
}

static void module_one_sensor_build_about(ModuleOneSensorApp* app) {
    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "Module One Sensor Logger\n\n"
        "Logs GPS NMEA and BME280 environmental samples.\n\n"
        "Outputs:\n"
        "- CSV table\n"
        "- JSONL events\n"
        "- GPX track points\n\n"
        "GPS and BME280 are independent; missing hardware is recorded instead of blocking a session.");
}

static void module_one_sensor_menu_callback(void* context, uint32_t index) {
    ModuleOneSensorApp* app = context;
    switch(index) {
    case ModuleOneSensorMenuStart:
        if(module_one_sensor_start_logging(app)) {
            view_dispatcher_switch_to_view(app->view_dispatcher, ModuleOneSensorViewRun);
        } else {
            furi_string_reset(app->text);
            ModuleOneSensorModel snapshot;
            with_view_model(
                app->run_view, ModuleOneSensorModel * model, { snapshot = *model; }, false);
            furi_string_cat_printf(
                app->text,
                "Cannot start logging.\n\n%s\n\nCheck SD card and retry.",
                snapshot.status);
            module_one_sensor_show_text(app);
        }
        break;
    case ModuleOneSensorMenuInterval:
        app->interval_index =
            (app->interval_index + 1U) % COUNT_OF(module_one_sensor_intervals_ms);
        module_one_sensor_update_menu_labels(app);
        submenu_set_selected_item(app->submenu, ModuleOneSensorMenuInterval);
        break;
    case ModuleOneSensorMenuBaud:
        app->baud_index = (app->baud_index + 1U) % COUNT_OF(module_one_sensor_baud_rates);
        module_one_sensor_update_menu_labels(app);
        submenu_set_selected_item(app->submenu, ModuleOneSensorMenuBaud);
        break;
    case ModuleOneSensorMenuStatus:
        module_one_sensor_build_status(app);
        module_one_sensor_show_text(app);
        break;
    case ModuleOneSensorMenuAbout:
        module_one_sensor_build_about(app);
        module_one_sensor_show_text(app);
        break;
    default:
        break;
    }
}

static bool module_one_sensor_back_callback(void* context) {
    ModuleOneSensorApp* app = context;
    module_one_sensor_stop_logging(app);
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static ModuleOneSensorApp* module_one_sensor_alloc(void) {
    ModuleOneSensorApp* app = malloc(sizeof(ModuleOneSensorApp));
    memset(app, 0, sizeof(ModuleOneSensorApp));
    app->interval_index = 1;
    app->baud_index = 0;
    app->rx_stream = furi_stream_buffer_alloc(MODULE_ONE_SENSOR_RX_BUFFER_SIZE, 1);
    app->text = furi_string_alloc();
    app->line = furi_string_alloc();
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, module_one_sensor_back_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, module_one_sensor_tick_callback, MODULE_ONE_SENSOR_TICK_MS);

    app->submenu = submenu_alloc();
    submenu_set_header(app->submenu, "Sensor Logger");
    submenu_add_item(
        app->submenu,
        "Start logging",
        ModuleOneSensorMenuStart,
        module_one_sensor_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Interval: 5s",
        ModuleOneSensorMenuInterval,
        module_one_sensor_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "GPS baud: 9600",
        ModuleOneSensorMenuBaud,
        module_one_sensor_menu_callback,
        app);
    submenu_add_item(
        app->submenu, "Status", ModuleOneSensorMenuStatus, module_one_sensor_menu_callback, app);
    submenu_add_item(
        app->submenu, "About", ModuleOneSensorMenuAbout, module_one_sensor_menu_callback, app);
    module_one_sensor_update_menu_labels(app);

    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(
        text_box_get_view(app->text_box), module_one_sensor_text_previous_callback);

    app->run_view = view_alloc();
    view_allocate_model(app->run_view, ViewModelTypeLocking, sizeof(ModuleOneSensorModel));
    view_set_draw_callback(app->run_view, module_one_sensor_draw_callback);
    view_set_input_callback(app->run_view, module_one_sensor_input_callback);
    view_set_previous_callback(app->run_view, module_one_sensor_run_previous_callback);
    view_set_context(app->run_view, app);
    with_view_model(
        app->run_view,
        ModuleOneSensorModel * model,
        {
            memset(model, 0, sizeof(ModuleOneSensorModel));
            model->interval_ms = module_one_sensor_intervals_ms[app->interval_index];
            model->gps_baud = module_one_sensor_baud_rates[app->baud_index];
            strlcpy(model->status, "Ready", sizeof(model->status));
        },
        true);

    view_dispatcher_add_view(
        app->view_dispatcher, ModuleOneSensorViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, ModuleOneSensorViewRun, app->run_view);
    view_dispatcher_add_view(
        app->view_dispatcher, ModuleOneSensorViewText, text_box_get_view(app->text_box));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, ModuleOneSensorViewMenu);
    return app;
}

static void module_one_sensor_free(ModuleOneSensorApp* app) {
    furi_assert(app);
    module_one_sensor_stop_logging(app);
    view_dispatcher_remove_view(app->view_dispatcher, ModuleOneSensorViewText);
    view_dispatcher_remove_view(app->view_dispatcher, ModuleOneSensorViewRun);
    view_dispatcher_remove_view(app->view_dispatcher, ModuleOneSensorViewMenu);
    view_free(app->run_view);
    text_box_free(app->text_box);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_stream_buffer_free(app->rx_stream);
    furi_string_free(app->line);
    furi_string_free(app->text);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t module_one_sensor_logger_app(void* context) {
    UNUSED(context);
    ModuleOneSensorApp* app = module_one_sensor_alloc();
    view_dispatcher_run(app->view_dispatcher);
    module_one_sensor_free(app);
    return 0;
}
