import asyncio
import os
import serial
import time
import threading
import queue
import json
from typing import Optional

import google.generativeai as genai
import speech_recognition as sr
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from dotenv import load_dotenv

load_dotenv()
API_KEY = os.getenv("GEMINI_API_KEY")
if not API_KEY:
    raise RuntimeError("GEMINI_API_KEY not found in .env")

genai.configure(api_key=API_KEY)
model = genai.GenerativeModel("gemini-2.5-flash")

SERIAL_PORT = os.getenv("SERIAL_PORT", "/dev/ttyACM0")
BAUD_RATE = int(os.getenv("BAUD_RATE", "115200"))

SYSTEM_PROMPT = """
Analyze user sentiment for LED control.
Respond ONLY with one of these letters:
- A: Panic, anger, or danger
- C: Relaxed, peaceful, or thinking
- X: Off, sleep, or dark
"""

COMMAND_LABELS = {
    "A": {"label": "Panic / Bahaya", "color": "red"},
    "C": {"label": "Santai / Fokus", "color": "cyan"},
    "X": {"label": "Mati / Tidur",   "color": "gray"},
}

app = FastAPI(title="STM32 AI Voice Bridge")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ---------- State ----------
stm32: Optional[serial.Serial] = None
stm32_connected = False
stm32_lock = threading.Lock()

active_connections: list[WebSocket] = []
is_listening = False
listen_thread: Optional[threading.Thread] = None
stop_event = threading.Event()
message_queue: queue.Queue = queue.Queue()

# ---------- Serial helpers ----------
def connect_serial():
    global stm32, stm32_connected
    try:
        s = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        welcome = ""
        if s.in_waiting > 0:
            welcome = s.read(s.in_waiting).decode("utf-8", errors="ignore").strip()
        with stm32_lock:
            stm32 = s
            stm32_connected = True
        return True, welcome
    except Exception as e:
        return False, str(e)

def send_command(cmd: str) -> str:
    global stm32, stm32_connected
    with stm32_lock:
        if not stm32_connected or stm32 is None:
            return "STM32 tidak terhubung"
        try:
            stm32.write(cmd.encode("utf-8"))
            time.sleep(0.1)
            resp = ""
            if stm32.in_waiting > 0:
                resp = stm32.read(stm32.in_waiting).decode("utf-8", errors="ignore").strip()
            return resp or "OK"
        except Exception as e:
            stm32_connected = False
            return f"Error: {e}"

# ---------- Broadcast helpers ----------
async def broadcast(payload: dict):
    dead = []
    for ws in active_connections:
        try:
            await ws.send_json(payload)
        except Exception:
            dead.append(ws)
    for ws in dead:
        active_connections.remove(ws)

def queue_broadcast(payload: dict):
    message_queue.put(payload)

