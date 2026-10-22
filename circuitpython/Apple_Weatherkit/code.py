"""
Main Weather Station Controller
Retrieves current weather and minute forecasts, manages file rotation via NVM.
"""
import time
import os
import supervisor
from microcontroller import nvm
from tm1637_display import TM1637Display
import board
from weather_helpers import (
    init_wifi_and_io,
    pct, tof, tomph, windir, mtom,
    sp, get_feeds_batch,
    set_stage_color, safe_get,
    make_send_with_fallback, log_missing_labels,
    LOCATION_ID,
    SLEEP_DURATION_SHORT,
    pixel
)

# Constants unique to code.py
MAX_NVM_VALUE = 74
DISPLAY_ROTATE_PERIOD = 2

runs = [
    1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 1, 2, 3,
    4, 5, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3,
    4, 5, 1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 6,
    7, 8, 1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 1,
    2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6,
    7, 8, 1, 2, 3, 4, 5, 6, 7, 8, 6
    ]

# File mapping for next run
FILE_MAP = {1: "f1.py", 2: "f2.py", 3: "f3.py", 4: "f4.py", 
            5: "f5.py", 6: "f6.py", 7: "f7.py", 8: "f8.py"}


def rotate_display(display, primary_value, run_number, duration_seconds, period_seconds=DISPLAY_ROTATE_PERIOD):
    """Alternate the LED display between the primary reading and the run number."""
    end_time = time.monotonic() + duration_seconds
    show_run = False
    while time.monotonic() < end_time:
        display.print(run_number if show_run else primary_value)
        show_run = not show_run
        time.sleep(period_seconds)

# Main execution starts here
print("This is file: code.py - Current Weather + File Rotation Controller")

# Initialize LED display
led_display = TM1637Display(board.SCL, board.SDA)

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

print("NVM: ", nvm[0])
if nvm[0] > MAX_NVM_VALUE:
    nvm[0] = 0
    next_run = 0
else:
    next_run = runs[nvm[0]]
        
print("Next Run: ", next_run)

set_stage_color('init')
print("********")
print("LED")
led_display.print("0000")



# Get weather forecast
set_stage_color('weather')
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()
# *** Current ***
current_forecast = safe_get(forecast, "current", {}, missing_labels, "forecast.current") or {}
current_asOf = safe_get(current_forecast, "asOf", "0", missing_labels, "current.asOf")
current_cloudCover = pct(safe_get(current_forecast, "cloudCover", 0, missing_labels, "current.cloudCover"))
current_cloudCoverLowAltPct = pct(safe_get(current_forecast, "cloudCoverLowAltPct", 0, missing_labels, "current.cloudCoverLowAltPct"))
current_cloudCoverMidAltPct = pct(safe_get(current_forecast, "cloudCoverMidAltPct", 0, missing_labels, "current.cloudCoverMidAltPct"))
current_cloudCoverHighAltPct = pct(safe_get(current_forecast, "cloudCoverHighAltPct", 0, missing_labels, "current.cloudCoverHighAltPct"))
current_conditionCode = safe_get(current_forecast, "conditionCode", "0", missing_labels, "current.conditionCode")
current_daylight_raw = safe_get(current_forecast, "daylight", None, missing_labels, "current.daylight")
if current_daylight_raw is None:
    current_daylight = "0"
elif current_daylight_raw == False:
    current_daylight = "Its dark"
else:
    current_daylight = "Its light"
current_humidity = pct(safe_get(current_forecast, "humidity", 0, missing_labels, "current.humidity"))
current_precipitationIntensity = safe_get(current_forecast, "precipitationIntensity", 0, missing_labels, "current.precipitationIntensity")
current_pressure = safe_get(current_forecast, "pressure", 0, missing_labels, "current.pressure")
sp(io)
current_pressureTrend = safe_get(current_forecast, "pressureTrend", "0", missing_labels, "current.pressureTrend")
current_temperature = tof(safe_get(current_forecast, "temperature", 0, missing_labels, "current.temperature"))
#led_display.print(current_temperature)
current_temperatureApparent = tof(safe_get(current_forecast, "temperatureApparent", 0, missing_labels, "current.temperatureApparent"))
current_temperatureDewPoint = tof(safe_get(current_forecast, "temperatureDewPoint", 0, missing_labels, "current.temperatureDewPoint"))
current_visibility = mtom(safe_get(current_forecast, "visibility", 0, missing_labels, "current.visibility"))
current_windDirectionDegrees = safe_get(current_forecast, "windDirection", 0, missing_labels, "current.windDirection")
current_windDirection = windir(current_windDirectionDegrees)
current_windGust = tomph(safe_get(current_forecast, "windGust", 0, missing_labels, "current.windGust"))
current_windSpeed = tomph(safe_get(current_forecast, "windSpeed", 0, missing_labels, "current.windSpeed"))
current_uv = safe_get(current_forecast, "uvIndex", 0, missing_labels, "current.uvIndex")
print("UV: ", current_uv)
primary_display_value = current_temperature
led_display.print(primary_display_value)
    
