from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, JSONResponse, StreamingResponse
from starlette.exceptions import HTTPException as StarletteHTTPException
from starlette.requests import Request
from pydantic import BaseModel
from typing import Optional, List
import math, time, random, json, os
from collections import deque
import anthropic

app = FastAPI(title="EnvMonitor IoT")
history = deque(maxlen=50)
chat_client = anthropic.Anthropic() if os.environ.get("ANTHROPIC_API_KEY") else None
CHAT_MODEL = "claude-haiku-4-5"

@app.exception_handler(StarletteHTTPException)
async def custom_http_exception_handler(request: Request, exc: StarletteHTTPException):
    if exc.status_code == 404:
        return FileResponse("app/static/404.html", status_code=404)
    return JSONResponse({"detail": exc.detail}, status_code=exc.status_code)

# Real device data: set by POST /data/sensor (ESP32 Receiver Gateway), read by GET /data/latest.
last_device_data = None
last_device_time = 0
DEVICE_FRESH_WINDOW = 30  # seconds — how long a device reading is considered "live"

# Real device data: set by POST /data/sensor (ESP32 Receiver Gateway), read by GET /data/latest.
last_device_data = None
last_device_time = 0
DEVICE_FRESH_WINDOW = 30  # seconds

# 1. DATA ANALYSIS & CATEGORIZATION FUNCTION
def analyze_sensor_data(d):
    # Analyze Soil Moisture
    soil_raw = d.get('soilAO', 4095)
    if soil_raw > 3500:
        d['soilStatus'] = "Very dry - Needs watering"
    elif soil_raw > 1500:
        d['soilStatus'] = "Ideal moisture"
    else:
        d['soilStatus'] = "Waterlogged"

    # Analyze UV Voltage
    uv_voltage = d.get('uvVoltage', 0)
    uv_index = int((uv_voltage or 0) / 0.1) # Assuming 0.1V roughly equals 1 UV Index
    d['uvIndex'] = min(uv_index, 11)
    if d['uvIndex'] <= 2:
        d['uvStatus'] = "Safe"
    elif d['uvIndex'] <= 5:
        d['uvStatus'] = "Moderate - Wear a hat"
    else:
        d['uvStatus'] = "High risk - Avoid direct sun"

    # Analyze Rain Sensor
    rain_raw = d.get('rainAO', 4095)
    if rain_raw < 2500:
        d['rainStatus'] = "Raining"
    else:
        d['rainStatus'] = "Clear"

    # Calculate Heat Index (Feels-like temperature)
    temp = d.get('temperature', 0)
    hum = d.get('humidity', 0)
    if temp is not None and hum is not None and temp >= 26:
        # Standard formula for Heat Index in Celsius
        d['heatIndex'] = round(temp + (0.5555 * (hum/100) * (temp - 14.5)), 1)
    else:
        d['heatIndex'] = temp

    return d

# 2. FETCH LATEST DATA (REPLACING FAKE DATA)
def latest_data():
    # If valid data has been received from the ESP32, return it
    if last_device_data is not None:
        return last_device_data
    
    # Fallback state: ESP32 hasn't connected yet
    return {
        "temperature": 0, "humidity": 0, "lux": 0,
        "rainDO": 1, "rainAO": 4095, "soilDO": 1, "soilAO": 4095,
        "uvVoltage": 0,
        "timestamp": "--:--:--", "date": "--/--/----",
        "source": "waiting",
        "soilStatus": "Waiting for ESP32 connection...",
        "rainStatus": "Waiting for ESP32 connection...",
        "uvIndex": 0, "uvStatus": "Waiting for ESP32...",
        "heatIndex": 0
    }

# (Keep your existing BaseModel classes here...)
class LoginData(BaseModel):
    email: str
    password: str

class SensorData(BaseModel):
    temperature: Optional[float] = None
    humidity:    Optional[float] = None
    lux:         Optional[float] = None
    rainDO:      Optional[int] = None
    rainAO:      Optional[int] = None
    soilDO:      Optional[int] = None
    soilAO:      Optional[int] = None
    uvVoltage:   Optional[float] = None

