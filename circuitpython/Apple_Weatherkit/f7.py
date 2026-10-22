"""
Day 2 Overnight Forecast Handler
Retrieves and transmits day 2 overnight forecast data to Adafruit IO.
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

print("This is file: f7.py - Day 2 Overnight Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract day 2 overnight data
days_2 = safe_get(forecast, "forecast_days_2", {}, missing_labels, "forecast_days_2") or {}
days_2_ovNt_forecast = safe_get(days_2, "overnightForecast", {}, missing_labels, "days2.overnightForecast") or {}
days_2_ovNt_start = safe_get(days_2_ovNt_forecast, 'forecastStart', "0", missing_labels, "days2ovnt.forecastStart")
days_2_ovNt_end = safe_get(days_2_ovNt_forecast, 'forecastEnd', "0", missing_labels, "days2ovnt.forecastEnd")
days_2_ovNt_cloudCover = pct(safe_get(days_2_ovNt_forecast, 'cloudCover', 0, missing_labels, "days2ovnt.cloudCover"))
days_2_ovNt_conditionCode = safe_get(days_2_ovNt_forecast, 'conditionCode', "0", missing_labels, "days2ovnt.conditionCode")
days_2_ovNt_humidity = pct(safe_get(days_2_ovNt_forecast, 'humidity', 0, missing_labels, "days2ovnt.humidity"))
days_2_ovNt_precipitationAmount = safe_get(days_2_ovNt_forecast, 'precipitationAmount', 0, missing_labels, "days2ovnt.precipitationAmount")
days_2_ovNt_precipitationChance = pct(safe_get(days_2_ovNt_forecast, 'precipitationChance', 0, missing_labels, "days2ovnt.precipitationChance"))
days_2_ovNt_precipitationType = safe_get(days_2_ovNt_forecast, 'precipitationType', "0", missing_labels, "days2ovnt.precipitationType")
days_2_ovNt_snowfallAmount = safe_get(days_2_ovNt_forecast, 'snowfallAmount', 0, missing_labels, "days2ovnt.snowfallAmount")
days_2_ovNt_temperatureMax = tof(safe_get(days_2_ovNt_forecast, 'temperatureMax', 0, missing_labels, "days2ovnt.temperatureMax"))
days_2_ovNt_temperatureMin = tof(safe_get(days_2_ovNt_forecast, 'temperatureMin', 0, missing_labels, "days2ovnt.temperatureMin"))
days_2_ovNt_windDirection = windir(safe_get(days_2_ovNt_forecast, 'windDirection', 0, missing_labels, "days2ovnt.windDirection"))
days_2_ovNt_windGustSpeedMax = tomph(safe_get(days_2_ovNt_forecast, 'windGustSpeedMax', 0, missing_labels, "days2ovnt.windGustSpeedMax"))
days_2_ovNt_windSpeed = tomph(safe_get(days_2_ovNt_forecast, 'windSpeed', 0, missing_labels, "days2ovnt.windSpeed"))
days_2_ovNt_windSpeedMax = tomph(safe_get(days_2_ovNt_forecast, 'windSpeedMax', 0, missing_labels, "days2ovnt.windSpeedMax"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'days2ovntstart', 'days2ovntend', 'days2ovntcloudcover', 'days2ovntconditioncode',
    'days2ovnthumidity', 'days2ovntprecipitationamount', 'days2ovntprecipitationchance',
    'days2ovntprecipitationtype', 'days2ovntsnowfallamount', 'days2ovnttemperaturemax',
    'days2ovnttemperaturemin', 'days2ovntwinddirection', 'days2ovntwindgustspeedmax',
    'days2ovntwindspeed', 'days2ovntwindspeedmax', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f7.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending day 2 overnight forecast data...")
send_with_fallback(feeds['days2ovntstart']['key'], days_2_ovNt_start, "days2ovnt.forecastStart")
send_with_fallback(feeds['days2ovntend']['key'], days_2_ovNt_end, "days2ovnt.forecastEnd")
send_with_fallback(feeds['days2ovntcloudcover']['key'], days_2_ovNt_cloudCover, "days2ovnt.cloudCover")
send_with_fallback(feeds['days2ovntconditioncode']['key'], days_2_ovNt_conditionCode, "days2ovnt.conditionCode")
send_with_fallback(feeds['days2ovnthumidity']['key'], days_2_ovNt_humidity, "days2ovnt.humidity")
sp(io)
send_with_fallback(feeds['days2ovntprecipitationamount']['key'], days_2_ovNt_precipitationAmount, "days2ovnt.precipitationAmount")
send_with_fallback(feeds['days2ovntprecipitationchance']['key'], days_2_ovNt_precipitationChance, "days2ovnt.precipitationChance")
send_with_fallback(feeds['days2ovntprecipitationtype']['key'], days_2_ovNt_precipitationType, "days2ovnt.precipitationType")
send_with_fallback(feeds['days2ovntsnowfallamount']['key'], days_2_ovNt_snowfallAmount, "days2ovnt.snowfallAmount")
send_with_fallback(feeds['days2ovnttemperaturemax']['key'], days_2_ovNt_temperatureMax, "days2ovnt.temperatureMax")
sp(io)
send_with_fallback(feeds['days2ovnttemperaturemin']['key'], days_2_ovNt_temperatureMin, "days2ovnt.temperatureMin")
send_with_fallback(feeds['days2ovntwinddirection']['key'], days_2_ovNt_windDirection, "days2ovnt.windDirection")
send_with_fallback(feeds['days2ovntwindgustspeedmax']['key'], days_2_ovNt_windGustSpeedMax, "days2ovnt.windGustSpeedMax")
send_with_fallback(feeds['days2ovntwindspeed']['key'], days_2_ovNt_windSpeed, "days2ovnt.windSpeed")
send_with_fallback(feeds['days2ovntwindspeedmax']['key'], days_2_ovNt_windSpeedMax, "days2ovnt.windSpeedMax")
sp(io)

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f7.py")

time.sleep(SLEEP_DURATION_SHORT)
print("f7.py ran successfully")
supervisor.set_next_code_file("f7a.py")
supervisor.reload()
