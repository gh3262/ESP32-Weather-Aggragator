"""
Hour 24 Forecast Handler
Retrieves and transmits hour 24 forecast data to Adafruit IO.
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

print("This is file: f5.py - Hour 24 Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract hour 24 data
hours_24 = safe_get(forecast, "forecast_hours_24", {}, missing_labels, "forecast_hours_24") or {}
hours_24_Start = safe_get(hours_24, "forecastStart", "0", missing_labels, "hours24.forecastStart")
hours_24_cloudCover = pct(safe_get(hours_24, "cloudCover", 0, missing_labels, "hours24.cloudCover"))
hours_24_conditionCode = safe_get(hours_24, "conditionCode", "0", missing_labels, "hours24.conditionCode")
hours_24_daylight_raw = safe_get(hours_24, "daylight", None, missing_labels, "hours24.daylight")
if hours_24_daylight_raw is None:
    hours_24_daylight = "0"
elif hours_24_daylight_raw:
    hours_24_daylight = "It will be light"
else:
    hours_24_daylight = "It will be dark"
hours_24_humidity = pct(safe_get(hours_24, "humidity", 0, missing_labels, "hours24.humidity"))
hours_24_precipitationAmount = safe_get(hours_24, "precipitationAmount", 0, missing_labels, "hours24.precipitationAmount")
hours_24_precipitationChance = pct(safe_get(hours_24, "precipitationChance", 0, missing_labels, "hours24.precipitationChance"))
hours_24_precipitationType = safe_get(hours_24, "precipitationType", "0", missing_labels, "hours24.precipitationType")
hours_24_precipitationIntensity = safe_get(hours_24, "precipitationIntensity", 0, missing_labels, "hours24.precipitationIntensity")
hours_24_pressure = safe_get(hours_24, "pressure", 0, missing_labels, "hours24.pressure")
hours_24_pressureTrend = safe_get(hours_24, "pressureTrend", "0", missing_labels, "hours24.pressureTrend")
hours_24_snowfallIntensity = safe_get(hours_24, "snowfallIntensity", 0, missing_labels, "hours24.snowfallIntensity")
hours_24_snowfallAmount = safe_get(hours_24, "snowfallAmount", 0, missing_labels, "hours24.snowfallAmount")
hours_24_temperature = tof(safe_get(hours_24, "temperature", 0, missing_labels, "hours24.temperature"))
hours_24_temperatureApparent = tof(safe_get(hours_24, "temperatureApparent", 0, missing_labels, "hours24.temperatureApparent"))
hours_24_temperatureDewPoint = tof(safe_get(hours_24, "temperatureDewPoint", 0, missing_labels, "hours24.temperatureDewPoint"))
hours_24_uvIndex = safe_get(hours_24, "uvIndex", 0, missing_labels, "hours24.uvIndex")
hours_24_visibility = safe_get(hours_24, "visibility", 0, missing_labels, "hours24.visibility")
hours_24_windDirection = windir(safe_get(hours_24, "windDirection", 0, missing_labels, "hours24.windDirection"))
hours_24_windGust = tomph(safe_get(hours_24, "windGust", 0, missing_labels, "hours24.windGust"))
hours_24_windSpeed = tomph(safe_get(hours_24, "windSpeed", 0, missing_labels, "hours24.windSpeed"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'hours24start', 'hours24cloudcover', 'hours24conditioncode', 'hours24daylight',
    'hours24humidity', 'hours24precipitationamount', 'hours24precipitationchance',
    'hours24precipitationtype', 'hours24precipitationintensity', 'hours24pressure',
    'hours24pressuretrend', 'hours24snowfallintensity', 'hours24snowfallamount',
    'hours24temperature', 'hours24temperatureapparent', 'hours24temperaturedewpoint',
    'hours24uvindex', 'hours24visibility', 'hours24winddirection',
    'hours24windgust', 'hours24windspeed', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f5.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending hour 24 forecast data...")
send_with_fallback(feeds['hours24start']['key'], hours_24_Start, "hours24.forecastStart")
send_with_fallback(feeds['hours24cloudcover']['key'], hours_24_cloudCover, "hours24.cloudCover")
send_with_fallback(feeds['hours24conditioncode']['key'], hours_24_conditionCode, "hours24.conditionCode")
send_with_fallback(feeds['hours24daylight']['key'], hours_24_daylight, "hours24.daylight")
send_with_fallback(feeds['hours24humidity']['key'], hours_24_humidity, "hours24.humidity")
sp(io)
send_with_fallback(feeds['hours24precipitationamount']['key'], hours_24_precipitationAmount, "hours24.precipitationAmount")
send_with_fallback(feeds['hours24precipitationchance']['key'], hours_24_precipitationChance, "hours24.precipitationChance")
send_with_fallback(feeds['hours24precipitationtype']['key'], hours_24_precipitationType, "hours24.precipitationType")
send_with_fallback(feeds['hours24precipitationintensity']['key'], hours_24_precipitationIntensity, "hours24.precipitationIntensity")
send_with_fallback(feeds['hours24pressure']['key'], hours_24_pressure, "hours24.pressure")
sp(io)
send_with_fallback(feeds['hours24pressuretrend']['key'], hours_24_pressureTrend, "hours24.pressureTrend")
send_with_fallback(feeds['hours24snowfallintensity']['key'], hours_24_snowfallIntensity, "hours24.snowfallIntensity")
send_with_fallback(feeds['hours24snowfallamount']['key'], hours_24_snowfallAmount, "hours24.snowfallAmount")
send_with_fallback(feeds['hours24temperature']['key'], hours_24_temperature, "hours24.temperature")
sp(io)
send_with_fallback(feeds['hours24temperatureapparent']['key'], hours_24_temperatureApparent, "hours24.temperatureApparent")
send_with_fallback(feeds['hours24temperaturedewpoint']['key'], hours_24_temperatureDewPoint, "hours24.temperatureDewPoint")
send_with_fallback(feeds['hours24uvindex']['key'], hours_24_uvIndex, "hours24.uvIndex")
send_with_fallback(feeds['hours24visibility']['key'], hours_24_visibility, "hours24.visibility")
send_with_fallback(feeds['hours24winddirection']['key'], hours_24_windDirection, "hours24.windDirection")
sp(io)
send_with_fallback(feeds['hours24windgust']['key'], hours_24_windGust, "hours24.windGust")
send_with_fallback(feeds['hours24windspeed']['key'], hours_24_windSpeed, "hours24.windSpeed")

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f5.py")

time.sleep(SLEEP_DURATION_LONG)
print("f5.py ran successfully")
supervisor.set_next_code_file("code.py")
supervisor.reload()
