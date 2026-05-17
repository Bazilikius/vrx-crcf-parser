# CRSF to ELRS Backpack Emulator for ESP32

This standalone ESP32 firmware acts as a bridge between a remote control (using the CRSF protocol) and a video receiver (using ELRS Backpack emulation via ESP-NOW).

It is specifically designed for users with a **RadioMaster Boxer** or similar radio, allowing you to switch video channels and bands using your radio's switches.

## Features
- **CRSF Listener**: Listens on **GPIO 16 (RX2)** with configurable baud rate (default 416,700).
- **Remote Switching (S2/S3)**:
  - **S2 (Channel 12)**: Discrete mapping for all 8 channels:
    - `988us` = Channel 1
    - `1500us` = Channel 5 (Middle)
    - `2012us` = Channel 8
  - **S3 (Channel 11)**: Discrete mapping for all 6 bands:
    - `988`, `1193`, `1398`, `1602`, `1807`, `2012` us.
- **Web Configurator**: Built-in WiFi portal for easy setup and calibration.
- **Binding Support**: Manual binding trigger to pair with your VRX.
- **Dual L-Band Support**: Choose between standard and ELRS L-band grids.
- **VTX Table Editor**: Fully customize frequencies for all 56 channels.
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

## Configuration & Binding
1. After uploading, search for a WiFi network named **"Backpack-Emul"** and connect to it.
2. Open your web browser and go to `http://192.168.4.1`.
3. Enter your **Binding UID** (this must match the UID generated from your binding phrase on your ELRS transmitter).
4. Select your preferred **L-Band grid**.
   - *Tip: If you select Grid 2 (ELRS), the bridge will send "Band X" commands to the receiver. Ensure your receiver's Band X is configured with the correct frequencies.*
5. Click **Save & Restart**.
6. To bind your VRX:
   - Put your VRX into binding mode (refer to your VRX documentation).
   - Click the **"Send Bind Packet"** button on the configuration page.

## Note for RX5808 Diversity Users
If your receiver uses the firmware from the LochnessFPV/RX5808-Div repository, ensure you have enabled the "ELRS Backpack" feature in the receiver's setup menu to allow it to receive commands from this bridge.
