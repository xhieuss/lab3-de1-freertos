# Báo cáo Lab 3 · Đề 1

## 1. Mục tiêu

Mô phỏng thiết bị giám sát người chạy trên ESP32. BMP180 cung cấp nhiệt độ tại cảm biến và áp suất khí quyển. Firmware ước tính độ cao, hiển thị số liệu trên Serial và gửi qua MQTT. LED và còi cảnh báo tại chỗ hoặc nhận lệnh bằng tay từ dashboard.

## 2. RTOS và cơ chế giao tiếp

RTOS cung cấp bộ lập lịch theo ưu tiên, giúp chia ứng dụng thành các tác vụ có chu kỳ hoặc thời hạn phản hồi riêng. Trong bài này, tác vụ lấy mẫu và cảnh báo tiếp tục chạy khi tác vụ mạng chờ broker. FreeRTOS cung cấp API tác vụ và giao tiếp đồng bộ, phù hợp với tài nguyên vi điều khiển. Đổi lại, từng task cần bộ nhớ stack, việc chia sẻ dữ liệu cần đồng bộ, và thời gian thực tế cần được đo nếu ứng dụng có yêu cầu deadline. [FreeRTOS trên ESP32](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)

Queue sao chép bản tin có cấu trúc sang tác vụ nhận. Queue một phần tử có thể ghi đè bản tin cũ, phù hợp khi bảng điều khiển cần số liệu mới nhất. Semaphore dùng để báo sự kiện hoặc quản lý số lượt truy cập tài nguyên. Ví dụ ISR có thể nhả binary semaphore để đánh thức task chờ sự kiện. Trong thiết kế này, task đọc sở hữu bus I²C và task cảnh báo sở hữu đầu ra. Bản tin và lệnh đi qua queue. [Queues](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/01-Queues)

## 3. Sơ đồ kết nối

| Linh kiện | Chân ESP32 | Kết nối còn lại |
| --- | --- | --- |
| BMP180 SDA | GPIO21 | I²C |
| BMP180 SCL | GPIO22 | I²C |
| BMP180 nguồn | 3V3 | GND chung |
| LED | GPIO26 | Nối tiếp trở 220 Ω, cathode về GND |
| Còi | GPIO25 | Chân còn lại về GND |

[BMP180 trong Wokwi](https://docs.wokwi.com/parts/board-bmp180)

## 4. Tác vụ và thuật toán

| Task | Core | Ưu tiên | Stack | Chu kỳ và dữ liệu |
| --- | --- | --- | --- | --- |
| Read | 1 | 1 | 4096 byte | Mỗi giây đọc BMP180, in Serial, gửi nhiệt độ, áp suất và độ cao vào `data_queue`. Gửi nhiệt độ và độ cao vào `data_alert`. |
| Send | 0 | 1 | 6144 byte | Kiểm tra kết nối và MQTT mỗi khoảng 25 ms khi kết nối ổn định. Gửi mẫu mới, nhận lệnh web và chuyển lệnh qua queue. |
| Alert | 1 | 2 | 3072 byte | Kiểm tra lệnh và mẫu mỗi khoảng 25 ms. Điều khiển đầu ra, ghi trạng thái vào queue để task Send phản hồi web. |

Hai queue dữ liệu cảm biến có một ô mỗi queue. Hai queue phụ chuyển lệnh Manual và trạng thái đầu ra. Chỉ task Send thao tác MQTT. Broker dùng các topic riêng cho số đo, trạng thái và lệnh.

Độ cao ước tính theo h = 44330 × [1 − (p / p₀)^(1 / 5,255)] với p và p₀ cùng đơn vị hPa. Áp suất tham chiếu demo là 1013,25 hPa. Giá trị tuyệt đối phụ thuộc áp suất tham chiếu. Ngưỡng demo giữ từ Lab 2. Cảnh báo nóng bật từ 35 °C và nhả ở 33 °C. Cảnh báo lạnh bật từ 10 °C trở xuống và nhả ở 12 °C. Cảnh báo độ cao bật khi chênh lệch tuyệt đối giữa mẫu mới nhất và mẫu cách đó khoảng 5 giây đạt 5 m. Trong Auto, LED bật và còi phát âm khi có một trong các cảnh báo.

## 5. Kịch bản kiểm thử

| Thao tác | Kết quả cần quan sát |
| --- | --- |
| Khởi chạy ở 24 °C và 101325 Pa | Serial hiện Read và Alert trên Core 1, Send trên Core 0. Web hiện 24 °C, 1013,25 hPa, khoảng 0 m và trạng thái bình thường. |
| Tăng lên 36 °C rồi hạ xuống 32 °C | LED và còi bật khi nóng rồi tắt sau khi nhiệt độ giảm. |
| Hạ xuống 9 °C rồi tăng lên 13 °C | LED và còi bật khi lạnh rồi tắt sau khi nhiệt độ tăng. |
| Giữ áp suất ban đầu ổn định ít nhất 6 giây rồi giảm từ 101325 xuống 101200 Pa | Độ cao ước tính tăng khoảng 10 m và kích hoạt cảnh báo độ cao. |
| Bấm LED/còi trên web rồi chọn Auto | Đầu ra đổi theo từng nút. Dashboard phản hồi trạng thái từ ESP32. Auto tiếp tục áp dụng ngưỡng. |
| Mất kết nối broker khi đang chạy Auto | Serial và cảnh báo tại chỗ tiếp tục chạy. Dashboard khôi phục nhận số liệu khi mạng trở lại. |

Bảng trên mô tả kết quả mong đợi. Minh chứng chạy mô phỏng được bổ sung sau khi thực hiện từng ca.

## 6. Kết quả đã quan sát

Project biên dịch và chạy trên Wokwi. Serial xác nhận Read và Alert ở Core 1, Send ở Core 0. Với BMP180 đặt 24 °C và 101325 Pa, Serial đọc 24,0 °C và 1013,27 hPa. Độ cao tính được khoảng −0,2 m. Serial cũng xác nhận kết nối thành công với MQTT broker. Các ca cảnh báo và điều khiển qua dashboard còn chờ kiểm tra từ đầu đến cuối.

[Mô phỏng Wokwi](https://wokwi.com/projects/480651749622503425)
