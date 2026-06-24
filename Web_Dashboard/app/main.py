from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, JSONResponse, StreamingResponse
from starlette.exceptions import HTTPException as StarletteHTTPException
from starlette.requests import Request
from pydantic import BaseModel
from typing import Optional, List
import math, time, json, os
from collections import deque
import google.generativeai as genai

app = FastAPI(title="EnvMonitor IoT")
history = deque(maxlen=50)

# Configure Gemini API
gemini_api_key = os.environ.get("GEMINI_API_KEY")
if gemini_api_key:
    genai.configure(api_key=gemini_api_key)
CHAT_MODEL = "gemini-1.5-flash"

@app.exception_handler(StarletteHTTPException)
async def custom_http_exception_handler(request: Request, exc: StarletteHTTPException):
    if exc.status_code == 404:
        return FileResponse("app/static/404.html", status_code=404)
    return JSONResponse({"detail": exc.detail}, status_code=exc.status_code)

# Real device data
last_device_data = None
last_device_time = 0
DEVICE_FRESH_WINDOW = 30  # Seconds

# 1. DATA ANALYSIS & TRANSLATION FUNCTION
def analyze_sensor_data(d):
    # Analyze Soil Moisture
    soil_raw = d.get('soilAO', 4095)
    if soil_raw > 3500:
        d['soilStatus'] = "Very dry - Needs watering"
    elif soil_raw > 1500:
        d['soilStatus'] = "Ideal moisture"
    else:
        d['soilStatus'] = "Waterlogged"

    # Analyze UV Index
    uv_voltage = d.get('uvVoltage', 0)
    uv_index = int((uv_voltage or 0) / 0.1)
    d['uvIndex'] = min(uv_index, 11)
    if d['uvIndex'] <= 2:
        d['uvStatus'] = "Safe"
    elif d['uvIndex'] <= 5:
        d['uvStatus'] = "Moderate - Wear a hat"
    else:
        d['uvStatus'] = "High risk - Avoid direct sun"

    # Analyze Rain Status
    rain_raw = d.get('rainAO', 4095)
    if rain_raw < 2500:
        d['rainStatus'] = "Raining"
    else:
        d['rainStatus'] = "Clear"

    # Calculate Heat Index
    temp = d.get('temperature', 0)
    hum = d.get('humidity', 0)
    if temp is not None and hum is not None and temp >= 26:
        d['heatIndex'] = round(temp + (0.5555 * (hum/100) * (temp - 14.5)), 1)
    else:
        d['heatIndex'] = temp

    return d

# 2. GET LATEST DATA (FALLBACK STATE)
def latest_data():
    if last_device_data is not None:
        return last_device_data
    
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

@app.post("/data/sensor")
def receive(data: SensorData):
    global last_device_data, last_device_time
    entry = {
        **data.model_dump(exclude_none=True),
        "timestamp": time.strftime("%H:%M:%S"),
        "date": time.strftime("%d/%m/%Y"),
        "source": "device",
    }
    
    # Process raw data through the analysis function
    entry = analyze_sensor_data(entry)
    
    last_device_data = entry
    last_device_time = time.time()
    history.append(entry)
    return {"status": "ok"}

# 3. BUILD AI SYSTEM PROMPT
def build_chat_system_prompt():
    d = latest_data()
    return (
        "You are the smart Environmental Assistant of the EnvMonitor IoT system, "
        "powered by Google (Gemini) to help users monitor their garden. "
        "Absolutely DO NOT reply with raw technical numbers. Use natural language instead.\n"
        "- If the soil is dry, advise them to water it.\n"
        "- If it is raining, remind them to cover things up.\n"
        "- If the UV index is high, remind them to protect their skin.\n"
        "- Keep your answers extremely concise, friendly, and maximum 3 sentences long.\n\n"
        f"Current environmental conditions:\n"
        f"- Temperature: {d.get('temperature')}°C (Feels like: {d.get('heatIndex')}°C)\n"
        f"- Humidity: {d.get('humidity')}%\n"
        f"- Light: {d.get('lux')} lux\n"
        f"- Soil: {d.get('soilStatus')}\n"
        f"- Weather: {d.get('rainStatus')}\n"
        f"- UV: {d.get('uvStatus')}\n"
    )

# 4. GEMINI CHAT ENDPOINT
@app.post("/api/chat")
def chat(req: ChatRequest):
    if not gemini_api_key:
        def missing_key():
            msg = "AI not connected — Please set GEMINI_API_KEY on the server."
            yield f"data: {json.dumps({'text': msg})}\n\n"
            yield "data: [DONE]\n\n"
        return StreamingResponse(missing_key(), media_type="text/event-stream")

    system_prompt = build_chat_system_prompt()
    
    # Initialize the model with system instructions
    model = genai.GenerativeModel(
        model_name=CHAT_MODEL,
        system_instruction=system_prompt
    )

    # Convert message history to Gemini format (user / model)
    gemini_history = []
    if len(req.messages) > 1:
        for m in req.messages[:-1]:
            role = "model" if m.role in ["assistant", "model"] else "user"
            gemini_history.append({"role": role, "parts": [m.content]})
            
    last_message = req.messages[-1].content if req.messages else ""

    def generate():
        try:
            # Start chat session with historical context
            chat_session = model.start_chat(history=gemini_history)
            
            # Send latest message and stream the response
            response = chat_session.send_message(last_message, stream=True)
            for chunk in response:
                if chunk.text:
                    yield f"data: {json.dumps({'text': chunk.text})}\n\n"
        except Exception as e:
            yield f"data: {json.dumps({'text': f'(AI Connection Error: {str(e)})'})}\n\n"
        yield "data: [DONE]\n\n"

    return StreamingResponse(generate(), media_type="text/event-stream")
