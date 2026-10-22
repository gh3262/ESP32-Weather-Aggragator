"""
Day 1 Overnight Forecast Handler
Retrieves and transmits day 1 overnight forecast data to Adafruit IO.
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
    SLEEP_DURATION_SHORT,
    pixel
)

print("This is file: f6.py - Day 1 Overnight Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract day 1 overnight data
days_1 = safe_get(forecast, "forecast_days_1", {}, missing_labels, "forecast_days_1") or {}
days_1_ovNt_forecast = safe_get(days_1, "overnightForecast", {}, missing_labels, "days1.overnightForecast") or {}
days_1_ovNt_start = safe_get(days_1_ovNt_forecast, 'forecastStart', "0", missing_labels, "days1ovnt.forecastStart")
days_1_ovNt_end = safe_get(days_1_ovNt_forecast, 'forecastEnd', "0", missing_labels, "days1ovnt.forecastEnd")
days_1_ovNt_cloudCover = pct(safe_get(days_1_ovNt_forecast, 'cloudCover', 0, missing_labels, "days1ovnt.cloudCover"))
days_1_ovNt_conditionCode = safe_get(days_1_ovNt_forecast, 'conditionCode', "0", missing_labels, "days1ovnt.conditionCode")
days_1_ovNt_humidity = pct(safe_get(days_1_ovNt_forecast, 'humidity', 0, missing_labels, "days1ovnt.humidity"))
days_1_ovNt_precipitationAmount = safe_get(days_1_ovNt_forecast, 'precipitationAmount', 0, missing_labels, "days1ovnt.precipitationAmount")
days_1_ovNt_precipitationChance = pct(safe_get(days_1_ovNt_forecast, 'precipitationChance', 0, missing_labels, "days1ovnt.precipitationChance"))
days_1_ovNt_precipitationType = safe_get(days_1_ovNt_forecast, 'precipitationType', "0", missing_labels, "days1ovnt.precipitationType")
days_1_ovNt_snowfallAmount = safe_get(days_1_ovNt_forecast, 'snowfallAmount', 0, missing_labels, "days1ovnt.snowfallAmount")
days_1_ovNt_temperatureMax = tof(safe_get(days_1_ovNt_forecast, 'temperatureMax', 0, missing_labels, "days1ovnt.temperatureMax"))
days_1_ovNt_temperatureMin = tof(safe_get(days_1_ovNt_forecast, 'temperatureMin', 0, missing_labels, "days1ovnt.temperatureMin"))
days_1_ovNt_windDirection = windir(safe_get(days_1_ovNt_forecast, 'windDirection', 0, missing_labels, "days1ovnt.windDirection"))
days_1_ovNt_windGustSpeedMax = tomph(safe_get(days_1_ovNt_forecast, 'windGustSpeedMax', 0, missing_labels, "days1ovnt.windGustSpeedMax"))
days_1_ovNt_windSpeed = tomph(safe_get(days_1_ovNt_forecast, 'windSpeed', 0, missing_labels, "days1ovnt.windSpeed"))
days_1_ovNt_windSpeedMax = tomph(safe_get(days_1_ovNt_forecast, 'windSpeedMax', 0, missing_labels, "days1ovnt.windSpeedMax"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'days1ovntstart', 'days1ovntend', 'days1ovntcloudcover', 'days1ovntconditioncode',
    'days1ovnthumidity', 'days1ovntprecipitationamount', 'days1ovntprecipitationchance',
    'days1ovntprecipitationtype', 'days1ovntsnowfallamount', 'days1ovnttemperaturemax',
    'days1ovnttemperaturemin', 'days1ovntwinddirection', 'days1ovntwindgustspeedmax',
    'days1ovntwindspeed', 'days1ovntwindspeedmax', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f6.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending day 1 overnight forecast data...")
send_with_fallback(feeds['days1ovntstart']['key'], days_1_ovNt_start, "days1ovnt.forecastStart")
send_with_fallback(feeds['days1ovntend']['key'], days_1_ovNt_end, "days1ovnt.forecastEnd")
send_with_fallback(feeds['days1ovntcloudcover']['key'], days_1_ovNt_cloudCover, "days1ovnt.cloudCover")
send_with_fallback(feeds['days1ovntconditioncode']['key'], days_1_ovNt_conditionCode, "days1ovnt.conditionCode")
send_with_fallback(feeds['days1ovnthumidity']['key'], days_1_ovNt_humidity, "days1ovnt.humidity")
sp(io)
send_with_fallback(feeds['days1ovntprecipitationamount']['key'], days_1_ovNt_precipitationAmount, "days1ovnt.precipitationAmount")
send_with_fallback(feeds['days1ovntprecipitationchance']['key'], days_1_ovNt_precipitationChance, "days1ovnt.precipitationChance")
send_with_fallback(feeds['days1ovntprecipitationtype']['key'], days_1_ovNt_precipitationType, "days1ovnt.precipitationType")
send_with_fallback(feeds['days1ovntsnowfallamount']['key'], days_1_ovNt_snowfallAmount, "days1ovnt.snowfallAmount")
send_with_fallback(feeds['days1ovnttemperaturemax']['key'], days_1_ovNt_temperatureMax, "days1ovnt.temperatureMax")
sp(io)
send_with_fallback(feeds['days1ovnttemperaturemin']['key'], days_1_ovNt_temperatureMin, "days1ovnt.temperatureMin")
send_with_fallback(feeds['days1ovntwinddirection']['key'], days_1_ovNt_windDirection, "days1ovnt.windDirection")
send_with_fallback(feeds['days1ovntwindgustspeedmax']['key'], days_1_ovNt_windGustSpeedMax, "days1ovnt.windGustSpeedMax")
send_with_fallback(feeds['days1ovntwindspeed']['key'], days_1_ovNt_windSpeed, "days1ovnt.windSpeed")
send_with_fallback(feeds['days1ovntwindspeedmax']['key'], days_1_ovNt_windSpeedMax, "days1ovnt.windSpeedMax")
sp(io)

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f6.py")

time.sleep(SLEEP_DURATION_SHORT)
print("f6.py ran successfully")
supervisor.set_next_code_file("f6a.py")
supervisor.reload()
