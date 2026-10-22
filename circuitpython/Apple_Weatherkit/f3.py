"""
Hour 6 Forecast Handler
Retrieves and transmits hour 6 forecast data to Adafruit IO.
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

print("This is file: f3.py - Hour 6 Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract hour 6 data
hours_6 = safe_get(forecast, "forecast_hours_6", {}, missing_labels, "forecast_hours_6") or {}
hours_6_Start = safe_get(hours_6, "forecastStart", "0", missing_labels, "hours6.forecastStart")
hours_6_cloudCover = pct(safe_get(hours_6, "cloudCover", 0, missing_labels, "hours6.cloudCover"))
hours_6_conditionCode = safe_get(hours_6, "conditionCode", "0", missing_labels, "hours6.conditionCode")
hours_6_daylight_raw = safe_get(hours_6, "daylight", None, missing_labels, "hours6.daylight")
if hours_6_daylight_raw is None:
    hours_6_daylight = "0"
elif hours_6_daylight_raw:
    hours_6_daylight = "It will be light"
else:
    hours_6_daylight = "It will be dark"
hours_6_humidity = pct(safe_get(hours_6, "humidity", 0, missing_labels, "hours6.humidity"))
hours_6_precipitationAmount = safe_get(hours_6, "precipitationAmount", 0, missing_labels, "hours6.precipitationAmount")
hours_6_precipitationChance = pct(safe_get(hours_6, "precipitationChance", 0, missing_labels, "hours6.precipitationChance"))
hours_6_precipitationType = safe_get(hours_6, "precipitationType", "0", missing_labels, "hours6.precipitationType")
hours_6_precipitationIntensity = safe_get(hours_6, "precipitationIntensity", 0, missing_labels, "hours6.precipitationIntensity")
hours_6_pressure = safe_get(hours_6, "pressure", 0, missing_labels, "hours6.pressure")
hours_6_pressureTrend = safe_get(hours_6, "pressureTrend", "0", missing_labels, "hours6.pressureTrend")
hours_6_snowfallIntensity = safe_get(hours_6, "snowfallIntensity", 0, missing_labels, "hours6.snowfallIntensity")
hours_6_snowfallAmount = safe_get(hours_6, "snowfallAmount", 0, missing_labels, "hours6.snowfallAmount")
hours_6_temperature = tof(safe_get(hours_6, "temperature", 0, missing_labels, "hours6.temperature"))
hours_6_temperatureApparent = tof(safe_get(hours_6, "temperatureApparent", 0, missing_labels, "hours6.temperatureApparent"))
hours_6_temperatureDewPoint = tof(safe_get(hours_6, "temperatureDewPoint", 0, missing_labels, "hours6.temperatureDewPoint"))
hours_6_uvIndex = safe_get(hours_6, "uvIndex", 0, missing_labels, "hours6.uvIndex")
hours_6_visibility = safe_get(hours_6, "visibility", 0, missing_labels, "hours6.visibility")
hours_6_windDirection = windir(safe_get(hours_6, "windDirection", 0, missing_labels, "hours6.windDirection"))
hours_6_windGust = tomph(safe_get(hours_6, "windGust", 0, missing_labels, "hours6.windGust"))
hours_6_windSpeed = tomph(safe_get(hours_6, "windSpeed", 0, missing_labels, "hours6.windSpeed"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'hours6start', 'hours6cloudcover', 'hours6conditioncode', 'hours6daylight',
    'hours6humidity', 'hours6precipitationamount', 'hours6precipitationchance',
    'hours6precipitationtype', 'hours6precipitationintensity', 'hours6pressure',
    'hours6pressuretrend', 'hours6snowfallintensity', 'hours6snowfallamount',
    'hours6temperature', 'hours6temperatureapparent', 'hours6temperaturedewpoint',
    'hours6uvindex', 'hours6visibility', 'hours6winddirection',
    'hours6windgust', 'hours6windspeed', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f3.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending hour 6 forecast data...")
send_with_fallback(feeds['hours6start']['key'], hours_6_Start, "hours6.forecastStart")
send_with_fallback(feeds['hours6cloudcover']['key'], hours_6_cloudCover, "hours6.cloudCover")
send_with_fallback(feeds['hours6conditioncode']['key'], hours_6_conditionCode, "hours6.conditionCode")
send_with_fallback(feeds['hours6daylight']['key'], hours_6_daylight, "hours6.daylight")
send_with_fallback(feeds['hours6humidity']['key'], hours_6_humidity, "hours6.humidity")
sp(io)
send_with_fallback(feeds['hours6precipitationamount']['key'], hours_6_precipitationAmount, "hours6.precipitationAmount")
send_with_fallback(feeds['hours6precipitationchance']['key'], hours_6_precipitationChance, "hours6.precipitationChance")
send_with_fallback(feeds['hours6precipitationtype']['key'], hours_6_precipitationType, "hours6.precipitationType")
send_with_fallback(feeds['hours6precipitationintensity']['key'], hours_6_precipitationIntensity, "hours6.precipitationIntensity")
send_with_fallback(feeds['hours6pressure']['key'], hours_6_pressure, "hours6.pressure")
sp(io)
send_with_fallback(feeds['hours6pressuretrend']['key'], hours_6_pressureTrend, "hours6.pressureTrend")
send_with_fallback(feeds['hours6snowfallintensity']['key'], hours_6_snowfallIntensity, "hours6.snowfallIntensity")
send_with_fallback(feeds['hours6snowfallamount']['key'], hours_6_snowfallAmount, "hours6.snowfallAmount")
send_with_fallback(feeds['hours6temperature']['key'], hours_6_temperature, "hours6.temperature")
sp(io)
send_with_fallback(feeds['hours6temperatureapparent']['key'], hours_6_temperatureApparent, "hours6.temperatureApparent")
send_with_fallback(feeds['hours6temperaturedewpoint']['key'], hours_6_temperatureDewPoint, "hours6.temperatureDewPoint")
send_with_fallback(feeds['hours6uvindex']['key'], hours_6_uvIndex, "hours6.uvIndex")
send_with_fallback(feeds['hours6visibility']['key'], hours_6_visibility, "hours6.visibility")
send_with_fallback(feeds['hours6winddirection']['key'], hours_6_windDirection, "hours6.windDirection")
sp(io)
send_with_fallback(feeds['hours6windgust']['key'], hours_6_windGust, "hours6.windGust")
send_with_fallback(feeds['hours6windspeed']['key'], hours_6_windSpeed, "hours6.windSpeed")

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f3.py")

time.sleep(SLEEP_DURATION_LONG)
print("f3.py ran successfully")
supervisor.set_next_code_file("code.py")
supervisor.reload()
