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


Trước khi build, mở `main/main.c` và thay `YOUR_WIFI_SSID` cùng `YOUR_WIFI_PASSWORD` bằng thông tin mạng Wi-Fi. 
Cắm phần cứng vào rồi Flash Device. 
Monitor Device; Trong log của Monitor có một dòng là "I (4503) WIFI: Got IP address: 192.168.1.9" tùy từng mạng kết nối sẽ có địa chỉ IP khác nhau. Coppy địa chỉ này dán vào trình duyệt sẽ mở được trang web của chương trình này.
* Lưu ý: Khi dùng trong thực tế, để thuận tiện thì nên dùng mạng của máy để lấy IP address. 
