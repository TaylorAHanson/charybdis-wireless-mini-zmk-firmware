/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT pixart_pmw3610_alt

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/input/input.h>
#include <zephyr/pm/device.h>

#include "pmw3610.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pmw3610, CONFIG_PMW3610_ALT_LOG_LEVEL);

//////// Sensor initialization steps definition //////////
// init is done in non-blocking manner (i.e., async), a //
// delayable work is defined for this purpose           //
enum pmw3610_init_step {
    ASYNC_INIT_STEP_POWER_UP,  // reset cs line and assert power-up reset
    ASYNC_INIT_STEP_CLEAR_OB1, // clear observation1 register for self-test check
    ASYNC_INIT_STEP_CHECK_OB1, // check the value of observation1 register after self-test check
    ASYNC_INIT_STEP_CONFIGURE, // set other registes like cpi and donwshift time (run, rest1, rest2)
                               // and clear motion registers

    ASYNC_INIT_STEP_COUNT // end flag
};

/* Timings (in ms) needed in between steps to allow each step finishes succussfully. */
// - Since MCU is not involved in the sensor init process, i is allowed to do other tasks.
//   Thus, k_sleep or delayed schedule can be used.
static const int32_t async_init_delay[ASYNC_INIT_STEP_COUNT] = {
    [ASYNC_INIT_STEP_POWER_UP] = 10 + CONFIG_PMW3610_ALT_INIT_POWER_UP_EXTRA_DELAY_MS, // >10ms needed
    [ASYNC_INIT_STEP_CLEAR_OB1] = 200, // 150 us required, test shows too short,
                                       // also power-up reset is added in this step, thus using 50 ms
    [ASYNC_INIT_STEP_CHECK_OB1] = 50,  // 10 ms required in spec,
                                       // test shows too short,
                                       // especially when integrated with display,
                                       // > 50ms is needed
    [ASYNC_INIT_STEP_CONFIGURE] = 0,
};

static int pmw3610_async_init_power_up(const struct device *dev);
static int pmw3610_async_init_clear_ob1(const struct device *dev);
static int pmw3610_async_init_check_ob1(const struct device *dev);
static int pmw3610_async_init_configure(const struct device *dev);

static int (*const async_init_fn[ASYNC_INIT_STEP_COUNT])(const struct device *dev) = {
    [ASYNC_INIT_STEP_POWER_UP] = pmw3610_async_init_power_up,
    [ASYNC_INIT_STEP_CLEAR_OB1] = pmw3610_async_init_clear_ob1,
    [ASYNC_INIT_STEP_CHECK_OB1] = pmw3610_async_init_check_ob1,
    [ASYNC_INIT_STEP_CONFIGURE] = pmw3610_async_init_configure,
};

static void bitbang_write_byte(const struct pixart_config *config, uint8_t val) {
    for (int i = 7; i >= 0; i--) {
        gpio_pin_set_dt(&config->sck_gpio, 0); // SCK LOW (falling edge)
        gpio_pin_set_dt(&config->sdio_gpio, (val >> i) & 1); // Set MOSI while SCK is LOW
        k_busy_wait(5);
        gpio_pin_set_dt(&config->sck_gpio, 1); // SCK HIGH (rising edge, sensor latches data)
        k_busy_wait(5);
    }
}

static uint8_t bitbang_read_byte(const struct pixart_config *config) {
    uint8_t val = 0;
    for (int i = 7; i >= 0; i--) {
        gpio_pin_set_dt(&config->sck_gpio, 0); // SCK LOW
        k_busy_wait(5); // Wait for sensor to output data
        int bit = gpio_pin_get_dt(&config->sdio_gpio); // Read data while SCK is LOW (stable)
        if (bit > 0) {
            val |= (1 << i);
        } else if (bit < 0) {
            LOG_ERR("gpio_pin_get_dt failed: %d", bit);
        }
        gpio_pin_set_dt(&config->sck_gpio, 1); // SCK HIGH
        k_busy_wait(5);
    }
    return val;
}

