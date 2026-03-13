# ESP32 Logic Analyzer

Отдельная прошивка для обычного `ESP32`, собранная вокруг компонента `ok-home/logic_analyzer`.

Назначение: посмотреть форму сигнала на линии `K-line` с отдельной платы и понять, что отправляет основной `ESP32-C6`.

## Что уже подключено

- компонент `logic_analyzer` лежит в `components/logic_analyzer`
- `app_main()` запускает `SUMP`-режим для `PulseView`
- дефолтная конфигурация задана в `sdkconfig.defaults`

## Дефолтный режим

- target: `esp32`
- транспорт: `Wi-Fi + WebSocket`
- Wi-Fi SSID: `ITPod`
- каналы анализа: `CH0 -> GPIO12`, `CH1 -> GPIO14`, `CH2 -> GPIO27`, `CH3 -> GPIO26`, `CH4 -> GPIO25`, `CH5 -> GPIO33`, `CH6 -> GPIO32`, `CH7 -> GPIO35`, `CH8 -> GPIO34`
- trigger: disabled by default
- sample rate: `2 MHz`
- samples: `20000`

## Важно по подключению K-line

Не подключайте `K-line` 12V напрямую к GPIO анализатора.

Нужно подавать на GPIO уже безопасный цифровой уровень `3.3V`, например:

- через выход `RX`/`K`-монитора существующего K-line трансивера
- через отдельный компаратор/делитель/трансивер, который формирует логический сигнал для ESP32

Если хотите видеть и `TX` со стороны ESP32-C6, и ответную линию после трансивера, заведите их на разные каналы анализатора и поменяйте `CONFIG_ANALYZER_CHAN_*`.

## Сборка

```bash
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor
```

## Web UI

После подключения платы к Wi-Fi откройте:

```text
http://<ip-адрес-платы>/la
```

IP-адрес смотрите в логах загрузки или в списке клиентов роутера.

## Что менять под вашу плату

Через `idf.py menuconfig`:

- `Logic Analyzer Configuration`
- `Use GPIO assignments for channels`
- `GPIO for chahhel 0`
- `GPIO for trigger pin`
- `Samples count`
- `Sample rate HZ`

Для K-line на `10400` бод разумный старт:

- `1-2 MHz` sample rate
- `20k-60k` samples

Если нужно, можно добавить второй канал и одновременно смотреть:

- вход на трансивер
- выход с трансивера
- RX/TX UART
