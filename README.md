# ESP32 Camera Server

Dự án ESP-IDF cho ESP32-S3 camera server.

## Yêu cầu

- ESP-IDF đã cài và biến môi trường `IDF_PATH` đã được thiết lập
- ESP32-S3 camera board
- VS Code với extension Espressif IDF hoặc terminal ESP-IDF

## Build và flash

Mở terminal ESP-IDF trong thư mục dự án rồi chạy:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Thay `COMx` bằng cổng COM của board.


Trước khi flash, mở `main/main.c` và thay `YOUR_WIFI_SSID` cùng `YOUR_WIFI_PASSWORD` bằng thông tin mạng Wi-Fi của bạn. Không commit mật khẩu thật lên repository công khai.