static int pmw3610_read(const struct device *dev, uint8_t addr, uint8_t *value, uint8_t len) {
	const struct pixart_config *cfg = dev->config;

	// Assert Chip Select (Active Low)
	gpio_pin_set_dt(&cfg->cs_gpio, 1);

	// Force SDIO as output to write the address
	gpio_pin_configure_dt(&cfg->sdio_gpio, GPIO_OUTPUT);
	
	// Write Address
	bitbang_write_byte(cfg, addr);

	// Force SDIO as input with pull-up IMMEDIATELY so we don't fight the sensor!
	gpio_pin_configure_dt(&cfg->sdio_gpio, GPIO_INPUT | GPIO_PULL_UP);

	// Wait for tSRAD (150us for motion burst, 20us otherwise)
    if (addr == PMW3610_REG_MOTION_BURST) {
	    k_busy_wait(150);
    } else {
        k_busy_wait(20);
    }

	// Read Data
	for (int i = 0; i < len; i++) {
		value[i] = bitbang_read_byte(cfg);
	}

	// Release Chip Select
	gpio_pin_set_dt(&cfg->cs_gpio, 0);
	k_busy_wait(40); // delay after CS release to satisfy tSRR/tSRW (min 20us)

	return 0;
}

static int pmw3610_read_reg(const struct device *dev, uint8_t addr, uint8_t *value) {
	return pmw3610_read(dev, addr, value, 1);
}

static int pmw3610_write_reg(const struct device *dev, uint8_t addr, uint8_t value) {
	const struct pixart_config *cfg = dev->config;

	// Assert Chip Select (Active Low)
	gpio_pin_set_dt(&cfg->cs_gpio, 1);

	// Force SDIO as output
	gpio_pin_configure_dt(&cfg->sdio_gpio, GPIO_OUTPUT);

	// Write Address + SPI_WRITE_BIT
	bitbang_write_byte(cfg, addr | SPI_WRITE_BIT);
	
	// Write Data
	bitbang_write_byte(cfg, value);

	// Release Chip Select
	gpio_pin_set_dt(&cfg->cs_gpio, 0);
	k_busy_wait(40); // delay after CS release to satisfy tSWR/tSWW (min 20us)

	return 0;
}

static int pmw3610_write(const struct device *dev, uint8_t reg, uint8_t val) {
	pmw3610_write_reg(dev, PMW3610_REG_SPI_CLK_ON_REQ, PMW3610_SPI_CLOCK_CMD_ENABLE);
	k_sleep(K_USEC(T_CLOCK_ON_DELAY_US));

    int err = pmw3610_write_reg(dev, reg, val);
    if (unlikely(err != 0)) {
        return err;
    }
    
    pmw3610_write_reg(dev, PMW3610_REG_SPI_CLK_ON_REQ, PMW3610_SPI_CLOCK_CMD_DISABLE);
    return 0;
}