# *** Minutes 5 ***
minutes_5_forecast = safe_get(forecast, "forecast_minutes_5", {}, missing_labels, "forecast_minutes_5") or {}
m5_startTime = safe_get(minutes_5_forecast, "startTime", "0", missing_labels, "minutes5.startTime")
m5_precipitationChance = pct(safe_get(minutes_5_forecast, "precipitationChance", 0, missing_labels, "minutes5.precipitationChance"))
m5_precipitationIntensity = safe_get(minutes_5_forecast, "precipitationIntensity", 0, missing_labels, "minutes5.precipitationIntensity")
# *** Minutes 30 ***
minutes_30_forecast = safe_get(forecast, "forecast_minutes_30", {}, missing_labels, "forecast_minutes_30") or {}
m30_startTime = safe_get(minutes_30_forecast, "startTime", "0", missing_labels, "minutes30.startTime")
m30_precipitationChance = pct(safe_get(minutes_30_forecast, "precipitationChance", 0, missing_labels, "minutes30.precipitationChance"))
m30_precipitationIntensity = safe_get(minutes_30_forecast, "precipitationIntensity", 0, missing_labels, "minutes30.precipitationIntensity")

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'currentasof', 'currentcloudcover', 'currentcloudcoverlowaltpct',
    'currentcloudcovermidaltpct', 'currentcloudcoverhighaltpct',
    'currentconditioncode', 'currentdaylight', 'currenthumidity',
    'currentprecipitationintensity', 'currentpressure', 'currentpressuretrend',
    'currenttemperature', 'currenttemperatureapparent', 'currenttemperaturedewpoint',
    'currentvisibility', 'currentwinddirection', 'currentwinddegrees', 'currentuv',
    'currentwindgust', 'currentwindspeed',
    'm5starttime', 'm5precipitationchance', 'm5precipitationintensity',
    'm30starttime', 'm30precipitationchance', 'm30precipitationintensity', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    print("Missing Labels: ", missing_labels)
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "code.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all current weather data
set_stage_color('send')
print("Sending current weather data...")
send_with_fallback(feeds['currentasof']['key'], current_asOf, "current.asOf")
send_with_fallback(feeds['currentcloudcover']['key'], current_cloudCover, "current.cloudCover")
send_with_fallback(feeds['currentcloudcoverlowaltpct']['key'], current_cloudCoverLowAltPct, "current.cloudCoverLowAltPct")
sp(io)
send_with_fallback(feeds['currentcloudcovermidaltpct']['key'], current_cloudCoverMidAltPct, "current.cloudCoverMidAltPct")
send_with_fallback(feeds['currentcloudcoverhighaltpct']['key'], current_cloudCoverHighAltPct, "current.cloudCoverHighAltPct")
send_with_fallback(feeds['currentconditioncode']['key'], current_conditionCode, "current.conditionCode")
send_with_fallback(feeds['currentdaylight']['key'], current_daylight, "current.daylight")
send_with_fallback(feeds['currenthumidity']['key'], current_humidity, "current.humidity")
sp(io)
send_with_fallback(feeds['currentprecipitationintensity']['key'], current_precipitationIntensity, "current.precipitationIntensity")
send_with_fallback(feeds['currentpressure']['key'], current_pressure, "current.pressure")
send_with_fallback(feeds['currentpressuretrend']['key'], current_pressureTrend, "current.pressureTrend")
send_with_fallback(feeds['currenttemperature']['key'], current_temperature, "current.temperature")
send_with_fallback(feeds['currenttemperatureapparent']['key'], current_temperatureApparent, "current.temperatureApparent")
sp(io)
send_with_fallback(feeds['currenttemperaturedewpoint']['key'], current_temperatureDewPoint, "current.temperatureDewPoint")
send_with_fallback(feeds['currentvisibility']['key'], current_visibility, "current.visibility")
send_with_fallback(feeds['currentwinddegrees']['key'], current_windDirectionDegrees, "current.windDirection")
send_with_fallback(feeds['currentwinddirection']['key'], current_windDirection, "current.windDirection")
send_with_fallback(feeds['currentwindgust']['key'], current_windGust, "current.windGust")
send_with_fallback(feeds['currentwindspeed']['key'], current_windSpeed, "current.windSpeed")
send_with_fallback(feeds['currentuv']['key'], current_uv, "current.uvIndex")
sp(io)

# Send minute forecast data
print("Sending minute forecast data...")
send_with_fallback(feeds['m5starttime']['key'], m5_startTime, "minutes5.startTime")
send_with_fallback(feeds['m5precipitationchance']['key'], m5_precipitationChance, "minutes5.precipitationChance")
send_with_fallback(feeds['m5precipitationintensity']['key'], m5_precipitationIntensity, "minutes5.precipitationIntensity")
send_with_fallback(feeds['m30starttime']['key'], m30_startTime, "minutes30.startTime")
send_with_fallback(feeds['m30precipitationchance']['key'], m30_precipitationChance, "minutes30.precipitationChance")
send_with_fallback(feeds['m30precipitationintensity']['key'], m30_precipitationIntensity, "minutes30.precipitationIntensity")
sp(io)

# Determine next file to run using dictionary lookup
fn = FILE_MAP.get(next_run, "f8.py")
sp(io)

set_stage_color('complete')
#ipadr = f" My IP address: {wifi.radio.ipv4_address}")
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: code.py")
rotate_display(led_display, primary_display_value, next_run, SLEEP_DURATION_SHORT)
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Next: " + fn)

print(fn)
rotate_display(led_display, primary_display_value, next_run, 60)
print("Restarting")    
nvm[0] = nvm[0] + 1
supervisor.set_next_code_file(fn)
supervisor.reload()