"""
Hour 1 Forecast Handler
Retrieves and transmits hour 1 forecast data to Adafruit IO.
"""
import time
import supervisor
from weather_helpers import (
    init_wifi_and_io,
    pct, tof, tomph, windir,
    sp, get_feeds_batch,
    set_stage_color, safe_get,
    make_send_with_fallback, log_missing_labels,
    LOCATION_ID,
    SLEEP_DURATION_LONG,
    pixel
)

print("This is file: f1.py - Hour 1 Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract hour 1 data
hours_1 = safe_get(forecast, "forecast_hours_1", {}, missing_labels, "forecast_hours_1") or {}
hours_1_Start = safe_get(hours_1, "forecastStart", "0", missing_labels, "hours1.forecastStart")
hours_1_cloudCover = pct(safe_get(hours_1, "cloudCover", 0, missing_labels, "hours1.cloudCover"))
hours_1_conditionCode = safe_get(hours_1, "conditionCode", "0", missing_labels, "hours1.conditionCode")
hours_1_daylight_raw = safe_get(hours_1, "daylight", None, missing_labels, "hours1.daylight")
if hours_1_daylight_raw is None:
    hours_1_daylight = "0"
elif hours_1_daylight_raw:
    hours_1_daylight = "It will be light"
else:
    hours_1_daylight = "It will be dark"
hours_1_humidity = pct(safe_get(hours_1, "humidity", 0, missing_labels, "hours1.humidity"))
hours_1_precipitationAmount = safe_get(hours_1, "precipitationAmount", 0, missing_labels, "hours1.precipitationAmount")
hours_1_precipitationChance = pct(safe_get(hours_1, "precipitationChance", 0, missing_labels, "hours1.precipitationChance"))
hours_1_precipitationType = safe_get(hours_1, "precipitationType", "0", missing_labels, "hours1.precipitationType")
hours_1_precipitationIntensity = safe_get(hours_1, "precipitationIntensity", 0, missing_labels, "hours1.precipitationIntensity")
hours_1_pressure = safe_get(hours_1, "pressure", 0, missing_labels, "hours1.pressure")
hours_1_pressureTrend = safe_get(hours_1, "pressureTrend", "0", missing_labels, "hours1.pressureTrend")
hours_1_snowfallIntensity = safe_get(hours_1, "snowfallIntensity", 0, missing_labels, "hours1.snowfallIntensity")
hours_1_snowfallAmount = safe_get(hours_1, "snowfallAmount", 0, missing_labels, "hours1.snowfallAmount")
hours_1_temperature = tof(safe_get(hours_1, "temperature", 0, missing_labels, "hours1.temperature"))
hours_1_temperatureApparent = tof(safe_get(hours_1, "temperatureApparent", 0, missing_labels, "hours1.temperatureApparent"))
hours_1_temperatureDewPoint = tof(safe_get(hours_1, "temperatureDewPoint", 0, missing_labels, "hours1.temperatureDewPoint"))
hours_1_uvIndex = safe_get(hours_1, "uvIndex", 0, missing_labels, "hours1.uvIndex")
hours_1_visibility = safe_get(hours_1, "visibility", 0, missing_labels, "hours1.visibility")
hours_1_windDirection = windir(safe_get(hours_1, "windDirection", 0, missing_labels, "hours1.windDirection"))
hours_1_windGust = tomph(safe_get(hours_1, "windGust", 0, missing_labels, "hours1.windGust"))
hours_1_windSpeed = tomph(safe_get(hours_1, "windSpeed", 0, missing_labels, "hours1.windSpeed"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'hours1start', 'hours1cloudcover', 'hours1conditioncode', 'hours1daylight',
    'hours1humidity', 'hours1precipitationamount', 'hours1precipitationchance',
    'hours1precipitationtype', 'hours1precipitationintensity', 'hours1pressure',
    'hours1pressuretrend', 'hours1snowfallintensity', 'hours1snowfallamount',
    'hours1temperature', 'hours1temperatureapparent', 'hours1temperaturedewpoint',
    'hours1uvindex', 'hours1visibility', 'hours1winddirection',
    'hours1windgust', 'hours1windspeed', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f1.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending hour 1 forecast data...")
send_with_fallback(feeds['hours1start']['key'], hours_1_Start, "hours1.forecastStart")
send_with_fallback(feeds['hours1cloudcover']['key'], hours_1_cloudCover, "hours1.cloudCover")
send_with_fallback(feeds['hours1conditioncode']['key'], hours_1_conditionCode, "hours1.conditionCode")
send_with_fallback(feeds['hours1daylight']['key'], hours_1_daylight, "hours1.daylight")
send_with_fallback(feeds['hours1humidity']['key'], hours_1_humidity, "hours1.humidity")
sp(io)
send_with_fallback(feeds['hours1precipitationamount']['key'], hours_1_precipitationAmount, "hours1.precipitationAmount")
send_with_fallback(feeds['hours1precipitationchance']['key'], hours_1_precipitationChance, "hours1.precipitationChance")
send_with_fallback(feeds['hours1precipitationtype']['key'], hours_1_precipitationType, "hours1.precipitationType")
send_with_fallback(feeds['hours1precipitationintensity']['key'], hours_1_precipitationIntensity, "hours1.precipitationIntensity")
send_with_fallback(feeds['hours1pressure']['key'], hours_1_pressure, "hours1.pressure")
sp(io)
send_with_fallback(feeds['hours1pressuretrend']['key'], hours_1_pressureTrend, "hours1.pressureTrend")
send_with_fallback(feeds['hours1snowfallintensity']['key'], hours_1_snowfallIntensity, "hours1.snowfallIntensity")
send_with_fallback(feeds['hours1snowfallamount']['key'], hours_1_snowfallAmount, "hours1.snowfallAmount")
send_with_fallback(feeds['hours1temperature']['key'], hours_1_temperature, "hours1.temperature")
sp(io)
send_with_fallback(feeds['hours1temperatureapparent']['key'], hours_1_temperatureApparent, "hours1.temperatureApparent")
send_with_fallback(feeds['hours1temperaturedewpoint']['key'], hours_1_temperatureDewPoint, "hours1.temperatureDewPoint")
send_with_fallback(feeds['hours1uvindex']['key'], hours_1_uvIndex, "hours1.uvIndex")
send_with_fallback(feeds['hours1visibility']['key'], hours_1_visibility, "hours1.visibility")
send_with_fallback(feeds['hours1winddirection']['key'], hours_1_windDirection, "hours1.windDirection")
sp(io)
send_with_fallback(feeds['hours1windgust']['key'], hours_1_windGust, "hours1.windGust")
send_with_fallback(feeds['hours1windspeed']['key'], hours_1_windSpeed, "hours1.windSpeed")

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f1.py")

time.sleep(SLEEP_DURATION_LONG)
print("f1.py ran successfully")
supervisor.set_next_code_file("code.py")
supervisor.reload()