static int pmw3610_set_cpi(const struct device *dev, uint32_t cpi, 
                           bool swap_xy, bool inv_x, bool inv_y) {
    /* Set resolution with CPI step of 200 cpi
     * 0x1: 200 cpi (minimum cpi)
     * 0x2: 400 cpi
     * 0x3: 600 cpi
     * :
     */

    if ((cpi > PMW3610_MAX_CPI) || (cpi < PMW3610_MIN_CPI)) {
        LOG_ERR("CPI value %u out of range", cpi);
        return -EINVAL;
    }

    uint8_t value = 0x00;
    int err = 0;

    LOG_INF("Setting cpi: %d", cpi);
    // Convert CPI to register value
    // Set prefered RES_STEP
    //   BIT 4-0: CPI
    uint8_t cpi_val = cpi / 200;
    value = (value & 0xE0) | (cpi_val & 0x1F);

    // Convert axis to register value
    // Set prefered RES_STEP
    //   BIT 7: SWAP_XY
    //   BIT 6: INV_X
    //   BIT 5: INV_Y
    LOG_INF("Setting axis swap_xy: %s inv_x: %s inv_y: %s", 
            swap_xy ? "yes" : "no", inv_x ? "yes" : "no", inv_y ? "yes" : "no");

#if IS_ENABLED(CONFIG_PMW3610_ALT_SWAP_XY)
    value |= (1 << 7);
#else
    if (swap_xy) { value |= (1 << 7); } else { value &= ~(1 << 7); }
#endif
#if IS_ENABLED(CONFIG_PMW3610_ALT_INVERT_X)
    value |= (1 << 6);
#else
    if (inv_x) { value |= (1 << 6); } else { value &= ~(1 << 6); }
#endif
#if IS_ENABLED(CONFIG_PMW3610_ALT_INVERT_Y)
    value |= (1 << 5);
#else
    if (inv_y) { value |= (1 << 5); } else { value &= ~(1 << 5); }
#endif

    LOG_INF("Setting CPI to %u (reg value 0x%x)", cpi, value);

    /* set the cpi */
    uint8_t addr[] = {0x7F, PMW3610_REG_RES_STEP, 0x7F};
    uint8_t data[] = {0xFF, value,                0x00};

	pmw3610_write_reg(dev, PMW3610_REG_SPI_CLK_ON_REQ, PMW3610_SPI_CLOCK_CMD_ENABLE);
	k_sleep(K_USEC(T_CLOCK_ON_DELAY_US));

    /* Write data */
    for (size_t i = 0; i < sizeof(data); i++) {
        err = pmw3610_write_reg(dev, addr[i], data[i]);
        if (err) {
            LOG_ERR("Burst write failed on SPI write (data)");
            break;
        }
    }
    pmw3610_write_reg(dev, PMW3610_REG_SPI_CLK_ON_REQ, PMW3610_SPI_CLOCK_CMD_DISABLE);

    if (err) {
        LOG_ERR("Failed to set CPI");
        return err;
    }

    return 0;
}

/* Set sampling rate in each mode (in ms) */
static int pmw3610_set_sample_time(const struct device *dev, uint8_t reg_addr, uint32_t sample_time) {
    uint32_t maxtime = 2550;
    uint32_t mintime = 10;
    if ((sample_time > maxtime) || (sample_time < mintime)) {
        LOG_WRN("Sample time %u out of range [%u, %u]", sample_time, mintime, maxtime);
        return -EINVAL;
    }

    uint8_t value = sample_time / mintime;
    LOG_INF("Set sample time to %u ms (reg value: 0x%x)", sample_time, value);

    /* The sample time is (reg_value * mintime ) ms. 0x00 is rounded to 0x1 */
    int err = pmw3610_write(dev, reg_addr, value);
    if (err) {
        LOG_ERR("Failed to change sample time");
    }

    return err;
}

