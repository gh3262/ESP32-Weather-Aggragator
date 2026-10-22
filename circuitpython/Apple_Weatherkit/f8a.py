"""
Day 5 Full + Daytime Forecast Handler
Retrieves and transmits day 5 complete day and daytime forecast data to Adafruit IO.
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

print("This is file: f8a.py - Day 5 Full + Daytime Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract day 5 full day data
days_5 = safe_get(forecast, "forecast_days_5", {}, missing_labels, "forecast_days_5") or {}
days_5_Start = safe_get(days_5, "forecastStart", "0", missing_labels, "days5.forecastStart")
days_5_End = safe_get(days_5, "forecastEnd", "0", missing_labels, "days5.forecastEnd")
days_5_conditionCode = safe_get(days_5, "conditionCode", "0", missing_labels, "days5.conditionCode")
days_5_maxUvIndex = safe_get(days_5, "maxUvIndex", 0, missing_labels, "days5.maxUvIndex")
days_5_moonPhase = safe_get(days_5, "moonPhase", "0", missing_labels, "days5.moonPhase")
days_5_moonrise = safe_get(days_5, "moonrise", "0", missing_labels, "days5.moonrise")
days_5_moonset = safe_get(days_5, "moonset", "0", missing_labels, "days5.moonset")
days_5_precipitationAmount = safe_get(days_5, "precipitationAmount", 0, missing_labels, "days5.precipitationAmount")
days_5_precipitationChance = pct(safe_get(days_5, "precipitationChance", 0, missing_labels, "days5.precipitationChance"))
days_5_precipitationType = safe_get(days_5, "precipitationType", "0", missing_labels, "days5.precipitationType")
days_5_snowfallAmount = safe_get(days_5, "snowfallAmount", 0, missing_labels, "days5.snowfallAmount")
days_5_solarMidnight = safe_get(days_5, "solarMidnight", "0", missing_labels, "days5.solarMidnight")
days_5_solarNoon = safe_get(days_5, "solarNoon", "0", missing_labels, "days5.solarNoon")
days_5_sunrise = safe_get(days_5, "sunrise", "0", missing_labels, "days5.sunrise")
days_5_sunriseCivil = safe_get(days_5, "sunriseCivil", "0", missing_labels, "days5.sunriseCivil")
days_5_sunriseNautical = safe_get(days_5, "sunriseNautical", "0", missing_labels, "days5.sunriseNautical")
days_5_sunriseAstronomical = safe_get(days_5, "sunriseAstronomical", "0", missing_labels, "days5.sunriseAstronomical")
days_5_sunset = safe_get(days_5, "sunset", "0", missing_labels, "days5.sunset")
days_5_sunsetCivil = safe_get(days_5, "sunsetCivil", "0", missing_labels, "days5.sunsetCivil")
days_5_sunsetNautical = safe_get(days_5, "sunsetNautical", "0", missing_labels, "days5.sunsetNautical")
days_5_sunsetAstronomical = safe_get(days_5, "sunsetAstronomical", "0", missing_labels, "days5.sunsetAstronomical")
days_5_temperatureMax = tof(safe_get(days_5, "temperatureMax", 0, missing_labels, "days5.temperatureMax"))
days_5_temperatureMin = tof(safe_get(days_5, "temperatureMin", 0, missing_labels, "days5.temperatureMin"))
days_5_windGustSpeedMax = tomph(safe_get(days_5, "windGustSpeedMax", 0, missing_labels, "days5.windGustSpeedMax"))
days_5_windSpeedAvg = tomph(safe_get(days_5, "windSpeedAvg", 0, missing_labels, "days5.windSpeedAvg"))
days_5_windSpeedMax = tomph(safe_get(days_5, "windSpeedMax", 0, missing_labels, "days5.windSpeedMax"))

# Extract day 5 daytime data
days_5_daytime_forecast = safe_get(days_5, "daytimeForecast", {}, missing_labels, "days5.daytimeForecast") or {}
days_5_daytime_start = safe_get(days_5_daytime_forecast, 'forecastStart', "0", missing_labels, "days5daytime.forecastStart")
days_5_daytime_end = safe_get(days_5_daytime_forecast, 'forecastEnd', "0", missing_labels, "days5daytime.forecastEnd")
days_5_daytime_cloudCover = pct(safe_get(days_5_daytime_forecast, 'cloudCover', 0, missing_labels, "days5daytime.cloudCover"))
days_5_daytime_conditionCode = safe_get(days_5_daytime_forecast, 'conditionCode', "0", missing_labels, "days5daytime.conditionCode")
days_5_daytime_humidity = pct(safe_get(days_5_daytime_forecast, 'humidity', 0, missing_labels, "days5daytime.humidity"))
days_5_daytime_precipitationAmount = safe_get(days_5_daytime_forecast, 'precipitationAmount', 0, missing_labels, "days5daytime.precipitationAmount")
days_5_daytime_precipitationChance = pct(safe_get(days_5_daytime_forecast, 'precipitationChance', 0, missing_labels, "days5daytime.precipitationChance"))
days_5_daytime_precipitationType = safe_get(days_5_daytime_forecast, 'precipitationType', "0", missing_labels, "days5daytime.precipitationType")
days_5_daytime_snowfallAmount = safe_get(days_5_daytime_forecast, 'snowfallAmount', 0, missing_labels, "days5daytime.snowfallAmount")
days_5_daytime_temperatureMax = tof(safe_get(days_5_daytime_forecast, 'temperatureMax', 0, missing_labels, "days5daytime.temperatureMax"))
days_5_daytime_temperatureMin = tof(safe_get(days_5_daytime_forecast, 'temperatureMin', 0, missing_labels, "days5daytime.temperatureMin"))
days_5_daytime_windDirection = windir(safe_get(days_5_daytime_forecast, 'windDirection', 0, missing_labels, "days5daytime.windDirection"))
days_5_daytime_windGustSpeedMax = tomph(safe_get(days_5_daytime_forecast, 'windGustSpeedMax', 0, missing_labels, "days5daytime.windGustSpeedMax"))
days_5_daytime_windSpeed = tomph(safe_get(days_5_daytime_forecast, 'windSpeed', 0, missing_labels, "days5daytime.windSpeed"))
days_5_daytime_windSpeedMax = tomph(safe_get(days_5_daytime_forecast, 'windSpeedMax', 0, missing_labels, "days5daytime.windSpeedMax"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'days5start', 'days5end', 'days5conditioncode', 'days5maxuvindex', 'days5moonphase',
    'days5moonrise', 'days5moonset', 'days5precipitationamount', 'days5precipitationchance',
    'days5precipitationtype', 'days5snowfallamount', 'days5solarmidnight', 'days5solarnoon',
    'days5sunrise', 'days5sunrisecivil', 'days5sunrisenautical', 'days5sunriseastronomical',
    'days5sunset', 'days5sunsetcivil', 'days5sunsetnautical', 'days5sunsetastronomical',
    'days5temperaturemax', 'days5temperaturemin', 'days5windgustspeedmax', 'days5windspeedavg',
    'days5windspeedmax', 'days5daytimestart', 'days5daytimeend', 'days5daytimecloudcover',
    'days5daytimeconditioncode', 'days5daytimehumidity', 'days5daytimeprecipitationamount',
    'days5daytimeprecipitationchance', 'days5daytimeprecipitationtype', 'days5daytimesnowfallamount',
    'days5daytimetemperaturemax', 'days5daytimetemperaturemin', 'days5daytimewinddirection',
    'days5daytimewindgustspeedmax', 'days5daytimewindspeed', 'days5daytimewindspeedmax', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f8a.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending day 5 full day and daytime forecast data...")
send_with_fallback(feeds['days5start']['key'], days_5_Start, "days5.forecastStart")
send_with_fallback(feeds['days5end']['key'], days_5_End, "days5.forecastEnd")
sp(io)
send_with_fallback(feeds['days5conditioncode']['key'], days_5_conditionCode, "days5.conditionCode")
send_with_fallback(feeds['days5maxuvindex']['key'], days_5_maxUvIndex, "days5.maxUvIndex")
send_with_fallback(feeds['days5moonphase']['key'], days_5_moonPhase, "days5.moonPhase")
send_with_fallback(feeds['days5moonrise']['key'], days_5_moonrise, "days5.moonrise")
send_with_fallback(feeds['days5moonset']['key'], days_5_moonset, "days5.moonset")
sp(io)
send_with_fallback(feeds['days5precipitationamount']['key'], days_5_precipitationAmount, "days5.precipitationAmount")
send_with_fallback(feeds['days5precipitationchance']['key'], days_5_precipitationChance, "days5.precipitationChance")
send_with_fallback(feeds['days5precipitationtype']['key'], days_5_precipitationType, "days5.precipitationType")
send_with_fallback(feeds['days5snowfallamount']['key'], days_5_snowfallAmount, "days5.snowfallAmount")
send_with_fallback(feeds['days5solarmidnight']['key'], days_5_solarMidnight, "days5.solarMidnight")
sp(io)
send_with_fallback(feeds['days5solarnoon']['key'], days_5_solarNoon, "days5.solarNoon")
send_with_fallback(feeds['days5sunrise']['key'], days_5_sunrise, "days5.sunrise")
send_with_fallback(feeds['days5sunrisecivil']['key'], days_5_sunriseCivil, "days5.sunriseCivil")
send_with_fallback(feeds['days5sunrisenautical']['key'], days_5_sunriseNautical, "days5.sunriseNautical")
send_with_fallback(feeds['days5sunriseastronomical']['key'], days_5_sunriseAstronomical, "days5.sunriseAstronomical")
sp(io)
send_with_fallback(feeds['days5sunset']['key'], days_5_sunset, "days5.sunset")
send_with_fallback(feeds['days5sunsetcivil']['key'], days_5_sunsetCivil, "days5.sunsetCivil")
send_with_fallback(feeds['days5sunsetnautical']['key'], days_5_sunsetNautical, "days5.sunsetNautical")
send_with_fallback(feeds['days5sunsetastronomical']['key'], days_5_sunsetAstronomical, "days5.sunsetAstronomical")
sp(io)
send_with_fallback(feeds['days5temperaturemax']['key'], days_5_temperatureMax, "days5.temperatureMax")
send_with_fallback(feeds['days5temperaturemin']['key'], days_5_temperatureMin, "days5.temperatureMin")
send_with_fallback(feeds['days5windgustspeedmax']['key'], days_5_windGustSpeedMax, "days5.windGustSpeedMax")
send_with_fallback(feeds['days5windspeedavg']['key'], days_5_windSpeedAvg, "days5.windSpeedAvg")
send_with_fallback(feeds['days5windspeedmax']['key'], days_5_windSpeedMax, "days5.windSpeedMax")
sp(io)
send_with_fallback(feeds['days5daytimestart']['key'], days_5_daytime_start, "days5daytime.forecastStart")
send_with_fallback(feeds['days5daytimeend']['key'], days_5_daytime_end, "days5daytime.forecastEnd")
send_with_fallback(feeds['days5daytimecloudcover']['key'], days_5_daytime_cloudCover, "days5daytime.cloudCover")
send_with_fallback(feeds['days5daytimeconditioncode']['key'], days_5_daytime_conditionCode, "days5daytime.conditionCode")
send_with_fallback(feeds['days5daytimehumidity']['key'], days_5_daytime_humidity, "days5daytime.humidity")
sp(io)
send_with_fallback(feeds['days5daytimeprecipitationamount']['key'], days_5_daytime_precipitationAmount, "days5daytime.precipitationAmount")
send_with_fallback(feeds['days5daytimeprecipitationchance']['key'], days_5_daytime_precipitationChance, "days5daytime.precipitationChance")
send_with_fallback(feeds['days5daytimeprecipitationtype']['key'], days_5_daytime_precipitationType, "days5daytime.precipitationType")
send_with_fallback(feeds['days5daytimesnowfallamount']['key'], days_5_daytime_snowfallAmount, "days5daytime.snowfallAmount")
send_with_fallback(feeds['days5daytimetemperaturemax']['key'], days_5_daytime_temperatureMax, "days5daytime.temperatureMax")
sp(io)
send_with_fallback(feeds['days5daytimetemperaturemin']['key'], days_5_daytime_temperatureMin, "days5daytime.temperatureMin")
send_with_fallback(feeds['days5daytimewinddirection']['key'], days_5_daytime_windDirection, "days5daytime.windDirection")
send_with_fallback(feeds['days5daytimewindgustspeedmax']['key'], days_5_daytime_windGustSpeedMax, "days5daytime.windGustSpeedMax")
send_with_fallback(feeds['days5daytimewindspeed']['key'], days_5_daytime_windSpeed, "days5daytime.windSpeed")
send_with_fallback(feeds['days5daytimewindspeedmax']['key'], days_5_daytime_windSpeedMax, "days5daytime.windSpeedMax")
sp(io)

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f8a.py")

time.sleep(SLEEP_DURATION_SHORT)
print("f8a.py ran successfully")
supervisor.set_next_code_file("code.py")
supervisor.reload()
