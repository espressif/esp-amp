| Supported Targets | ESP32-C5 | ESP32-C6 |
| ----------------- | -------- | -------- |

# ESP-AMP Light Sleep Support

This example demonstrates how to safely use ESP-AMP while the maincore enters Light Sleep, and how memory placement affects what the subcore can execute during that time.

## Overview

- Maincore is configured with
  - `light_sleep_enable = true`
  - and registers enter/exit callbacks to log sleep timing.
- Subcore continues running, but any code it executes while the maincore sleeps must live in RTC-retained memory (RTC text/data). Otherwise, it must prevent light sleep around that code.
- Two categories of subcore code:
  - Light-sleep-safe: code/data placed in RTC memory (keeps working while maincore sleeps).
  - Light-sleep-unsafe: code/data in default HP memory (requires a guard to temporarily prevent light sleep).

## What the code does

- `add()`: mapped to RTC text (safe during maincore light sleep). Called without any guard.
- `myrand()`/`seed()`: mapped to default HP memory (not safe during maincore light sleep). Calls are wrapped in:
  - `ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER()`;
  - ...call(s) to `seed()`/`myrand()`...
  - `ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT()`;
- The `myrand()` state variable lcg_state uses `RTC_DATA_ATTR` so it persists across sleep, but the `myrand()` and `seed()` functions themselves are in HP memory.

## Ways to control memory placement

### A) Source-level attributes (quick, per-symbol)

- For functions that must run while the maincore sleeps, place them in RTC fast memory:
  - Use `RTC_FAST_ATTR` (or `RTC_IRAM_ATTR` on some targets) for code.
  - Use `RTC_DATA_ATTR` for data that must retain state.
- Example:

  ```c
  #include "esp_attr.h"

  // Function in RTC fast memory (safe during maincore light sleep)
  RTC_FAST_ATTR int add(int a, int b) {
      return a + b;
  }

  // Data retained in RTC (persists across sleep)
  RTC_DATA_ATTR static int lcg_state = 1;

  // Function not moved (default/HP): requires a sleep guard when called
  int myrand(void) {
      lcg_state = (int)((16807ULL * lcg_state) % 2147483647ULL);
      return (int)lcg_state;
  }
  ```

- When calling code that remains in default/HP memory, guard it:
  ```c
  ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();
  int a = myrand();
  int b = myrand();
  ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();
  ```

### B) Linker mapping files (.lf) (scalable, by archive/object/pattern)

- Use .lf files to tell the build system where to place code/data.
- Examples:
  - `myrand.lf` (default mapping → HP, not sleep-safe)
    ```
    [mapping:_myrand]
    archive: libmyrand.a
    entries:
        * (default)
    ```
  - `add.lf` (rtc mapping → RTC, sleep-safe)
    ```
    [mapping:_add]
    archive: libadd.a
    entries:
        * (rtc)
    ```
  - `main.lf` (example mapping main archive to RTC)
    ```
    [mapping:main]
    archive: libmain.a
    entries:
        * (rtc)
    ```
- Use .lf when you want to place entire libraries or groups of functions without editing source attributes.

## Usage

### Flash and run the example

```
source ${IDF_PATH}/export.sh
idf.py set-target <target>
idf.py build
idf.py flash monitor
```

And you should see output like this:

```
SUB: start
I (0) sys_info: ESP-AMP shared memory (HP RAM): addr=0x4087a580, len=3fe0
I (7) sys_info: ESP-AMP shared memory (RTC RAM): addr=0x500037a0, len=100
I (402) pm: Frequency switching config: CPU_MAX: 160, APB_MAX: 40, APB_MIN: 40, Light sleep: ENABLED
W (412) : Entering light sleep mode, expected sleep time: 9976000
W (921) : Woken up from light sleep mode, actual sleep time: 509734
SUB: sent sum=1081 (a=491, b=590), waiting echo
I (933) app_main: maincore: received sum=1081, echoing back
W (942) : Entering light sleep mode, expected sleep time: 4294435000
W (1461) : Woken up from light sleep mode, actual sleep time: 519618
SUB: recieved echoed sum=1081
W (1477) : Entering light sleep mode, expected sleep time: 4293900000
W (1986) : Woken up from light sleep mode, actual sleep time: 509588
SUB: sent sum=445 (a=126, b=319), waiting echo
I (1998) app_main: maincore: received sum=445, echoing back
W (2007) : Entering light sleep mode, expected sleep time: 4293370000
W (2526) : Woken up from light sleep mode, actual sleep time: 519216
SUB: recieved echoed sum=445
W (2541) : Entering light sleep mode, expected sleep time: 4292836000
W (3050) : Woken up from light sleep mode, actual sleep time: 509227
SUB: sent sum=450 (a=208, b=242), waiting echo
I (3063) app_main: maincore: received sum=450, echoing back
```

### Verify the memory placement (map file)

After building, open the subcore’s link map file (under `build/subcore`) and confirm section addresses:

- Functions in default/HP memory (not sleep-safe):
  ```
  .text.seed     0x40876560        0xe esp-idf/myrand/libmyrand.a(myrand.c.obj)
                 0x40876560                seed
  .text.myrand   0x4087656e       0x32 esp-idf/myrand/libmyrand.a(myrand.c.obj)
                 0x4087656e                myrand
  ```
- RTC-retained data (sleep-safe):
  ```
  .rtc.data.0    0x50001ba8        0x4 esp-idf/myrand/libmyrand.a(myrand.c.obj)
  ```
- Functions in RTC text (sleep-safe):
  ```
  .text.add      0x500001cc        0x4 esp-idf/add/libadd.a(add.c.obj)
                 0x500001cc                add
  ```

Rule of thumb:

- 0x4080\_... (or similar) indicates default/HP memory (not safe while maincore sleeps).
- 0x5000\_... indicates RTC domains (safe during maincore light sleep).

## Misc

### Sleep guards: when and why

- Use ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER()/ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT() around any subcore work that:
  - Calls functions in default/HP text (e.g., myrand(), seed()).
  - Accesses data in default/HP data/rodata.
- It is not needed for work placed in RTC text/data (e.g., add()).

### Important: removing the guards around myrand()

- If you remove the sleep guard around the two `myrand()` calls in the loop, the maincore may enter light sleep while the subcore is executing from default/HP memory.
- Result: the subcore will get stuck (hang or bus fault) once the HP domain is powered down, because `myrand()` resides in HP memory.
- Keep the guards unless you relocate those functions into RTC using attributes or .lf mapping.

Below is an example of stucking if we remove sleep guard around myrand().

```
SUB: start
I (0) sys_info: ESP-AMP shared memory (HP RAM): addr=0x4087a580, len=3fe0
I (7) sys_info: ESP-AMP shared memory (RTC RAM): addr=0x500037a0, len=100
I (402) pm: Frequency switching config: CPU_MAX: 160, APB_MAX: 40, APB_MIN: 40, Light sleep: ENABLED
W (412) : Entering light sleep mode, expected sleep time: 9976000
W (921) : Woken up from light sleep mode, actual sleep time: 509763
SUB: sent sum=1081 (a=491, b=590), waiting echo
I (933)                 <---- stuck
```

### Practical tips

- Prefer RTC for hot paths that must run regardless of the maincore’s sleep state.
- Keep `myrand()`/`seed()` in default memory only if you guard their usage, or move them into RTC if you need them during sleep.
- If you mix RTC and default memory, ensure callees of an RTC function are also in RTC (or guarded) to avoid hidden HP references.