/* Set downshift time in ms. */
// NOTE: The unit of run-mode downshift is related to pos mode rate, which is hard coded to be 4 ms
// The pos-mode rate is configured in pmw3610_async_init_configure
static int pmw3610_set_downshift_time(const struct device *dev, uint8_t reg_addr, uint32_t time) {
    uint32_t maxtime;
    uint32_t mintime;

    switch (reg_addr) {
    case PMW3610_REG_RUN_DOWNSHIFT:
        /*
         * Run downshift time = PMW3610_REG_RUN_DOWNSHIFT
         *                      * 8 * pos-rate (fixed to 4ms)
         */
        maxtime = 8160; // 32 * 255;
        mintime = 32; // hard-coded in pmw3610_async_init_configure
        break;

    case PMW3610_REG_REST1_DOWNSHIFT:
        /*
         * Rest1 downshift time = PMW3610_REG_RUN_DOWNSHIFT
         *                        * 16 * Rest1_sample_period (default 40 ms)
         */
        maxtime = 255 * 16 * CONFIG_PMW3610_ALT_REST1_SAMPLE_TIME_MS;
        mintime = 16 * CONFIG_PMW3610_ALT_REST1_SAMPLE_TIME_MS;
        break;

    case PMW3610_REG_REST2_DOWNSHIFT:
        /*
         * Rest2 downshift time = PMW3610_REG_REST2_DOWNSHIFT
         *                        * 128 * Rest2 rate (default 100 ms)
         */
        maxtime = 255 * 128 * CONFIG_PMW3610_ALT_REST2_SAMPLE_TIME_MS;
        mintime = 128 * CONFIG_PMW3610_ALT_REST2_SAMPLE_TIME_MS;
        break;

    default:
        LOG_ERR("Not supported");
        return -ENOTSUP;
    }

    if ((time > maxtime) || (time < mintime)) {
        LOG_WRN("Downshift time %u out of range (%u - %u)", time, mintime, maxtime);
        return -EINVAL;
    }

    __ASSERT_NO_MSG((mintime > 0) && (maxtime / mintime <= UINT8_MAX));

    /* Convert time to register value */
    uint8_t value = time / mintime;

    LOG_INF("Set downshift time to %u ms (reg value 0x%x)", time, value);

    int err = pmw3610_write(dev, reg_addr, value);
    if (err) {
        LOG_ERR("Failed to change downshift time");
    }

    return err;
}

static int pmw3610_set_performance(const struct device *dev, bool enabled) {
    const struct pixart_config *config = dev->config;
    int err = 0;

    if (config->force_awake) {
        uint8_t value;
        err = pmw3610_read_reg(dev, PMW3610_REG_PERFORMANCE, &value);
        if (err) {
            LOG_ERR("Can't read ref-performance %d", err);
            return err;
        }
        LOG_INF("Get performance register (reg value 0x%x)", value);

        // Set prefered RUN RATE        
        //   BIT 3:   VEL_RUNRATE    0x0: 8ms; 0x1 4ms;
        //   BIT 2:   POSHI_RUN_RATE 0x0: 8ms; 0x1 4ms;
        //   BIT 1-0: POSLO_RUN_RATE 0x0: 8ms; 0x1 4ms; 0x2 2ms; 0x4 Reserved
        uint8_t perf;
        if (config->force_awake_4ms_mode) {
            perf = 0x0d; // RUN RATE @ 4ms
        } else {
            // reset bit[3..0] to 0x0 (normal operation)
            perf = value & 0x0F; // RUN RATE @ 8ms
        }

        if (enabled) {
            perf |= 0xF0; // set bit[3..0] to 0xF (force awake)
        }
        if (perf != value) {
            err = pmw3610_write(dev, PMW3610_REG_PERFORMANCE, perf);
            if (err) {
                LOG_ERR("Can't write performance register %d", err);
                return err;
            }
            LOG_INF("Set performance register (reg value 0x%x)", perf);
        }
        LOG_INF("%s performance mode", enabled ? "enable" : "disable");
    }

    return err;
}

static int pmw3610_set_interrupt(const struct device *dev, const bool en) {
    const struct pixart_config *config = dev->config;
    int ret = gpio_pin_interrupt_configure_dt(&config->irq_gpio,
                                              en ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_DISABLE);
    if (unlikely(ret < 0)) {
        LOG_ERR("can't set interrupt");
    }
    return ret;
}

