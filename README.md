# Lab 3 · Đề 1 · Giám sát và cảnh báo người chạy

**Sinh viên:** Bùi Xuân Hiếu  
**Lớp:** Cần xác nhận  
**Project Wokwi:** [Mở mô phỏng Lab 3 – Đề 1](https://wokwi.com/projects/480651749622503425)

## Cách chạy

1. Tạo project [ESP32 trên Wokwi](https://wokwi.com/projects/new/esp32). Chép nội dung [firmware](Lab3_Runner/Lab3_Runner.ino) vào `sketch.ino`, [sơ đồ](Lab3_Runner/diagram.json) vào `diagram.json` và thêm các thư viện trong [libraries.txt](Lab3_Runner/libraries.txt).
2. Chạy mô phỏng và mở Serial Monitor. Ba dòng khởi động ghi core của từng task. Chờ thông báo kết nối MQTT.
3. Tải hoặc clone repo, mở [web/index.html](web/index.html) trong trình duyệt. Dashboard cần Internet để tải thư viện và kết nối broker. ESP32 dùng MQTT cổng 1883, web dùng MQTT qua WebSocket bảo mật cổng 8081 trên cùng broker thử nghiệm. [Eclipse Mosquitto](https://test.mosquitto.org/)
4. Đọc ba số liệu và thử lần lượt hai nút LED/còi. Mỗi nút đưa hệ thống sang Manual và trạng thái hiển thị cập nhật theo phản hồi của ESP32. Chọn Auto để khôi phục điều khiển tự động.

[Xem báo cáo kỹ thuật và kịch bản kiểm thử](Bao-cao.md).

