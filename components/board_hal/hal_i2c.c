#include "hal_i2c.h"

#include <stdbool.h>
#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/* -1 = let the driver pick a free I2C port (driver/i2c_master.h:22). This project has one bus,
 * so it gets port 0, but no port number has to be chosen or kept in sync anywhere. */
#define HAL_I2C_PORT_AUTO (-1)

/* Short pulses on SDA/SCL narrower than this many I2C-module clock cycles are filtered out.
 * 7 is the value the driver documentation calls typical (driver/i2c_master.h:31). */
#define HAL_I2C_GLITCH_IGNORE_CNT 7U

/* Largest 7-bit address. Anything above would not fit the 7 address bits on the wire. */
#define HAL_I2C_ADDR7_MAX 0x7FU

/* The xfer_timeout_ms given to i2c_master_probe(). A probe is only a START, one address byte
 * and a STOP, a fraction of a millisecond on the wire.
 * A probe also has a hardware SCL timeout, fixed at 20 ms (i2c_master.c:1392). When it fires,
 * the controller has stopped and reports a TIMEOUT event (i2c_master.c:797-799, :582). If this
 * software wait ran out first instead, the probe would return and hand the bus lock back while
 * the hardware might still be in the middle of the transfer (i2c_master.c:601-605). So this
 * value is LONGER than 20 ms. Note: neither path resets the I2C controller after a probe
 * timeout in v6.0.2; that is what hal_i2c_bus_reset (later in T04) is for. */
#define HAL_I2C_PROBE_TIMEOUT_MS 50

/* The per-call timeout given to the driver (TASKS.md T04) for register reads and writes. */
#define HAL_I2C_XFER_TIMEOUT_MS 20

/* One slot per sensor (MPU-6050, BMP280). A fixed table instead of a growing list: the size is
 * known at compile time, and nothing is allocated here after boot (§9). */
#define HAL_I2C_MAX_DEVICES 2U

/* HAL_I2C_DEV_INVALID must never be a real slot number, or it would stop meaning "no device". */
_Static_assert(HAL_I2C_MAX_DEVICES <= HAL_I2C_DEV_INVALID, "HAL_I2C_DEV_INVALID collides with a slot");

/* One registered device: its address (to find it again in hal_i2c_probe) and the driver's
 * handle (used by the register transfers). */
typedef struct {
    uint8_t addr7;
    i2c_master_dev_handle_t handle;
} hal_i2c_slot_t;

/* The bus handle. NULL means "not initialised yet", which is how init detects a second call. */
static i2c_master_bus_handle_t s_bus;

/* SCL rate given to every device handle (each handle carries its own rate). Kept here so the
 * sensor drivers never need to know the bus speed. */
static uint32_t s_scl_hz;

/* Slots 0..s_dev_count-1 are in use; a hal_i2c_dev_t is the slot number. Slots are only ever
 * added, never removed, so an index handed out once stays valid. */
static hal_i2c_slot_t s_devs[HAL_I2C_MAX_DEVICES];
static uint8_t s_dev_count;

/* I2C errors since boot (§1). Only app_main touches the bus today, so one task reads and
 * increments it. Once the sampler task (T07) shares the bus, revisit this: reading an aligned
 * 32-bit value is a single load on Xtensa (safe), but `s_err_count++` is load, add, store, and two
 * tasks interleaving those steps can lose a count. Also at T07: a probe that cannot get the bus
 * lock in time returns the same ESP_ERR_TIMEOUT as a stuck bus (i2c_master.c:1358-1360), so
 * "waited for the other task" would be counted as a bus fault. */
static uint32_t s_err_count;

/* True if `addr7` is in the device table. A linear search: the table holds two entries. */
static bool is_registered(uint8_t addr7)
{
    for (uint8_t i = 0U; i < s_dev_count; i++) {
        if (s_devs[i].addr7 == addr7) {
            return true;
        }
    }
    return false;
}

/* The checks every function that takes a hal_i2c_dev_t starts with. The bus check comes first:
 * before init the table is empty, so every index would fail, and "not initialised" is the
 * more useful answer. Only indices hal_i2c_add_device() handed out pass, so `s_devs[dev]` is
 * safe to use afterwards; HAL_I2C_DEV_INVALID never passes. */
