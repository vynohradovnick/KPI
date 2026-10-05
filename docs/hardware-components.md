# Компоненти StageCue

| Компонент | Підключення і робочий режим | Джерело |
| --- | --- | --- |
| Nucleo-C031C6 | STM32C031C6T6, Cortex-M0+, 48 МГц, 32 КіБ flash, 12 КіБ SRAM. USART2 PA2 TX / PA3 RX, LD4 PA5, I²C1 PB8 SCL / PB9 SDA. | ST UM2953, розділи 2 та 6, таблиці розведення; ST DS13866, таблиці призначення виводів і alternate functions. |
| MPU-6050 | I²C 7 біт, AD0=0 дає 0x68. Для HAL адресу передаємо як 0x68 << 1. План 100 кГц, open-drain. VDD кристала 2,375–3,46 В. Для фізичної плати необхідно звірити схему breakout та підтягування до 3,3 В. Типово 3,9 мА з активними гіроскопом і акселерометром. | InvenSense PS-MPU-6000A-00, рев. 3.4, §6.4, §6.5, §9; Wokwi MPU6050 Reference. |
| PIR | VCC, GND, OUT. Цифровий OUT планується на PA8 з рівнями GPIO. У моделі за замовчуванням OUT високий 5 с після стимулу; реальні струм, напруга та рівні залежать від модуля. Для моделі струм не вимірюємо. | Wokwi PIR Motion Sensor Reference, Pin names, Attributes. |
| Модуль LDR | VCC=3,3 В у схемі, GND, AO на PA0 ADC1_IN0. DO не використовується. За описом моделі фоторезистор і резистор 10 кОм формують дільник, AO зростає зі зменшенням освітленості. У ЛР2 підтверджуємо полярність і коди ADC. Струм реального модуля залежить від його схеми і освітлення. | Wokwi Photoresistor Sensor Reference, Pins, Analog output; ST DS13866, ADC characteristics. |

## Регістри MPU-6050 для ЛР2

- `WHO_AM_I` 0x75 повертає 0x68; біт AD0 визначає адресу на шині
  окремо від значення регістра.
- `PWR_MGMT_1` 0x6B. Зняти SLEEP та вибрати джерело тактування.
- `GYRO_CONFIG` 0x1B та `ACCEL_CONFIG` 0x1C. План ±250 °/с і ±2 g,
  відповідно 131 LSB/(°/с) та 16384 LSB/g.
- `ACCEL_XOUT_H` 0x3B. Безперервне читання 14 байтів отримує три осі
  акселерометра, температуру і три осі гіроскопа; 16-бітові значення зі
  знаком, старший байт першим. Джерело: RM-MPU-6000A-00, §4.4–4.6,
  §4.17–4.19, §4.30 та §4.32.

## Джерела

1. STMicroelectronics. STM32C031x4/x6 datasheet, DS13866.
   https://www.st.com/resource/en/datasheet/stm32c031c6.pdf
2. STMicroelectronics. STM32 Nucleo-64 boards (MB1717), UM2953.
   https://www.st.com/resource/en/user_manual/um2953-stm32-nucleo64-board-mb1717-stmicroelectronics.pdf
3. InvenSense. MPU-6000/MPU-6050 Product Specification. Rev. 3.4, 2013.
   https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet.pdf
4. InvenSense. MPU-6000/MPU-6050 Register Map and Descriptions. Rev. 4.2, 2013.
   https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf
5. Wokwi. MPU6050 6-Axis Accel & Gyro Sensor.
   https://docs.wokwi.com/parts/wokwi-mpu6050
6. Wokwi. PIR Motion Sensor.
   https://docs.wokwi.com/parts/wokwi-pir-motion-sensor
7. Wokwi. Photoresistor Sensor Reference.
   https://docs.wokwi.com/parts/wokwi-photoresistor-sensor
