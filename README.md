# stm32-speech-recognition

## 1. Project Overview

The stm32-speech-recognition project is a high-performance Edge AI IoT framework that establishes a Gateway Architecture between a physical STM32F401CCU6 microcontroller and the Google Gemini Large Language Model (LLM). This system enables low-latency voice-command execution by offloading complex natural language processing and sentiment analysis to the Gemini 2.5 Flash API while maintaining real-time hardware control on the embedded target.

The system captures acoustic data via a host workstation, performs inference to determine intent and sentiment, and transmits serialized execution tokens to the STM32 via a native USB Communication Device Class (CDC) interface. The firmware is designed with a strictly non-blocking architecture, utilizing state machines and hardware interrupts to ensure deterministic behavior across multiple operational modes.

## 2. System Architecture

The pipeline follows a sequential data transformation flow:

1.  **Acoustic Acquisition**: The Python-based bridge utilizes the SpeechRecognition and PyAudio libraries to capture vocal input from the host workstation microphone.
2.  **Transcription and Inference**: The captured audio is converted to text and forwarded to the Google Gemini 2.5 Flash LLM. A specialized system prompt constrains the model to perform sentiment analysis and return a single-character command token.
3.  **Protocol Translation**: The Python bridge parses the LLM response and transmits a 1-byte control character over the Virtual COM Port (VCP).
4.  **Embedded Execution**: The STM32 receives the byte via a USB CDC interrupt. The firmware’s main loop, driven by a `HAL_GetTick()` polling mechanism, executes the corresponding hardware animation or state transition without blocking system execution.

## 3. Hardware Pinout and Wiring

The system is implemented on the STM32F401CCU6 (Black Pill) development board. The internal clock is configured to 84MHz, with a 48MHz peripheral clock dedicated to the USB On-The-Go (OTG) Full Speed core.

| Component | STM32 Pin | Configuration | Function |
| :--- | :--- | :--- | :--- |
| LED 0-7 | PA0 - PA7 | GPIO Output Push-Pull | 8-bit LED Array |
| Potentiometer | PB0 | ADC1_IN8 | Analog Input (12-bit Resolution) |
| Mode Button | PB12 | GPIO Input (Pull-up) | Operation Mode Selection |
| Interrupt Button| PB13 | GPIO EXTI Falling Edge| 5-Second Global Override |
| AI Toggle | PB14 | GPIO Input (Pull-up) | AI Listener Enable/Disable |
| USB Interface | PA11 (D-), PA12 (D+)| USB_OTG_FS (CDC) | Native VCP Communication |

## 4. Software Dependencies

### 4.1 Firmware Environment
- **Toolchain**: STM32CubeIDE.
- **Library**: STM32Cube MCU Package for F4 Series (HAL - Hardware Abstraction Layer).
- **USB Stack**: Middleware USB Device Library (Communication Device Class).

### 4.2 Python Bridge Environment
- **Runtime**: Python 3.10+ (Tested on Fedora Linux).
- **Required Libraries**:
  - `pyserial`: Serial communication with the VCP.
  - `google-generativeai`: Interface for Gemini 2.5 Flash API.
  - `SpeechRecognition`: Audio-to-text processing.
  - `PyAudio`: Real-time microphone stream management.
  - `python-dotenv`: Environment variable management for API credentials.

## 5. Setup and Installation Instructions

### 5.1 Firmware Deployment
1. Open the `STM32_Firmware` project directory in STM32CubeIDE.
2. Ensure the `USB_DEVICE` middleware is correctly linked in the project properties.
3. Build the project to generate the `.elf` or `.bin` file.
4. Flash the firmware to the STM32F401CCU6 using an ST-LINK debugger or via DFU bootloader.

### 5.2 Python Bridge Configuration
1. Navigate to the `Python_Bridge` directory.
2. Create a virtual environment:
   ```bash
   python3 -m venv venv
   source venv/bin/activate
   ```
3. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```
4. Configure the environment:
   Create a `.env` file in the root directory and append your Google Gemini API Key:
   ```env
   GEMINI_API_KEY=your_api_key_here
   ```

### 5.3 Execution
1. Connect the STM32 via the USB Type-C port.
2. Identify the device path (typically `/dev/ttyACM0` on Linux).
3. Execute the bridge script:
   ```bash
   python bridge.py
   ```

## 6. Operation Modes

The firmware implements a state-machine-driven logic controlled via PB12.

- **Mode 0: Standby**
  System remains in an idle state. All LEDs in the PA0-PA7 array are driven LOW.

- **Mode 1: Shift**
  Implements a cyclic bit-shift animation. Each LED activates sequentially with a 200ms delay maintained via `HAL_GetTick()`.

- **Mode 2: Sawtooth Generator**
  The MCU generates a digital sawtooth waveform. Values cycle between 0-68 and 0-58. These data points are stored in memory buffers, optimized for visualization via STM32CubeMonitor for real-time telemetry analysis.

- **Mode 3: ADC Volume Bar**
  The 12-bit ADC reads the voltage from the PB0 potentiometer. The value is mapped to an 8-level scale, lighting the PA0-PA7 array proportionally to represent an analog volume meter.

- **Mode 4: AI Listener**
  The system enters a passive listening state for USB CDC interrupts. The Python Bridge sends sentiment tokens based on voice input:
  - Token 'A': Triggers a high-frequency asynchronous "panic" blink.
  - Token 'C': Triggers a low-frequency "calm" alternating blink.
  - Token 'X': Resets the LED array to an OFF state.

- **Global Interrupt (PB13)**
  Triggering the EXTI13 line forces an immediate 5-second override. All LEDs are driven HIGH, and the main state machine execution is suspended until the timer expires, ensuring high-priority user feedback.

## 7. License

This project is licensed under the MIT License. See the LICENSE file for full legal text.