static esp_err_t check_dev(hal_i2c_dev_t dev)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (dev >= s_dev_count) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/* Counting rule for transfers through a device handle: every result other than ESP_OK is one
 * error. Unlike a scan probe, a registered device is expected to answer, so a NACK here is a
 * fault too. (In v6.0.2 a synchronous device transfer that does not finish with DONE returns
 * ESP_ERR_INVALID_RESPONSE for a NACK and for a hardware timeout alike, i2c_master.c:722-724, so the code alone does
 * not tell a NACK from a stuck bus anyway.) */
static esp_err_t count_xfer_result(esp_err_t err)
{
    if (err != ESP_OK) {
        s_err_count++;
    }
    return err;
}

esp_err_t hal_i2c_init(int sda_gpio, int scl_gpio, uint32_t scl_hz)
{
    /* A second i2c_new_master_bus() would grab another port and its memory, and the first
     * handle would be lost (a leak). Refuse instead. */
    if (s_bus != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 0 Hz would only fail later, when the first device is added; catch it at boot. */
    if (scl_hz == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const i2c_master_bus_config_t cfg = {
        .i2c_port = HAL_I2C_PORT_AUTO,
        .sda_io_num = (gpio_num_t)sda_gpio,
        .scl_io_num = (gpio_num_t)scl_gpio,
        /* XTAL (40 MHz) on the S3 (soc/esp32s3/include/soc/clk_tree_defs.h:348). */
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = HAL_I2C_GLITCH_IGNORE_CNT,
        .intr_priority = 0, /* 0 = driver picks a default level (1..3) */
        /* 0 = synchronous transfers: each call returns when the transfer is over. A queue
         * depth above 0 switches to asynchronous mode, which does not work with
         * i2c_master_probe() (warning at i2c_master.c:1120). */
        .trans_queue_depth = 0U,
        /* Off: the sensor modules carry the pull-ups (docs/pins.md, "Sensor modules"). */
        .flags.enable_internal_pullup = 0U,
        /* 0 = the driver does not save/restore its registers around sleep, so the I2C power
         * domain must stay on. Sleep is planned in T15; until then nothing powers it down. */
        .flags.allow_pd = 0U,
    };

    /* The driver checks the pins itself (GPIO_IS_VALID_GPIO) and returns ESP_ERR_INVALID_ARG. */
    i2c_master_bus_handle_t bus = NULL;
    const esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK) {
        return err;
    }

    /* Only a fully created bus is stored, so a failed init can simply be retried. */
    s_bus = bus;
    s_scl_hz = scl_hz;
    return ESP_OK;
}

esp_err_t hal_i2c_add_device(uint8_t addr7, hal_i2c_dev_t *out_dev)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_dev == NULL || addr7 > HAL_I2C_ADDR7_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Two handles for one address would make every transfer ambiguous: a typo in app_config.h,
     * caught at boot. */
    if (is_registered(addr7)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev_count >= HAL_I2C_MAX_DEVICES) {
        return ESP_ERR_NO_MEM;
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr7,
        /* This is where the 400 kHz takes effect: the driver sets the bus timing from the
         * device's rate at the start of every transfer to it (i2c_master.c:698). */
        .scl_speed_hz = s_scl_hz,
        .scl_wait_us = 0U,             /* 0 = the driver's default clock-stretch timeout */
        .flags.disable_ack_check = 0U, /* a NACK must end the transfer with an error */
    };

    /* No bus traffic here: the driver only allocates two small structs and links them into its
     * device list (i2c_master.c:1182-1195). So this succeeds even with the sensor unplugged;
     * whether it answers is the probe's job. Those two allocations happen once, at boot. */
    i2c_master_dev_handle_t handle = NULL;
    const esp_err_t err = i2c_master_bus_add_device(s_bus, &cfg, &handle);
    if (err != ESP_OK) {
        return err;
    }

    /* Only a fully created device takes a slot, as with the bus in hal_i2c_init(). */
    s_devs[s_dev_count].addr7 = addr7;
    s_devs[s_dev_count].handle = handle;
    *out_dev = (hal_i2c_dev_t)s_dev_count;
    s_dev_count++;
    return ESP_OK;
}

