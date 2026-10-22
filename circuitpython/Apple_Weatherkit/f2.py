"""
Hour 2 Forecast Handler
Retrieves and transmits hour 2 forecast data to Adafruit IO.
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

print("This is file: f2.py - Hour 2 Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract hour 2 data
hours_2 = safe_get(forecast, "forecast_hours_2", {}, missing_labels, "forecast_hours_2") or {}
hours_2_Start = safe_get(hours_2, "forecastStart", "0", missing_labels, "hours2.forecastStart")
hours_2_cloudCover = pct(safe_get(hours_2, "cloudCover", 0, missing_labels, "hours2.cloudCover"))
hours_2_conditionCode = safe_get(hours_2, "conditionCode", "0", missing_labels, "hours2.conditionCode")
hours_2_daylight_raw = safe_get(hours_2, "daylight", None, missing_labels, "hours2.daylight")
if hours_2_daylight_raw is None:
    hours_2_daylight = "0"
elif hours_2_daylight_raw:
    hours_2_daylight = "It will be light"
else:
    hours_2_daylight = "It will be dark"
hours_2_humidity = pct(safe_get(hours_2, "humidity", 0, missing_labels, "hours2.humidity"))
hours_2_precipitationAmount = safe_get(hours_2, "precipitationAmount", 0, missing_labels, "hours2.precipitationAmount")
hours_2_precipitationChance = pct(safe_get(hours_2, "precipitationChance", 0, missing_labels, "hours2.precipitationChance"))
hours_2_precipitationType = safe_get(hours_2, "precipitationType", "0", missing_labels, "hours2.precipitationType")
hours_2_precipitationIntensity = safe_get(hours_2, "precipitationIntensity", 0, missing_labels, "hours2.precipitationIntensity")
hours_2_pressure = safe_get(hours_2, "pressure", 0, missing_labels, "hours2.pressure")
hours_2_pressureTrend = safe_get(hours_2, "pressureTrend", "0", missing_labels, "hours2.pressureTrend")
hours_2_snowfallIntensity = safe_get(hours_2, "snowfallIntensity", 0, missing_labels, "hours2.snowfallIntensity")
hours_2_snowfallAmount = safe_get(hours_2, "snowfallAmount", 0, missing_labels, "hours2.snowfallAmount")
hours_2_temperature = tof(safe_get(hours_2, "temperature", 0, missing_labels, "hours2.temperature"))
hours_2_temperatureApparent = tof(safe_get(hours_2, "temperatureApparent", 0, missing_labels, "hours2.temperatureApparent"))
hours_2_temperatureDewPoint = tof(safe_get(hours_2, "temperatureDewPoint", 0, missing_labels, "hours2.temperatureDewPoint"))
hours_2_uvIndex = safe_get(hours_2, "uvIndex", 0, missing_labels, "hours2.uvIndex")
hours_2_visibility = safe_get(hours_2, "visibility", 0, missing_labels, "hours2.visibility")
hours_2_windDirection = windir(safe_get(hours_2, "windDirection", 0, missing_labels, "hours2.windDirection"))
hours_2_windGust = tomph(safe_get(hours_2, "windGust", 0, missing_labels, "hours2.windGust"))
hours_2_windSpeed = tomph(safe_get(hours_2, "windSpeed", 0, missing_labels, "hours2.windSpeed"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'hours2start', 'hours2cloudcover', 'hours2conditioncode', 'hours2daylight',
    'hours2humidity', 'hours2precipitationamount', 'hours2precipitationchance',
    'hours2precipitationtype', 'hours2precipitationintensity', 'hours2pressure',
    'hours2pressuretrend', 'hours2snowfallintensity', 'hours2snowfallamount',
    'hours2temperature', 'hours2temperatureapparent', 'hours2temperaturedewpoint',
    'hours2uvindex', 'hours2visibility', 'hours2winddirection',
    'hours2windgust', 'hours2windspeed', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f2.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending hour 2 forecast data...")
send_with_fallback(feeds['hours2start']['key'], hours_2_Start, "hours2.forecastStart")
send_with_fallback(feeds['hours2cloudcover']['key'], hours_2_cloudCover, "hours2.cloudCover")
send_with_fallback(feeds['hours2conditioncode']['key'], hours_2_conditionCode, "hours2.conditionCode")
send_with_fallback(feeds['hours2daylight']['key'], hours_2_daylight, "hours2.daylight")
send_with_fallback(feeds['hours2humidity']['key'], hours_2_humidity, "hours2.humidity")
sp(io)
send_with_fallback(feeds['hours2precipitationamount']['key'], hours_2_precipitationAmount, "hours2.precipitationAmount")
send_with_fallback(feeds['hours2precipitationchance']['key'], hours_2_precipitationChance, "hours2.precipitationChance")
send_with_fallback(feeds['hours2precipitationtype']['key'], hours_2_precipitationType, "hours2.precipitationType")
send_with_fallback(feeds['hours2precipitationintensity']['key'], hours_2_precipitationIntensity, "hours2.precipitationIntensity")
send_with_fallback(feeds['hours2pressure']['key'], hours_2_pressure, "hours2.pressure")
sp(io)
send_with_fallback(feeds['hours2pressuretrend']['key'], hours_2_pressureTrend, "hours2.pressureTrend")
send_with_fallback(feeds['hours2snowfallintensity']['key'], hours_2_snowfallIntensity, "hours2.snowfallIntensity")
send_with_fallback(feeds['hours2snowfallamount']['key'], hours_2_snowfallAmount, "hours2.snowfallAmount")
send_with_fallback(feeds['hours2temperature']['key'], hours_2_temperature, "hours2.temperature")
sp(io)
send_with_fallback(feeds['hours2temperatureapparent']['key'], hours_2_temperatureApparent, "hours2.temperatureApparent")
send_with_fallback(feeds['hours2temperaturedewpoint']['key'], hours_2_temperatureDewPoint, "hours2.temperatureDewPoint")
send_with_fallback(feeds['hours2uvindex']['key'], hours_2_uvIndex, "hours2.uvIndex")
send_with_fallback(feeds['hours2visibility']['key'], hours_2_visibility, "hours2.visibility")
send_with_fallback(feeds['hours2winddirection']['key'], hours_2_windDirection, "hours2.windDirection")
sp(io)
send_with_fallback(feeds['hours2windgust']['key'], hours_2_windGust, "hours2.windGust")
send_with_fallback(feeds['hours2windspeed']['key'], hours_2_windSpeed, "hours2.windSpeed")

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f2.py")

time.sleep(SLEEP_DURATION_LONG)
print("f2.py ran successfully")
supervisor.set_next_code_file("code.py")
supervisor.reload()