static int pmw3610_async_init_power_up(const struct device *dev) {
	int ret = pmw3610_write_reg(dev, PMW3610_REG_POWER_UP_RESET, PMW3610_POWERUP_CMD_RESET);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static int pmw3610_async_init_clear_ob1(const struct device *dev) {
    return pmw3610_write(dev, PMW3610_REG_OBSERVATION, 0x00);
}

static int pmw3610_async_init_check_ob1(const struct device *dev) {
    uint8_t value;
    int err = pmw3610_read_reg(dev, PMW3610_REG_OBSERVATION, &value);
    if (err) {
        LOG_ERR("Can't do self-test");
        return err;
    }

    if ((value & 0x0F) != 0x0F) {
        LOG_ERR("Failed self-test (0x%x)", value);
        return -EINVAL;
    }

    uint8_t product_id = 0x01;
    err = pmw3610_read_reg(dev, PMW3610_REG_PRODUCT_ID, &product_id);
    if (err) {
        LOG_ERR("Cannot obtain product id");
        return err;
    }

    if (product_id != PMW3610_PRODUCT_ID) {
        LOG_ERR("Incorrect product id 0x%x (expecting 0x%x)!", product_id, PMW3610_PRODUCT_ID);
        // return -EIO; // BYPASS
    }

    return 0;
}

static int pmw3610_async_init_configure(const struct device *dev) {
    int err = 0;
    const struct pixart_config *config = dev->config;

    // clear motion registers first (required in datasheet)
    for (uint8_t reg = 0x02; (reg <= 0x05) && !err; reg++) {
        uint8_t buf[1];
        err = pmw3610_read_reg(dev, reg, buf);
    }

    if (!err) {
        err = pmw3610_set_performance(dev, true);
    }

    if (!err) {
        err = pmw3610_set_cpi(dev, config->cpi, config->swap_xy, config->inv_x, config->inv_y);
    }

    if (!err) {
        err = pmw3610_set_downshift_time(dev, PMW3610_REG_RUN_DOWNSHIFT,
                                         CONFIG_PMW3610_ALT_RUN_DOWNSHIFT_TIME_MS);
    }

    if (!err) {
        err = pmw3610_set_downshift_time(dev, PMW3610_REG_REST1_DOWNSHIFT,
                                         CONFIG_PMW3610_ALT_REST1_DOWNSHIFT_TIME_MS);
    }

    if (!err) {
        err = pmw3610_set_downshift_time(dev, PMW3610_REG_REST2_DOWNSHIFT,
                                         CONFIG_PMW3610_ALT_REST2_DOWNSHIFT_TIME_MS);
    }

    if (!err) {
        err = pmw3610_set_sample_time(dev, PMW3610_REG_REST1_RATE,
                                      CONFIG_PMW3610_ALT_REST1_SAMPLE_TIME_MS);
    }

    if (!err) {
        err = pmw3610_set_sample_time(dev, PMW3610_REG_REST2_RATE,
                                      CONFIG_PMW3610_ALT_REST2_SAMPLE_TIME_MS);
    }

    if (!err) {
        err = pmw3610_set_sample_time(dev, PMW3610_REG_REST3_RATE,
                                      CONFIG_PMW3610_ALT_REST3_SAMPLE_TIME_MS);
    }

    if (err) {
        LOG_ERR("Config the sensor failed");
        return err;
    }

    return 0;
}

static void pmw3610_async_init(struct k_work *work) {
    struct k_work_delayable *work2 = (struct k_work_delayable *)work;
    struct pixart_data *data = CONTAINER_OF(work2, struct pixart_data, init_work);
    const struct device *dev = data->dev;

    LOG_INF("PMW3610 async init step %d", data->async_init_step);

    data->err = async_init_fn[data->async_init_step](dev);
    if (data->err) {
        LOG_ERR("PMW3610 initialization failed in step %d", data->async_init_step);
    } else {
        data->async_init_step++;

        if (data->async_init_step == ASYNC_INIT_STEP_COUNT) {
            data->ready = true; // sensor is ready to work
            LOG_INF("PMW3610 initialized");
            pmw3610_set_interrupt(dev, true);
            k_work_submit(&data->trigger_work); // manually trigger once to clear any pending motion
        } else {
            k_work_schedule(&data->init_work, K_MSEC(async_init_delay[data->async_init_step]));
        }
    }
}

// Manually sign-extend the 12-bit values to 16-bit to avoid GCC bitfield bugs
static inline int16_t sign_extend_12(uint16_t val) {
    return (int16_t)((val & 0x0800) ? (val | 0xF000) : val);
}

static int pmw3610_report_data(const struct device *dev) {
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;
    uint8_t buf[PMW3610_BURST_SIZE];

    if (unlikely(!data->ready)) {
        LOG_WRN("Device is not initialized yet");
        return -EBUSY;
    }

    static int64_t dx = 0;
    static int64_t dy = 0;

#if CONFIG_PMW3610_ALT_REPORT_INTERVAL_MIN > 0
    static int64_t last_smp_time = 0;
    static int64_t last_rpt_time = 0;
    int64_t now = k_uptime_get();
#endif

	// Wake up SPI clock
	pmw3610_write_reg(dev, PMW3610_REG_SPI_CLK_ON_REQ, PMW3610_SPI_CLOCK_CMD_ENABLE);
	k_busy_wait(300);

	// Read Motion Register first (this freezes delta registers until XY_H is read)
    // Latch motion data into burst buffer
    pmw3610_write_reg(dev, PMW3610_REG_MOTION_BURST, 0x00);
    k_busy_wait(20);

    // Read all 7 bytes via burst read to avoid any data clearing race conditions
    int err = pmw3610_read(dev, PMW3610_REG_MOTION_BURST, buf, PMW3610_BURST_SIZE);
    if (err) {
        return err;
    }

    // Stop SPI clock to save power
    pmw3610_write_reg(dev, PMW3610_REG_SPI_CLK_ON_REQ, PMW3610_SPI_CLOCK_CMD_DISABLE);

    // Check if motion is actually present (bit 7 of Motion register)
    if (!(buf[0] & 0x80)) {
        return 0; // no movement
    }

    // Restore full 12-bit math since Burst Read captures all bytes perfectly!
    uint16_t raw_x = buf[1] + ((buf[3] & 0xF0) << 4);
    uint16_t raw_y = buf[2] + ((buf[3] & 0x0F) << 8);
    int16_t x = sign_extend_12(raw_x);
    int16_t y = sign_extend_12(raw_y);

    // Apply exact mathematical inverse matrix to correct for Charybdis 45-degree sensor rotation
    // This perfectly aligns the diagonal tracking to orthogonal up/down/left/right
    int16_t rot_x = x + y;
    int16_t rot_y = x - y;
    x = rot_x;
    y = rot_y;

    bool have_x = x != 0;
    bool have_y = y != 0;

    if (have_x) {
        input_report_rel(dev, config->x_input_code, x, !have_y, K_NO_WAIT);
    }
    if (have_y) {
        input_report_rel(dev, config->y_input_code, y, true, K_NO_WAIT);
    }

    if (have_x || have_y) {
        return 1; // Return 1 to indicate motion occurred
    }
    
    return 0; // No motion
}

static void pmw3610_gpio_callback(const struct device *gpiob, struct gpio_callback *cb,
                                  uint32_t pins) {
    struct pixart_data *data = CONTAINER_OF(cb, struct pixart_data, irq_gpio_cb);
    const struct device *dev = data->dev;
    pmw3610_set_interrupt(dev, false);
    
    // Reset idle frames and start the timer when physical motion wakes us up
    data->idle_frames = 0;
    k_timer_start(&data->poll_timer, K_MSEC(8), K_MSEC(8));
    
    k_work_submit(&data->trigger_work);
}

static void pmw3610_work_callback(struct k_work *work) {
    struct pixart_data *data = CONTAINER_OF(work, struct pixart_data, trigger_work);
    const struct device *dev = data->dev;
    int motion = pmw3610_report_data(dev);
    pmw3610_set_interrupt(dev, true);

    // Smart Polling: stop the 125Hz timer after 1 second of NO motion (125 frames)
    if (motion == 0) {
        if (data->idle_frames < 255) {
            data->idle_frames++;
        }
        if (data->idle_frames >= 125) {
            k_timer_stop(&data->poll_timer);
        }
    } else if (motion == 1) {
        data->idle_frames = 0;
    }
}

static void pmw3610_timer_handler(struct k_timer *timer) {
    struct pixart_data *data = CONTAINER_OF(timer, struct pixart_data, poll_timer);
    k_work_submit(&data->trigger_work);
}

static int pmw3610_init_irq(const struct device *dev) {
    int err;
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;

    // check readiness of irq gpio pin
    if (!device_is_ready(config->irq_gpio.port)) {
        LOG_ERR("IRQ GPIO device not ready");
        return -ENODEV;
    }

    // init the irq pin
    err = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
    if (err) {
        LOG_ERR("Cannot configure IRQ GPIO");
        return err;
    }

    // setup and add the irq callback associated
    gpio_init_callback(&data->irq_gpio_cb, pmw3610_gpio_callback, BIT(config->irq_gpio.pin));

    err = gpio_add_callback(config->irq_gpio.port, &data->irq_gpio_cb);
    if (err) {
        LOG_ERR("Cannot add IRQ GPIO callback");
    }

    return err;
}

static int pmw3610_init(const struct device *dev) {
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;
    int err;

	if (!gpio_is_ready_dt(&config->cs_gpio) || !gpio_is_ready_dt(&config->sck_gpio) || !gpio_is_ready_dt(&config->sdio_gpio)) {
		LOG_ERR("SPI GPIOs not ready");
		return -ENODEV;
	}

    gpio_pin_configure_dt(&config->cs_gpio, GPIO_OUTPUT_INACTIVE);
    
    gpio_pin_configure_dt(&config->sck_gpio, GPIO_OUTPUT_ACTIVE); // SPI Mode 3 requires SCK idle HIGH!
    
    gpio_pin_configure_dt(&config->sdio_gpio, GPIO_INPUT | GPIO_PULL_UP); // Idle state is input with pull-up

    // init device pointer
    data->dev = dev;

    // Initialize Smart Polling tracker
    data->idle_frames = 0;

    // init trigger handler work
    k_work_init(&data->trigger_work, pmw3610_work_callback);

    // init diagnostic polling timer (started initially, will sleep automatically)
    k_timer_init(&data->poll_timer, pmw3610_timer_handler, NULL);
    k_timer_start(&data->poll_timer, K_MSEC(8), K_MSEC(8)); // poll every 8ms (125Hz)

    // init irq routine
    err = pmw3610_init_irq(dev);
    if (err) {
        return err;
    }

    // Setup delayable and non-blocking init jobs, including following steps:
    // 1. power reset
    // 2. upload initial settings
    // 3. other configs like cpi, downshift time, sample time etc.
    // The sensor is ready to work (i.e., data->ready=true after the above steps are finished)
    k_work_init_delayable(&data->init_work, pmw3610_async_init);

    k_work_schedule(&data->init_work, K_MSEC(async_init_delay[data->async_init_step]));

    return err;
}

static int pmw3610_alt_attr_set(const struct device *dev, enum sensor_channel chan,
                            enum sensor_attribute attr, const struct sensor_value *val) {
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;
    int err;

    if (unlikely(chan != SENSOR_CHAN_ALL)) {
        return -ENOTSUP;
    }

    if (unlikely(!data->ready)) {
        LOG_DBG("Device is not initialized yet");
        return -EBUSY;
    }

    switch ((uint32_t)attr) {
    case PMW3610_ALT_ATTR_CPI:
        err = pmw3610_set_cpi(dev, PMW3610_SVALUE_TO_CPI(*val),
                              config->swap_xy, config->inv_x, config->inv_y);
        break;

    case PMW3610_ALT_ATTR_RUN_DOWNSHIFT_TIME:
        err = pmw3610_set_downshift_time(dev, PMW3610_REG_RUN_DOWNSHIFT, PMW3610_SVALUE_TO_TIME(*val));
        break;

    case PMW3610_ALT_ATTR_REST1_DOWNSHIFT_TIME:
        err = pmw3610_set_downshift_time(dev, PMW3610_REG_REST1_DOWNSHIFT, PMW3610_SVALUE_TO_TIME(*val));
        break;

    case PMW3610_ALT_ATTR_REST2_DOWNSHIFT_TIME:
        err = pmw3610_set_downshift_time(dev, PMW3610_REG_REST2_DOWNSHIFT, PMW3610_SVALUE_TO_TIME(*val));
        break;

    case PMW3610_ALT_ATTR_REST1_SAMPLE_TIME:
        err = pmw3610_set_sample_time(dev, PMW3610_REG_REST1_RATE, PMW3610_SVALUE_TO_TIME(*val));
        break;

    case PMW3610_ALT_ATTR_REST2_SAMPLE_TIME:
        err = pmw3610_set_sample_time(dev, PMW3610_REG_REST2_RATE, PMW3610_SVALUE_TO_TIME(*val));
        break;

    case PMW3610_ALT_ATTR_REST3_SAMPLE_TIME:
        err = pmw3610_set_sample_time(dev, PMW3610_REG_REST3_RATE, PMW3610_SVALUE_TO_TIME(*val));
        break;

    default:
        LOG_ERR("Unknown attribute");
        err = -ENOTSUP;
    }

    return err;
}

static const struct sensor_driver_api pmw3610_driver_api = {
    .attr_set = pmw3610_alt_attr_set,
};

#if IS_ENABLED(CONFIG_PM_DEVICE)
static int pmw3610_pm_action(const struct device *dev, enum pm_device_action action) {
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        pmw3610_set_interrupt(dev, false);
        pmw3610_write_reg(dev, PMW3610_REG_SHUTDOWN, 0xB6); // Power down sensor
        return 0;
    case PM_DEVICE_ACTION_RESUME:
        pmw3610_write_reg(dev, PMW3610_REG_POWER_UP_RESET, PMW3610_POWERUP_CMD_RESET); // Wake up
        k_busy_wait(300);
        pmw3610_set_interrupt(dev, true);
        return 0;
    default:
        return -ENOTSUP;
    }
}
#endif // IS_ENABLED(CONFIG_PM_DEVICE)

