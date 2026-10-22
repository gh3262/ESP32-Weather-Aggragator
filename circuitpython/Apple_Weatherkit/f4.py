"""
Hour 12 Forecast Handler
Retrieves and transmits hour 12 forecast data to Adafruit IO.
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

print("This is file: f4.py - Hour 12 Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract hour 12 data
hours_12 = safe_get(forecast, "forecast_hours_12", {}, missing_labels, "forecast_hours_12") or {}
hours_12_Start = safe_get(hours_12, "forecastStart", "0", missing_labels, "hours12.forecastStart")
hours_12_cloudCover = pct(safe_get(hours_12, "cloudCover", 0, missing_labels, "hours12.cloudCover"))
hours_12_conditionCode = safe_get(hours_12, "conditionCode", "0", missing_labels, "hours12.conditionCode")
hours_12_daylight_raw = safe_get(hours_12, "daylight", None, missing_labels, "hours12.daylight")
if hours_12_daylight_raw is None:
    hours_12_daylight = "0"
elif hours_12_daylight_raw:
    hours_12_daylight = "It will be light"
else:
    hours_12_daylight = "It will be dark"
hours_12_humidity = pct(safe_get(hours_12, "humidity", 0, missing_labels, "hours12.humidity"))
hours_12_precipitationAmount = safe_get(hours_12, "precipitationAmount", 0, missing_labels, "hours12.precipitationAmount")
hours_12_precipitationChance = pct(safe_get(hours_12, "precipitationChance", 0, missing_labels, "hours12.precipitationChance"))
hours_12_precipitationType = safe_get(hours_12, "precipitationType", "0", missing_labels, "hours12.precipitationType")
hours_12_precipitationIntensity = safe_get(hours_12, "precipitationIntensity", 0, missing_labels, "hours12.precipitationIntensity")
hours_12_pressure = safe_get(hours_12, "pressure", 0, missing_labels, "hours12.pressure")
hours_12_pressureTrend = safe_get(hours_12, "pressureTrend", "0", missing_labels, "hours12.pressureTrend")
hours_12_snowfallIntensity = safe_get(hours_12, "snowfallIntensity", 0, missing_labels, "hours12.snowfallIntensity")
hours_12_snowfallAmount = safe_get(hours_12, "snowfallAmount", 0, missing_labels, "hours12.snowfallAmount")
hours_12_temperature = tof(safe_get(hours_12, "temperature", 0, missing_labels, "hours12.temperature"))
hours_12_temperatureApparent = tof(safe_get(hours_12, "temperatureApparent", 0, missing_labels, "hours12.temperatureApparent"))
hours_12_temperatureDewPoint = tof(safe_get(hours_12, "temperatureDewPoint", 0, missing_labels, "hours12.temperatureDewPoint"))
hours_12_uvIndex = safe_get(hours_12, "uvIndex", 0, missing_labels, "hours12.uvIndex")
hours_12_visibility = safe_get(hours_12, "visibility", 0, missing_labels, "hours12.visibility")
hours_12_windDirection = windir(safe_get(hours_12, "windDirection", 0, missing_labels, "hours12.windDirection"))
hours_12_windGust = tomph(safe_get(hours_12, "windGust", 0, missing_labels, "hours12.windGust"))
hours_12_windSpeed = tomph(safe_get(hours_12, "windSpeed", 0, missing_labels, "hours12.windSpeed"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'hours12start', 'hours12cloudcover', 'hours12conditioncode', 'hours12daylight',
    'hours12humidity', 'hours12precipitationamount', 'hours12precipitationchance',
    'hours12precipitationtype', 'hours12precipitationintensity', 'hours12pressure',
    'hours12pressuretrend', 'hours12snowfallintensity', 'hours12snowfallamount',
    'hours12temperature', 'hours12temperatureapparent', 'hours12temperaturedewpoint',
    'hours12uvindex', 'hours12visibility', 'hours12winddirection',
    'hours12windgust', 'hours12windspeed', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f4.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending hour 12 forecast data...")
send_with_fallback(feeds['hours12start']['key'], hours_12_Start, "hours12.forecastStart")
send_with_fallback(feeds['hours12cloudcover']['key'], hours_12_cloudCover, "hours12.cloudCover")
send_with_fallback(feeds['hours12conditioncode']['key'], hours_12_conditionCode, "hours12.conditionCode")
send_with_fallback(feeds['hours12daylight']['key'], hours_12_daylight, "hours12.daylight")
send_with_fallback(feeds['hours12humidity']['key'], hours_12_humidity, "hours12.humidity")
sp(io)
send_with_fallback(feeds['hours12precipitationamount']['key'], hours_12_precipitationAmount, "hours12.precipitationAmount")
send_with_fallback(feeds['hours12precipitationchance']['key'], hours_12_precipitationChance, "hours12.precipitationChance")
send_with_fallback(feeds['hours12precipitationtype']['key'], hours_12_precipitationType, "hours12.precipitationType")
send_with_fallback(feeds['hours12precipitationintensity']['key'], hours_12_precipitationIntensity, "hours12.precipitationIntensity")
send_with_fallback(feeds['hours12pressure']['key'], hours_12_pressure, "hours12.pressure")
sp(io)
send_with_fallback(feeds['hours12pressuretrend']['key'], hours_12_pressureTrend, "hours12.pressureTrend")
send_with_fallback(feeds['hours12snowfallintensity']['key'], hours_12_snowfallIntensity, "hours12.snowfallIntensity")
send_with_fallback(feeds['hours12snowfallamount']['key'], hours_12_snowfallAmount, "hours12.snowfallAmount")
send_with_fallback(feeds['hours12temperature']['key'], hours_12_temperature, "hours12.temperature")
sp(io)
send_with_fallback(feeds['hours12temperatureapparent']['key'], hours_12_temperatureApparent, "hours12.temperatureApparent")
send_with_fallback(feeds['hours12temperaturedewpoint']['key'], hours_12_temperatureDewPoint, "hours12.temperatureDewPoint")
send_with_fallback(feeds['hours12uvindex']['key'], hours_12_uvIndex, "hours12.uvIndex")
send_with_fallback(feeds['hours12visibility']['key'], hours_12_visibility, "hours12.visibility")
send_with_fallback(feeds['hours12winddirection']['key'], hours_12_windDirection, "hours12.windDirection")
sp(io)
send_with_fallback(feeds['hours12windgust']['key'], hours_12_windGust, "hours12.windGust")
send_with_fallback(feeds['hours12windspeed']['key'], hours_12_windSpeed, "hours12.windSpeed")

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f4.py")

time.sleep(SLEEP_DURATION_LONG)
print("f4.py ran successfully")
supervisor.set_next_code_file("code.py")
supervisor.reload()
