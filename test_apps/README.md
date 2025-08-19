# Usage

## Basic tests

| Supported Targets | ESP32-C5 | ESP32-C6 | ESP32-P4 |
| ----------------- | -------- | -------- | -------- |

```
cd esp_amp_basic_tests
idf.py set-target <target>
pytest --target <target>
```

## Light sleep tests

| Supported Targets | ESP32-C5 | ESP32-C6 |
| ----------------- | -------- | -------- |

```
cd esp_amp_light_sleep_tests
idf.py set-target <target>
pytest --target <target>
```

## Upgrading test dependencies

> Make sure you have **[uv](https://github.com/astral-sh/uv)** installed.

```
uv lock --upgrade
```
