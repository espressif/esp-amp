# Automatic Light Sleep

## Introduction

In ESP-AMP, targets with LP subcore (such as ESP32-C5 and ESP32-C6) can enable automatic light sleep on HP maincore to reduce power consumption. When HP maincore is in light sleep, LP subcore and RTC RAM remain powered on. This allows LP subcore to execute code and access data in RTC RAM when HP maincore is in light sleep, and wake up HP maincore only when access to HP RAM or HP peripherals is required.

> [!NOTE]
> This feature is not supported by ESP32-P4, as HP subcore shares the same power domain of HP maincore. Enabling light sleep on maincore also put subcore into light sleep mode. Therefore, in this document, subcore is limited to LP subcore, and subcore application refers to firmware running on LP subcore.

## Design

ESP chips support various [sleep modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/sleep_modes.html). The automatic light sleep discussed here is not a manually triggered light or deep sleep, but a feature of the [power management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/power_management.html) system. The power management algorithm can automatically adjust the APB and CPU frequencies and put the chip into light sleep mode when no tasks are active for a certain period and all power management locks are released.

During light sleep, HP maincore, HP RAM, and HP peripherals are clock-gated to reduce power consumption, while LP subcore, RTC RAM, and LP peripherals remain active. Waking up HP maincore also resumes functioning of HP RAM and HP peripherals. Accessing HP RAM or HP peripherals while HP maincore is asleep will cause LP subcore to hang. Therefore, subcore application must wake up HP maincore and keep it active until its access to HP resources is complete.

To maximize power savings, subcore application should avoid unnecessary wakeups. The most straightforward approach is to build a **Pure RTC subcore application**. This means the entire subcore application (code, data, and stack) is loaded into RTC RAM, and it does not access any HP RAM or peripheral. In this scenario, there is no need to wake up HP maincore during LP subcore's lifecycle.

However, the 16KB of RTC RAM is often too small for both subcore application and shared memory required for HP-LP inter-processor communication (IPC), such as ESP-AMP queue. Furthermore, atomic operations, which are central to ESP-AMP's software interrupt and event components, are only supported by HP RAM. Consequently, some shared memory for ESP-AMP HP-LP IPC must reside in HP RAM, requiring HP maincore to be active during IPC process. Specifically, when LP subcore sends or receives data from a shared buffer, HP maincore must be awake. ESP-AMP has already handled this logic in most of its APIs.

### Behind the Scenes

This section details the internal implementation of ESP-AMP's automatic light sleep support. You can safely skip this and proceed to the [Usage](#usage) section to learn how to enable this feature and write power-saving applications.

Automatic light sleep leverages FreeRTOS's Tickless IDLE feature. When maincore application releases all [power locks](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/power_management.html) and all FreeRTOS tasks are in a blocked or suspended state, the system calculates the next time an event will wake the OS. If this time exceeds a configured duration (`CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP`), the `esp_pm` component configures a timer wake-up source and attempts to enter light sleep.

Before the `esp_pm` component puts the maincore into light sleep, it performs a final check. `esp_amp` registers a series of `skip_light_sleep_cb_t` callbacks and examines their return values. If any callback returns `true`, `esp_pm` skips entering light sleep for the current tick. This check occurs every systick until all registered callbacks return `false`. The function prototype for this callback and the registration API are defined as follows:

```c
typedef bool (* skip_light_sleep_cb_t)(void);
esp_err_t esp_pm_register_skip_light_sleep_callback(skip_light_sleep_cb_t cb);
```

ESP-AMP uses this callback to keep HP maincore active when needed. A counter, `lp_access_hp_cnt`, is reserved in RTC RAM. When LP subcore needs to access HP RAM or HP peripherals, it increments this counter. The value of the counter indicates the number of outstanding requests from LP subcore for HP resources. The `skip_light_sleep_cb_t` callback returns `true` if `lp_access_hp_cnt` is non-zero. To ensure LP subcore can wake up HP maincore, LP subcore is registered as a wakeup source for HP maincore.

We provide the following two macros for the subcore application to modify `lp_access_hp_cnt`:

```c
#define ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER() esp_amp_system_pm_subcore_skip_light_sleep()
#define ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT() esp_amp_system_pm_subcore_resume_light_sleep()
```

- `esp_amp_system_pm_subcore_skip_light_sleep()` increments the `lp_access_hp_cnt` counter. If the counter was previously zero, the maincore might be in light sleep, so this function also triggers a software interrupt to wake it up. When the maincore is ready to enter light sleep, its skip-light-sleep callback will check the counter and skip sleeping because the counter is now non-zero.
- `esp_amp_system_pm_subcore_resume_light_sleep()` decrements the `lp_access_hp_cnt` counter. No software interrupt is triggered. Once the counter reaches zero, the maincore is allowed to enter light sleep again.

### Light Sleep Behavior of ESP-AMP Components

The following table summarizes which ESP-AMP components are safe for the subcore application to call when automatic light sleep is enabled on the HP maincore.