# ---------- Listening loop (runs in thread) ----------
def listening_loop():
    recognizer = sr.Recognizer()
    queue_broadcast({"type": "status", "msg": "Mendengarkan... (ucapkan 'keluar' untuk berhenti)"})

    while not stop_event.is_set():
        try:
            with sr.Microphone() as source:
                recognizer.adjust_for_ambient_noise(source, duration=0.4)
                try:
                    audio = recognizer.listen(source, timeout=5, phrase_time_limit=10)
                except sr.WaitTimeoutError:
                    queue_broadcast({"type": "listening_pulse"})
                    continue

            queue_broadcast({"type": "status", "msg": "Memproses suara..."})
            try:
                text = recognizer.recognize_google(audio, language="id-ID")
            except sr.UnknownValueError:
                queue_broadcast({"type": "status", "msg": "Suara tidak dikenali, coba lagi..."})
                continue
            except sr.RequestError as e:
                queue_broadcast({"type": "error", "msg": f"Layanan speech error: {e}"})
                continue

            queue_broadcast({"type": "transcript", "text": text})

            # Stop command
            if any(kw in text.lower() for kw in ["keluar", "matikan sistem", "stop", "berhenti"]):
                queue_broadcast({"type": "status", "msg": "Sistem dihentikan oleh pengguna."})
                queue_broadcast({"type": "listening_stopped"})
                break

            # Gemini
            try:
                queue_broadcast({"type": "status", "msg": "Menganalisis dengan Gemini AI..."})
                full_prompt = f"{SYSTEM_PROMPT}\nInput: {text}"
                response = model.generate_content(full_prompt)
                ai_cmd = response.text.strip().upper()

                if ai_cmd not in ["A", "C", "X"]:
                    queue_broadcast({"type": "error", "msg": f"Format AI tidak valid: {ai_cmd}"})
                    continue

                info = COMMAND_LABELS[ai_cmd]
                queue_broadcast({
                    "type": "ai_command",
                    "command": ai_cmd,
                    "label": info["label"],
                    "color": info["color"],
                })

                stm32_resp = send_command(ai_cmd)
                queue_broadcast({
                    "type": "stm32_response",
                    "response": stm32_resp,
                    "command": ai_cmd,
                })
                queue_broadcast({"type": "status", "msg": "Mendengarkan..."})

            except Exception as e:
                queue_broadcast({"type": "error", "msg": f"Gemini error: {e}"})

        except Exception as e:
            queue_broadcast({"type": "error", "msg": f"Mic error: {e}"})
            break

    global is_listening
    is_listening = False
    queue_broadcast({"type": "listening_stopped"})

# ---------- Background task: drain queue -> broadcast ----------
async def drain_queue():
    while True:
        await asyncio.sleep(0.05)
        while not message_queue.empty():
            payload = message_queue.get_nowait()
            await broadcast(payload)

@app.on_event("startup")
async def startup():
    ok, msg = connect_serial()
    if ok:
        print(f"[STM32] Connected. {msg}")
    else:
        print(f"[STM32] Not connected: {msg}")
    asyncio.create_task(drain_queue())

# ---------- WebSocket endpoint ----------
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    # Pindahkan deklarasi global ke baris pertama di dalam fungsi
    global is_listening, listen_thread, stop_event
    
    await websocket.accept()
    active_connections.append(websocket)

    # Send current state on connect
    await websocket.send_json({
        "type": "init",
        "stm32_connected": stm32_connected,
        "is_listening": is_listening,
        "serial_port": SERIAL_PORT,
    })

    try:
        while True:
            data = await websocket.receive_json()
            action = data.get("action")

            if action == "start_listen":
                if not is_listening:
                    is_listening = True
                    stop_event.clear()
                    listen_thread = threading.Thread(target=listening_loop, daemon=True)
                    listen_thread.start()
                    await broadcast({"type": "listening_started"})

            elif action == "stop_listen":
                stop_event.set()
                await broadcast({"type": "status", "msg": "Menghentikan..."})

            elif action == "send_command":
                cmd = data.get("command", "").upper()
                if cmd in ["A", "C", "X"]:
                    resp = send_command(cmd)
                    info = COMMAND_LABELS[cmd]
                    await broadcast({
                        "type": "ai_command",
                        "command": cmd,
                        "label": info["label"],
                        "color": info["color"],
                    })
                    await broadcast({"type": "stm32_response", "response": resp, "command": cmd})

            elif action == "reconnect_serial":
                ok, msg = connect_serial()
                await broadcast({
                    "type": "init",
                    "stm32_connected": stm32_connected,
                    "is_listening": is_listening,
                    "serial_port": SERIAL_PORT,
                })
                await broadcast({
                    "type": "status",
                    "msg": f"Serial {'terhubung' if ok else 'gagal'}: {msg}",
                })

    except WebSocketDisconnect:
        active_connections.remove(websocket)
                       

# ---------- Serve frontend ----------
app.mount("/static", StaticFiles(directory="."), name="static")

@app.get("/")
async def serve_index():
    return FileResponse("index.html")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("backend:app", host="0.0.0.0", port=8000, reload=False)