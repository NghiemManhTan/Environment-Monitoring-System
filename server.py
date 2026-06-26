from flask import Flask, request, jsonify, render_template
import csv
import os
from datetime import datetime
from dotenv import load_dotenv

load_dotenv()
CSV_FILE = os.getenv('CSV_FILE_NAME', 'sensor_data.csv')
HOST = os.getenv('HOST', '0.0.0.0')
PORT = int(os.getenv('PORT', 5000))

app = Flask(__name__)

# Store the latest data in RAM for the Web interface
latest_data = {
    "temperature": 0, "humidity": 0, "lux": 0, 
    "rainAO": 4095, "soilAO": 4095, "uvVoltage": 0,
    "ai_analysis": "Waiting for data..."
}

if not os.path.exists(CSV_FILE):
    with open(CSV_FILE, mode='w', newline='', encoding='utf-8') as file:
        writer = csv.writer(file)
        writer.writerow(['Timestamp', 'Temperature', 'Humidity', 'Light_Lux', 'Rain_AO', 'Soil_AO', 'UV_Voltage'])

def analyze_environment(data):
    temp = float(data.get('temperature', 0))
    soil = int(data.get('soilAO', 4095))
    
    analysis = "Normal conditions."
    if soil > 3000 and temp > 30:
        analysis = "WARNING: Extremely dry soil and high temperature. Turn on the water pump immediately!"
    elif soil < 1000:
        analysis = "Soil is very moist, no need to water."
    elif temp > 35:
        analysis = "High temperature warning, risk of root wilting."
        
    return analysis

# 1. API to receive data from ESP32
@app.route('/api/sensor', methods=['POST'])
def receive_data():
    global latest_data
    try:
        data = request.get_json()
        now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # 1. Ghi log vào file text
        with open('system_log.txt', 'a') as log_file:
            log_file.write(f"[{now}] Data: {data}\n")
        
        # 2. Ghi vào file CSV
        with open(CSV_FILE, mode='a', newline='', encoding='utf-8') as file:
            writer = csv.writer(file)
            writer.writerow([now, data.get('temperature'), data.get('humidity'), data.get('lux'), data.get('rainAO'), data.get('soilAO'), data.get('uvVoltage')])
        
        # 3. Phân tích AI
        ai_result = analyze_environment(data)
        
        # 4. Cập nhật dữ liệu cho Web
        latest_data = data.copy()
        latest_data['ai_analysis'] = ai_result
        latest_data['timestamp'] = now
        
        print(f"[{now}] Đã nhận và phân tích: {ai_result}")
        
        # QUAN TRỌNG: Phải trả về JSON ở đây
        return jsonify({"status": "success"}), 200

    except Exception as e:
        print(f"Lỗi: {e}")
        return jsonify({"status": "error", "message": str(e)}), 400

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/latest')
def get_latest():
    return jsonify(latest_data)

if __name__ == '__main__':
    print(f"Server is running at http://{HOST}:{PORT}")
    app.run(host=HOST, port=PORT, debug=True)