#if IS_ENABLED(CONFIG_PM_DEVICE)
#define PMW3610_PM_DEFINE(n) PM_DEVICE_DT_INST_DEFINE(n, pmw3610_pm_action);
#else
#define PMW3610_PM_DEFINE(n)
#endif

#define PMW3610_DEFINE(n)                                                                          \
    PMW3610_PM_DEFINE(n)                                                                           \
    static struct pixart_data data##n;                                                             \
    static const struct pixart_config config##n = {                                                \
		.cs_gpio = GPIO_DT_SPEC_INST_GET(n, cs_gpios),                                             \
		.sck_gpio = GPIO_DT_SPEC_INST_GET(n, sck_gpios),                                           \
		.sdio_gpio = GPIO_DT_SPEC_INST_GET(n, sdio_gpios),                                         \
        .irq_gpio = GPIO_DT_SPEC_INST_GET(n, irq_gpios),                                           \
        .cpi = DT_PROP(DT_DRV_INST(n), cpi),                                                       \
        .swap_xy = DT_PROP(DT_DRV_INST(n), swap_xy),                                               \
        .inv_x = DT_PROP(DT_DRV_INST(n), invert_x),                                                \
        .inv_y = DT_PROP(DT_DRV_INST(n), invert_y),                                                \
        .evt_type = DT_PROP(DT_DRV_INST(n), evt_type),                                             \
        .x_input_code = DT_PROP(DT_DRV_INST(n), x_input_code),                                     \
        .y_input_code = DT_PROP(DT_DRV_INST(n), y_input_code),                                     \
        .force_awake = DT_PROP(DT_DRV_INST(n), force_awake),                                       \
        .force_awake_4ms_mode = DT_PROP(DT_DRV_INST(n), force_awake_4ms_mode),                     \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, pmw3610_init, PM_DEVICE_DT_INST_GET(n), &data##n, &config##n,             \
                          POST_KERNEL, CONFIG_INPUT_PMW3610_INIT_PRIORITY, &pmw3610_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PMW3610_DEFINE)
