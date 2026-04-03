import serial
import time
import os
import google.generativeai as genai
import speech_recognition as sr
from dotenv import load_dotenv

load_dotenv()
API_KEY = os.getenv("GEMINI_API_KEY")

if not API_KEY:
    print("Error: GEMINI_API_KEY not found in .env")
    exit()

genai.configure(api_key=API_KEY)
model = genai.GenerativeModel('gemini-2.5-flash')

# Serial Configuration
SERIAL_PORT = '/dev/ttyACM2'
BAUD_RATE = 115200

print(f"Connecting to STM32 on {SERIAL_PORT}...")
try:
    stm32 = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)
    print("Connected to hardware.\n")
    
    if stm32.in_waiting > 0:
        msg = stm32.read(stm32.in_waiting).decode('utf-8', errors='ignore')
        print(f"Hardware message: {msg.strip()}")
        
except Exception as e:
    print(f"Failed to connect to STM32: {e}")
    exit()

def listen_voice():
    recognizer = sr.Recognizer()
    with sr.Microphone() as source:
        print("\nListening... (Say 'keluar' to stop)")
        recognizer.adjust_for_ambient_noise(source, duration=0.5)
        try:
            audio = recognizer.listen(source, timeout=5, phrase_time_limit=10)
            print("Processing voice input...")
            return recognizer.recognize_google(audio, language="id-ID")
        except sr.WaitTimeoutError:
            print("No speech detected.")
            return None
        except sr.UnknownValueError:
            print("Unrecognized speech.")
            return None
        except sr.RequestError as e:
            print(f"Recognition service error: {e}")
            return None

system_prompt = """
Analyze user sentiment for LED control.
Respond ONLY with one of these letters:
- A: Panic, anger, or danger
- C: Relaxed, peaceful, or thinking
- X: Off, sleep, or dark
"""

print("-" * 30)
print("AI Voice Bridge Active")
print("-" * 30)

while True:
    user_input = listen_voice()
    
    if user_input is None:
        continue
        
    print(f"User input: {user_input}")
    
    if any(cmd in user_input.lower() for cmd in ['keluar', 'matikan sistem']):
        print("Shutting down...")
        break
        
    try:
        full_prompt = f"{system_prompt}\nInput: {user_input}"
        response = model.generate_content(full_prompt)
        ai_command = response.text.strip().upper()
        
        if ai_command in ['A', 'C', 'X']:
            print(f"AI Command: {ai_command}")
            stm32.write(ai_command.encode('utf-8'))
            
            time.sleep(0.1)
            if stm32.in_waiting > 0:
                resp = stm32.read(stm32.in_waiting).decode('utf-8', errors='ignore')
                print(f"STM32 response: {resp.strip()}")
        else:
            print(f"Invalid AI format: {ai_command}")
            
    except Exception as e:
         print(f"Gemini API error: {e}")