class ChatMessage(BaseModel):
    role: str
    content: str

class ChatRequest(BaseModel):
    messages: List[ChatMessage]

app.mount("/static", StaticFiles(directory="app/static"), name="static")

@app.get("/")
def landing():   return FileResponse("app/static/landing.html")

@app.get("/login")
def login_page(): return FileResponse("app/static/login.html")

@app.get("/dashboard")
def dashboard():  return FileResponse("app/static/dashboard.html")

@app.get("/sensors")
def sensors_page(): return FileResponse("app/static/sensors.html")

@app.get("/alerts")
def alerts_page(): return FileResponse("app/static/alerts.html")

@app.get("/charts")
def charts_page(): return FileResponse("app/static/charts.html")

@app.get("/settings")
def settings_page(): return FileResponse("app/static/settings.html")

@app.post("/auth/login")
def do_login(data: LoginData):
    name = data.email.split("@")[0].replace(".", " ").title() if "@" in data.email else "Demo User"
    return {"success": True, "user": {"name": name, "email": data.email, "role": "Admin"}}

@app.get("/data/latest")
def get_latest(): return latest_data()

@app.get("/data/history")
def get_history(): return list(history)

# 3. RECEIVE DATA FROM ESP32 GATEWAY
@app.post("/data/sensor")
def receive(data: SensorData):
    global last_device_data, last_device_time
    entry = {
        **data.model_dump(exclude_none=True),
        "timestamp": time.strftime("%H:%M:%S"),
        "date": time.strftime("%d/%m/%Y"),
        "source": "device",
    }
    
    # Process raw data through the analysis function before saving it
    entry = analyze_sensor_data(entry)
    
    last_device_data = entry
    last_device_time = time.time()
    history.append(entry)
    return {"status": "ok"}

def build_chat_system_prompt():
    d = latest_data()
    return (
        "You are the EnvMonitor Assistant, a friendly helper built into a small IoT "
        "environmental monitoring dashboard for a garden/farm. Answer questions about "
        "the current conditions and give short, practical, plain-language advice. "
        "Keep replies to 2-4 sentences unless the user asks for more detail. "
        "If a question has nothing to do with the dashboard or the garden, answer briefly "
        "and steer back to what you can help with.\n\n"
        f"Live sensor readings (source: {d.get('source')}, as of {d.get('timestamp')}):\n"
        f"- Temperature: {d.get('temperature')} °C\n"
        f"- Humidity: {d.get('humidity')} %\n"
        f"- Light: {d.get('lux')} lux\n"
        f"- Soil dryness (raw analog, higher = drier): {d.get('soilAO')}\n"
        f"- Rain sensor (raw analog): {d.get('rainAO')}\n"
        f"- UV sensor voltage: {d.get('uvVoltage')} V\n"
    )

@app.post("/api/chat")
def chat(req: ChatRequest):
    if chat_client is None:
        def missing_key():
            msg = "Chat is not configured yet — the server is missing an ANTHROPIC_API_KEY."
            yield f"data: {json.dumps({'text': msg})}\n\n"
            yield "data: [DONE]\n\n"
        return StreamingResponse(missing_key(), media_type="text/event-stream")

    system_prompt = build_chat_system_prompt()
    messages = [{"role": m.role, "content": m.content} for m in req.messages]

    def generate():
        try:
            with chat_client.messages.stream(
                model=CHAT_MODEL,
                max_tokens=1024,
                system=system_prompt,
                messages=messages,
            ) as stream:
                for text in stream.text_stream:
                    yield f"data: {json.dumps({'text': text})}\n\n"
        except anthropic.APIError as e:
            yield f"data: {json.dumps({'text': f'(Chat error: {e.message})'})}\n\n"
        yield "data: [DONE]\n\n"

    return StreamingResponse(generate(), media_type="text/event-stream")
