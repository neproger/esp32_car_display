# ESP32-C6 Controller

Второй узел проекта на `ESP32-C6`.

Назначение:
- искать `head`-узел по `BLE`
- пробовать подключение по `Long Range / Coded PHY`
- использоваться как удаленный контроллер или клиент

Текущее состояние:
- `BLE central`
- `extended scan`
- поиск устройства `Golf3 K-Line`
- попытка подключения к найденному `head`

## Сборка

```powershell
idf.py set-target esp32c6
idf.py build
idf.py -p <PORT> flash monitor
```
