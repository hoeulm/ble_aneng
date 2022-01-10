# ble_aneng
Support for Bluetooth DigitalMultiMeter ANENG V05B by ESP32
![m5_stip_ble_aneng_dmm](https://user-images.githubusercontent.com/78592626/148792252-9028ee94-1614-4eeb-a6e2-77baf146ed4a.jpg)

The ANENG V05 DMM is a cheap DMM which supports Bluetooth.
Unfortunally it is NOT supported by Linux or WIN, only a APP is available.
So i decided to make it available to ANY Browser on ANY OS without the need of Install or Download any Programm or APP

The Data of the DMM are received by a ESP32 which supports Bluetooth
The ESP32 itself exports the DMM Data via
<ul>
  <li>a SERIAL Interface</li>
  <li>a LCD ( M5-Stick CPlus has a 240x135 Color-Display)</li>
  <li>a WebServer which runs directly on the ESP32</li>
  <li>a UPP-Service which directly runs also on the ESP32
</ul>