esp_err_t hal_i2c_probe(uint8_t addr7)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (addr7 > HAL_I2C_ADDR7_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Note: in v6.0.2 a probe always runs at 100 kHz, whatever scl_hz says. The probe has no
     * device handle, so the driver sets a fixed 100 kHz timing for it
     * (i2c_master.c:1385, i2c_hal_set_bus_timing(hal, 100000, ...)). The 400 kHz applies to
     * transfers through a device handle (hal_i2c_add_device), so a scope on SCL during a scan
     * shows 100 kHz, not 400 kHz. */
    const esp_err_t err = i2c_master_probe(s_bus, addr7, HAL_I2C_PROBE_TIMEOUT_MS);

    /* Counting rule (user decision 2026-11-09). A NACK is an error only where a device is
     * expected; at any other address it is the normal "nobody here" answer of a scan. Every
     * other failure (e.g. TIMEOUT) means the bus itself misbehaved, wherever it happened.
     * The early returns above are not counted: no probe went out on the bus. */
    if (err == ESP_ERR_NOT_FOUND) {
        if (is_registered(addr7)) {
            s_err_count++;
        }
    } else if (err != ESP_OK) {
        s_err_count++;
    }

    /* The driver's code is returned unchanged: ESP_OK, ESP_ERR_NOT_FOUND or ESP_ERR_TIMEOUT
     * each mean something different to the caller, so none of them is turned into another. */
    return err;
}

esp_err_t hal_i2c_read_regs(hal_i2c_dev_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    const esp_err_t check = check_dev(dev);
    if (check != ESP_OK) {
        return check;
    }
    /* The driver would refuse these too, but its refusal would come back as a driver error and
     * be counted. Nothing went on the bus, so it is the caller's mistake, not an I2C error. */
    if (buf == NULL || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    /* One transfer: START, address+W, `reg`, then a repeated START (no STOP in between),
     * address+R and `len` bytes, STOP (i2c_master.c:1310-1331). Without the STOP the bus is
     * never released between "set the register pointer" and "read", so nothing can come in
     * between. Both sensors move their register pointer on by one after each byte read, so a
     * multi-byte read returns reg, reg+1, ... */
    const esp_err_t err = i2c_master_transmit_receive(s_devs[dev].handle, &reg, 1U, buf, len, HAL_I2C_XFER_TIMEOUT_MS);
    return count_xfer_result(err);
}

esp_err_t hal_i2c_write_reg(hal_i2c_dev_t dev, uint8_t reg, uint8_t value)
{
    const esp_err_t check = check_dev(dev);
    if (check != ESP_OK) {
        return check;
    }

    /* The sensor takes the first byte after its address as the register number and the next
     * one as the value, so both go out in one transfer: START, address+W, reg, value, STOP.
     * A buffer on the stack is fine: the bus is synchronous (trans_queue_depth = 0 in
     * hal_i2c_init), so the driver is done with it when this call returns. */
    const uint8_t frame[2] = {reg, value};
    const esp_err_t err = i2c_master_transmit(s_devs[dev].handle, frame, sizeof(frame), HAL_I2C_XFER_TIMEOUT_MS);
    return count_xfer_result(err);
}

esp_err_t hal_i2c_bus_reset(void)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* What i2c_master_bus_reset() does on the ESP32-S3 in v6.0.2 (i2c_master.c:1246-1254):
     * 1. Controller reset. The driver does not use the S3's FSM-reset bit (esp32s3 i2c_ll.h:33),
     *    so it resets the whole I2C module (i2c_master.c:121) and writes the saved timing and
     *    glitch-filter settings back (i2c_master.c:132-133): the 400 kHz setup survives.
     * 2. Bus clear. The controller sends 9 SCL pulses in hardware (i2c_master.c:82, 9 =
     *    I2C_LL_RESET_SLV_SCL_PULSE_NUM_DEFAULT, esp32s3 i2c_ll.h:85). A sensor cut off in the
     *    middle of sending a byte keeps holding SDA low; these clocks let it finish the byte and
     *    let go of SDA.
     * 3. The driver's bus status goes back to idle (i2c_master.c:1252).
     * It does not take the driver's bus lock, so it must never run while another task may be
     * using the bus (from T07 on), and its bus clear waits without yielding the CPU
     * (i2c_master.c:86-95): revisit both when the sampler and the task watchdog arrive.
     * Not counted: see hal_i2c.h. */
    return i2c_master_bus_reset(s_bus);
}

uint32_t hal_i2c_error_count(void)
{
    return s_err_count;
}
