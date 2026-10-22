"""
Day 5 Overnight Forecast Handler
Retrieves and transmits day 5 overnight forecast data to Adafruit IO.
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

print("This is file: f8.py - Day 5 Overnight Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract day 5 overnight data
days_5 = safe_get(forecast, "forecast_days_5", {}, missing_labels, "forecast_days_5") or {}
days_5_ovNt_forecast = safe_get(days_5, "overnightForecast", {}, missing_labels, "days5.overnightForecast") or {}
days_5_ovNt_start = safe_get(days_5_ovNt_forecast, 'forecastStart', "0", missing_labels, "days5ovnt.forecastStart")
days_5_ovNt_end = safe_get(days_5_ovNt_forecast, 'forecastEnd', "0", missing_labels, "days5ovnt.forecastEnd")
days_5_ovNt_cloudCover = pct(safe_get(days_5_ovNt_forecast, 'cloudCover', 0, missing_labels, "days5ovnt.cloudCover"))
days_5_ovNt_conditionCode = safe_get(days_5_ovNt_forecast, 'conditionCode', "0", missing_labels, "days5ovnt.conditionCode")
days_5_ovNt_humidity = pct(safe_get(days_5_ovNt_forecast, 'humidity', 0, missing_labels, "days5ovnt.humidity"))
days_5_ovNt_precipitationAmount = safe_get(days_5_ovNt_forecast, 'precipitationAmount', 0, missing_labels, "days5ovnt.precipitationAmount")
days_5_ovNt_precipitationChance = pct(safe_get(days_5_ovNt_forecast, 'precipitationChance', 0, missing_labels, "days5ovnt.precipitationChance"))
days_5_ovNt_precipitationType = safe_get(days_5_ovNt_forecast, 'precipitationType', "0", missing_labels, "days5ovnt.precipitationType")
days_5_ovNt_snowfallAmount = safe_get(days_5_ovNt_forecast, 'snowfallAmount', 0, missing_labels, "days5ovnt.snowfallAmount")
days_5_ovNt_temperatureMax = tof(safe_get(days_5_ovNt_forecast, 'temperatureMax', 0, missing_labels, "days5ovnt.temperatureMax"))
days_5_ovNt_temperatureMin = tof(safe_get(days_5_ovNt_forecast, 'temperatureMin', 0, missing_labels, "days5ovnt.temperatureMin"))
days_5_ovNt_windDirection = windir(safe_get(days_5_ovNt_forecast, 'windDirection', 0, missing_labels, "days5ovnt.windDirection"))
days_5_ovNt_windGustSpeedMax = tomph(safe_get(days_5_ovNt_forecast, 'windGustSpeedMax', 0, missing_labels, "days5ovnt.windGustSpeedMax"))
days_5_ovNt_windSpeed = tomph(safe_get(days_5_ovNt_forecast, 'windSpeed', 0, missing_labels, "days5ovnt.windSpeed"))
days_5_ovNt_windSpeedMax = tomph(safe_get(days_5_ovNt_forecast, 'windSpeedMax', 0, missing_labels, "days5ovnt.windSpeedMax"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'days5ovntstart', 'days5ovntend', 'days5ovntcloudcover', 'days5ovntconditioncode',
    'days5ovnthumidity', 'days5ovntprecipitationamount', 'days5ovntprecipitationchance',
    'days5ovntprecipitationtype', 'days5ovntsnowfallamount', 'days5ovnttemperaturemax',
    'days5ovnttemperaturemin', 'days5ovntwinddirection', 'days5ovntwindgustspeedmax',
    'days5ovntwindspeed', 'days5ovntwindspeedmax', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f8.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending day 5 overnight forecast data...")
send_with_fallback(feeds['days5ovntstart']['key'], days_5_ovNt_start, "days5ovnt.forecastStart")
send_with_fallback(feeds['days5ovntend']['key'], days_5_ovNt_end, "days5ovnt.forecastEnd")
send_with_fallback(feeds['days5ovntcloudcover']['key'], days_5_ovNt_cloudCover, "days5ovnt.cloudCover")
send_with_fallback(feeds['days5ovntconditioncode']['key'], days_5_ovNt_conditionCode, "days5ovnt.conditionCode")
send_with_fallback(feeds['days5ovnthumidity']['key'], days_5_ovNt_humidity, "days5ovnt.humidity")
sp(io)
send_with_fallback(feeds['days5ovntprecipitationamount']['key'], days_5_ovNt_precipitationAmount, "days5ovnt.precipitationAmount")
send_with_fallback(feeds['days5ovntprecipitationchance']['key'], days_5_ovNt_precipitationChance, "days5ovnt.precipitationChance")
send_with_fallback(feeds['days5ovntprecipitationtype']['key'], days_5_ovNt_precipitationType, "days5ovnt.precipitationType")
send_with_fallback(feeds['days5ovntsnowfallamount']['key'], days_5_ovNt_snowfallAmount, "days5ovnt.snowfallAmount")
send_with_fallback(feeds['days5ovnttemperaturemax']['key'], days_5_ovNt_temperatureMax, "days5ovnt.temperatureMax")
sp(io)
send_with_fallback(feeds['days5ovnttemperaturemin']['key'], days_5_ovNt_temperatureMin, "days5ovnt.temperatureMin")
send_with_fallback(feeds['days5ovntwinddirection']['key'], days_5_ovNt_windDirection, "days5ovnt.windDirection")
send_with_fallback(feeds['days5ovntwindgustspeedmax']['key'], days_5_ovNt_windGustSpeedMax, "days5ovnt.windGustSpeedMax")
send_with_fallback(feeds['days5ovntwindspeed']['key'], days_5_ovNt_windSpeed, "days5ovnt.windSpeed")
send_with_fallback(feeds['days5ovntwindspeedmax']['key'], days_5_ovNt_windSpeedMax, "days5ovnt.windSpeedMax")
sp(io)

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f8.py")

time.sleep(SLEEP_DURATION_SHORT)
print("f8.py ran successfully")
supervisor.set_next_code_file("f8a.py")
supervisor.reload()
