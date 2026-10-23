\# Smart Home Automation and Security System



An ESP32-based smart home system that combines voice-controlled appliances, temperature-based fan automation, and motion detection with Telegram security alerts.



\## Features



\- \*\*Voice Control:\*\* Control the light and fan using Google Assistant through Sinric Pro.

\- \*\*Temperature-Based Automation:\*\* Automatically turns the fan on when the temperature exceeds 32°C using a DHT22 sensor.

\- \*\*Security Mode:\*\* Enable a security mode that monitors motion using a PIR sensor.

\- \*\*Motion Alerts:\*\* Activates a buzzer and sends Telegram notifications when motion is detected in security mode.

\- \*\*Remote Notifications:\*\* Receive system status and alerts through Telegram.



\## Hardware Components



\- ESP32 DevKit V1

\- PIR motion sensor

\- DHT22 temperature sensor

\- LED

\- Active buzzer

\- L298N motor driver

\- DC motor (fan prototype)

\- Breadboard and jumper wires



\## Software and Technologies



\- Arduino IDE

\- ESP32 Arduino Core

\- C/C++

\- Sinric Pro

\- Google Assistant

\- Telegram Bot API



\## Project Images



\### Hardware Prototype



!\[Hardware Prototype](images/hardware-prototype.jpeg)



\### Telegram Alert



!\[Telegram Alert](images/telegram-alert.jpeg)



\## Configuration



1\. Install the Arduino IDE and configure support for the ESP32 board.

2\. Install the required libraries: SinricPro and DHT sensor library.

3\. Configure your Wi-Fi, Telegram bot, and Sinric Pro credentials in your \*\*private local copy\*\* of the sketch.

4\. Update the device IDs to match your Sinric Pro devices.

5\. Connect the hardware according to your circuit and upload the sketch to the ESP32.



\*\*Security note:\*\* Never publish Wi-Fi passwords, Telegram bot tokens, or Sinric Pro secrets in a public repository.



\## Future Improvements



\- Add energy monitoring and power-consumption analysis.

\- Improve the mobile monitoring interface.

\- Expand automation with additional sensors and devices.



\## License



This project is available for learning and educational purposes.