| ESP-AMP Component  | Safe with Auto Light Sleep? | Key Consideration                                                   |
| :----------------: | :-------------------------: | ------------------------------------------------------------------- |
|  SysInfo (HP RAM)  |             ❌              | Accessing will hang the LP subcore if the HP maincore is asleep.    |
|  SysInfo (LP RAM)  |             ✅              | Safe to access, but atomic operations are not supported on RTC RAM. |
| Software Interrupt |             ✅              | APIs are safe. Keep ISRs on LP subcore short to save power.         |
|       Event        |             ⚠️              | Avoid calling `esp_amp_event_poll()`                                |
|     Virtqueue      |             ✅              | APIs are safe. Wake HP maincore only when accessing data.           |
|       RPMsg        |             ✅              | Safe. Built on top of Virtqueue.                                    |
|        RPC         |             ✅              | Safe. Built on top of RPMsg.                                        |

#### 1. SysInfo

SysInfo data can be allocated from either HP RAM or RTC RAM. When the HP core is in light sleep, it is safe to access SysInfo data in RTC RAM. However, accessing SysInfo data in HP RAM will cause the LP core to hang.

#### 2. Software Interrupt

Software interrupt APIs are light-sleep safe. They rely on a pair of atomic integers in HP RAM to achieve lock-free notifications. The HP maincore is woken up when:

1.  The interrupt bitmask in HP RAM is updated by the LP subcore calling `esp_amp_sw_intr_trigger()`.
2.  The LP subcore receives an interrupt from the HP maincore. This requires the HP maincore to remain active during the entire interrupt handling process.

ESP-AMP automatically wraps access to the interrupt bitmask with `ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER()` and `ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT()`. To maximize power savings, your interrupt handling logic on the LP subcore should be as short as possible.

#### 3. Event

Similar to software interrupt APIs, event APIs are also light-sleep safe. `ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER()` and `ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT()` are called at entry and exit of each API to ensure an active HP power domain throughout the access to event bitmask within HP RAM.

However, `esp_amp_event_poll()` is a blocking API and is **not recommended** for use on LP subcore when light sleep is enabled. It continuously checks the event bitmask, preventing HP maincore from entering light sleep until the poll returns. This polling behavior negates the power-saving benefits of automatic light sleep. Consider using software interrupt as a more power-efficient signaling mechanism from HP to LP core.

#### 4. Virtqueue

Virtqueue APIs are light-sleep safe and optimized for low power consumption. Queue buffers are allocated from HP RAM, but the flags indicating whether a queue is empty or full are in RTC RAM. This allows the LP subcore to check for new data without waking up the HP maincore. The maincore is only woken up when the subcore begins to read or write data.

- When sending data, `ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER()` is called at the entry of `esp_amp_queue_alloc_try()`, and `ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT()` is called at the exit of `esp_amp_queue_send_try()`. This allows the LP subcore application to safely construct data in the HP RAM buffer before sending it.
- Similarly, when receiving data, the HP maincore is kept awake between `esp_amp_queue_recv_try()` and `esp_amp_queue_free_try()`, allowing the LP subcore to safely process data directly from the HP RAM buffer.

#### 5. RPMsg & RPC

RPMsg and RPC APIs are light-sleep safe because they are built on top of the Virtqueue component. They inherit the same power-optimized behavior of waking the HP maincore only when necessary.

## Usage

Set the Kconfig option `CONFIG_ESP_AMP_SYSTEM_AUTO_LIGHT_SLEEP_SUPPORT_ENABLE=y` to enable ESP-AMP support for automatic light sleep.

### Maincore

To enable automatic light sleep on maincore, set Kconfig option `CONFIG_PM_ENABLE=y` and configure power management to enable light sleep.

```c
    esp_pm_config_t pm_config = {.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_IDF_TARGET_ESP32C5
                                 .min_freq_mhz = 48,
#else
                                 .min_freq_mhz = CONFIG_XTAL_FREQ,
#endif
                                 .light_sleep_enable = true
                                };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
```

Since subcore application initialization involves accessing sysinfo data in HP RAM, it is recommended to call `esp_pm_configure()` only after the `EVENT_SUBCORE_READY` event is received.

### Subcore

#### Memory Placement

Subcore code and data can be placed in either HP RAM or RTC RAM. ESP-AMP uses IDF's [linker script generation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/linker-script-generation.html) feature to manage the [memory layout](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-guides/memory-types.html#memory-layout) of the subcore application.

By default, if automatic light sleep is not enabled, the entire subcore application (except for the stack and interrupt vectors) is placed into HP RAM to maximize available space. If `CONFIG_ESP_AMP_SYSTEM_AUTO_LIGHT_SLEEP_SUPPORT_ENABLE=y` is set, the ESP-AMP component itself is placed into RTC RAM. However, other components, including `main` component, are still placed into HP RAM by default. This is left for users to configure.

