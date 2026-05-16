# CRSF to ELRS Backpack Emulator for ESP32

This standalone ESP32 firmware acts as a bridge between a remote control (using the CRSF protocol) and a video receiver (using ELRS Backpack emulation via ESP-NOW).

It is specifically designed for users with a **RadioMaster Boxer** or similar radio, allowing you to switch video channels and bands using your radio's switches.

## Features
- **CRSF Listener**: Listens on **GPIO 16 (RX2)** at 420,000 baud.
- **Remote Switching**:
  - **S2 (Channel 12)**: Switches through 8 video channels.
  - **S3 (Channel 11)**: Switches through 6 bands (A, B, E, F, R, L).
- **Web Configurator**: Built-in WiFi portal for easy setup.
- **Dual L-Band Support**: Choose between standard and ELRS L-band grids.
- **Persistence**: Settings are saved in the ESP32's memory (NVS).

## Hardware Setup
1. Use a standard ESP32 development board.
2. Connect your ELRS receiver's **TX** pin (CRSF data) to the ESP32's **GPIO 16**.
3. *Note: Only the RX pin on the ESP32 is used.*

## Installation (Arduino IDE)
1. Open the Arduino IDE.
2. Go to **File > Open** and select `BackpackEmulator.ino` from the `BackpackEmulator` folder.
3. Ensure you have the **ESP32 board support** installed (via Boards Manager).
4. Select your board (e.g., "ESP32 Dev Module").
5. Click **Upload**.

## Configuration
1. After uploading, search for a WiFi network named **"Backpack-Emul"** and connect to it.
2. Open your web browser and go to `http://192.168.4.1`.
3. Enter your **Binding UID** (this must match the UID generated from your binding phrase on your ELRS transmitter).
4. (Optional) Adjust the CRSF channel mappings for Video and Band switching.
5. Select your preferred **L-Band grid**.
   - *Tip: If you select Grid 2 (ELRS), the bridge will send "Band X" commands to the receiver. Ensure your receiver's Band X is configured with the correct frequencies.*
6. Click **Save**. The ESP32 will restart with the new settings.