The Kconfig option `CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP=y` forces linker to place the entire subcore application into RTC RAM. It is equivalent to create a linker fragment file to place every section of subcore application into RTC RAM. This way, you don't need to create your own linker fragment file and examine which section must move from HP RAM to RTC RAM. If the binary is too large to fit, the build will fail. You can find tips on reducing binary size in [Subcore Build Tips](./subcore_build_tips.md). However, this option **DOES NOT** mean that your subcore application never access HP RAM. As mentioned earlier, shared memory for HP-LP IPC still remains in HP RAM. Meanwhile, there is no way to prevent subcore application from accessing HP Peripherals.

### ESP Attribute Macros

ESP-IDF provides a series of macros to control the memory placement of code and data at a fine-grained level:

| Attribute         | Explanation               |
| ----------------- | ------------------------- |
| `RTC_DATA_ATTR`   | Place data in RTC RAM     |
| `RTC_RODATA_ATTR` | Place rodata in RTC RAM   |
| `RTC_IRAM_ATTR`   | Place function in RTC RAM |

```c
RTC_DATA_ATTR static int c = 1;
RTC_RODATA_ATTR static const char str[] = "hello world";

RTC_IRAM_ATTR int add(int a, int b) {
    return a + b;
}
```

> [!NOTE]
> A common practice in IDF applications is to use `IRAM_ATTR` for ISR handlers. However, `IRAM_ATTR` does not place the handler in RTC RAM. Use `RTC_IRAM_ATTR` instead for subcore ISRs that must run during light sleep.

### Linker Script Generation (Linker Fragments)

While [ESP Attribute Macros](#esp-attribute-macros) are useful, ESP-IDF's [Linker Script Generation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/linker-script-generation.html) feature offers more powerful and flexible control over memory placement at different levels of granularity.

ESP-AMP defines two memory placement schemes: `rtc` and `default`. The `rtc` scheme places code/data into RTC RAM, while the `default` scheme places them into HP RAM.

In a typical power-saving application, the `main` component of the subcore should be placed into RTC RAM, as its main loop may execute periodically. Waking up the maincore on every iteration is undesirable. Instead of manually adding `RTC_..._ATTR` to every function and variable in `main` component, ESP-IDF allows you to achieve something equivalent by creating a linker fragment file with merely few lines of code, as shown below.

```lf
[mapping:main]
archive: libmain.a
entries:
    * (rtc)
```

This file directs the linker to use the `rtc` scheme for everything in `libmain.a`, meaning all sections from the `main` component will be placed in the `rtcram` region.

The granularity of a `mapping` rule can be specified by `archive:`, `object:`, or `symbol:`, allowing you to control memory placement for an entire static library, a single object file, or even an individual function or global variable.

> [!TIP]
> User can refer to `examples/light_sleep` for a complete example on how to use linker fragments to instrument memory placement.

> [!IMPORTANT]
> Avoid heap allocation (`malloc`, `calloc`, etc.) in the subcore application when automatic light sleep is enabled, as heap is located in HP RAM.
> If you must use heap allocation, ensure that all heap-related operations, including accessing data stored in heap memory, are wrapped between `ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER()` and `ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT()` calls.

### Accessing HP RAM and HP Peripherals

Subcore application must wake up HP maincore when:

1. Executing a function that is located in HP RAM. Large and infrequently called functions are good candidates to be placed in HP RAM to save RTC RAM space.
2. Accessing an HP peripheral. Although HP peripherals can hold their state during light sleep, they can only be reconfigured when the HP power domain is active.

In either case, the relevant code snippet must be wrapped by `ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER()` and `ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT()`.

> [!IMPORTANT]
> Ensure that `ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER()` and `ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT()` are always called in pairs. A mismatch can prevent the maincore from ever entering light sleep.

### Developing & Debugging Tips

#### Memory Placement Debugging

To verify that your linker fragment rules are working correctly, inspect the `.map` file in your subcore project's build folder. This file lists all symbols and their addresses. You can use this to create a feedback loop for fine-tuning memory placement: adjust your `.lf` rules, rebuild, check the map file, and repeat until all necessary code and data reside in `rtcram`.

#### Profiling Light Sleep

The time HP maincore spends in light sleep can be profiled by registering a pair of power management callbacks.

```c
static esp_err_t enter_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    ESP_EARLY_LOGW("", "Entering light sleep mode, expected sleep time: %lld", sleep_time_us);
    return ESP_OK;
}

static esp_err_t exit_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    ESP_EARLY_LOGW("", "Woken up from light sleep mode, actual sleep time: %lld", sleep_time_us);
    return ESP_OK;
}

esp_pm_sleep_cbs_register_config_t pm_callbacks = {
    .enter_cb = enter_light_sleep_cb,
    .exit_cb = exit_light_sleep_cb,
    .enter_cb_prior = 5,
    .exit_cb_prior = 5,
    .enter_cb_user_arg = NULL,
    .exit_cb_user_arg = NULL
};
ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&pm_callbacks));
```

> [!IMPORTANT]
> Do not call blocking APIs from within power management callbacks.
