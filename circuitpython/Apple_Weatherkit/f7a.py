"""
Day 2 Full + Daytime Forecast Handler
Retrieves and transmits day 2 complete day and daytime forecast data to Adafruit IO.
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

print("This is file: f7a.py - Day 2 Full + Daytime Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract day 2 full day data
days_2 = safe_get(forecast, "forecast_days_2", {}, missing_labels, "forecast_days_2") or {}
days_2_Start = safe_get(days_2, "forecastStart", "0", missing_labels, "days2.forecastStart")
days_2_End = safe_get(days_2, "forecastEnd", "0", missing_labels, "days2.forecastEnd")
days_2_conditionCode = safe_get(days_2, "conditionCode", "0", missing_labels, "days2.conditionCode")
days_2_maxUvIndex = safe_get(days_2, "maxUvIndex", 0, missing_labels, "days2.maxUvIndex")
days_2_moonPhase = safe_get(days_2, "moonPhase", "0", missing_labels, "days2.moonPhase")
days_2_moonrise = safe_get(days_2, "moonrise", "0", missing_labels, "days2.moonrise")
days_2_moonset = safe_get(days_2, "moonset", "0", missing_labels, "days2.moonset")
days_2_precipitationAmount = safe_get(days_2, "precipitationAmount", 0, missing_labels, "days2.precipitationAmount")
days_2_precipitationChance = pct(safe_get(days_2, "precipitationChance", 0, missing_labels, "days2.precipitationChance"))
days_2_precipitationType = safe_get(days_2, "precipitationType", "0", missing_labels, "days2.precipitationType")
days_2_snowfallAmount = safe_get(days_2, "snowfallAmount", 0, missing_labels, "days2.snowfallAmount")
days_2_solarMidnight = safe_get(days_2, "solarMidnight", "0", missing_labels, "days2.solarMidnight")
days_2_solarNoon = safe_get(days_2, "solarNoon", "0", missing_labels, "days2.solarNoon")
days_2_sunrise = safe_get(days_2, "sunrise", "0", missing_labels, "days2.sunrise")
days_2_sunriseCivil = safe_get(days_2, "sunriseCivil", "0", missing_labels, "days2.sunriseCivil")
days_2_sunriseNautical = safe_get(days_2, "sunriseNautical", "0", missing_labels, "days2.sunriseNautical")
days_2_sunriseAstronomical = safe_get(days_2, "sunriseAstronomical", "0", missing_labels, "days2.sunriseAstronomical")
days_2_sunset = safe_get(days_2, "sunset", "0", missing_labels, "days2.sunset")
days_2_sunsetCivil = safe_get(days_2, "sunsetCivil", "0", missing_labels, "days2.sunsetCivil")
days_2_sunsetNautical = safe_get(days_2, "sunsetNautical", "0", missing_labels, "days2.sunsetNautical")
days_2_sunsetAstronomical = safe_get(days_2, "sunsetAstronomical", "0", missing_labels, "days2.sunsetAstronomical")
days_2_temperatureMax = tof(safe_get(days_2, "temperatureMax", 0, missing_labels, "days2.temperatureMax"))
days_2_temperatureMin = tof(safe_get(days_2, "temperatureMin", 0, missing_labels, "days2.temperatureMin"))
days_2_windGustSpeedMax = tomph(safe_get(days_2, "windGustSpeedMax", 0, missing_labels, "days2.windGustSpeedMax"))
days_2_windSpeedAvg = tomph(safe_get(days_2, "windSpeedAvg", 0, missing_labels, "days2.windSpeedAvg"))
days_2_windSpeedMax = tomph(safe_get(days_2, "windSpeedMax", 0, missing_labels, "days2.windSpeedMax"))

# Extract day 2 daytime data
days_2_daytime_forecast = safe_get(days_2, "daytimeForecast", {}, missing_labels, "days2.daytimeForecast") or {}
days_2_daytime_start = safe_get(days_2_daytime_forecast, 'forecastStart', "0", missing_labels, "days2daytime.forecastStart")
days_2_daytime_end = safe_get(days_2_daytime_forecast, 'forecastEnd', "0", missing_labels, "days2daytime.forecastEnd")
days_2_daytime_cloudCover = pct(safe_get(days_2_daytime_forecast, 'cloudCover', 0, missing_labels, "days2daytime.cloudCover"))
days_2_daytime_conditionCode = safe_get(days_2_daytime_forecast, 'conditionCode', "0", missing_labels, "days2daytime.conditionCode")
days_2_daytime_humidity = pct(safe_get(days_2_daytime_forecast, 'humidity', 0, missing_labels, "days2daytime.humidity"))
days_2_daytime_precipitationAmount = safe_get(days_2_daytime_forecast, 'precipitationAmount', 0, missing_labels, "days2daytime.precipitationAmount")
days_2_daytime_precipitationChance = pct(safe_get(days_2_daytime_forecast, 'precipitationChance', 0, missing_labels, "days2daytime.precipitationChance"))
days_2_daytime_precipitationType = safe_get(days_2_daytime_forecast, 'precipitationType', "0", missing_labels, "days2daytime.precipitationType")
days_2_daytime_snowfallAmount = safe_get(days_2_daytime_forecast, 'snowfallAmount', 0, missing_labels, "days2daytime.snowfallAmount")
days_2_daytime_temperatureMax = tof(safe_get(days_2_daytime_forecast, 'temperatureMax', 0, missing_labels, "days2daytime.temperatureMax"))
days_2_daytime_temperatureMin = tof(safe_get(days_2_daytime_forecast, 'temperatureMin', 0, missing_labels, "days2daytime.temperatureMin"))
days_2_daytime_windDirection = windir(safe_get(days_2_daytime_forecast, 'windDirection', 0, missing_labels, "days2daytime.windDirection"))
days_2_daytime_windGustSpeedMax = tomph(safe_get(days_2_daytime_forecast, 'windGustSpeedMax', 0, missing_labels, "days2daytime.windGustSpeedMax"))
days_2_daytime_windSpeed = tomph(safe_get(days_2_daytime_forecast, 'windSpeed', 0, missing_labels, "days2daytime.windSpeed"))
days_2_daytime_windSpeedMax = tomph(safe_get(days_2_daytime_forecast, 'windSpeedMax', 0, missing_labels, "days2daytime.windSpeedMax"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'days2start', 'days2end', 'days2conditioncode', 'days2maxuvindex', 'days2moonphase',
    'days2moonrise', 'days2moonset', 'days2precipitationamount', 'days2precipitationchance',
    'days2precipitationtype', 'days2snowfallamount', 'days2solarmidnight', 'days2solarnoon',
    'days2sunrise', 'days2sunrisecivil', 'days2sunrisenautical', 'days2sunriseastronomical',
    'days2sunset', 'days2sunsetcivil', 'days2sunsetnautical', 'days2sunsetastronomical',
    'days2temperaturemax', 'days2temperaturemin', 'days2windgustspeedmax', 'days2windspeedavg',
    'days2windspeedmax', 'days2daytimestart', 'days2daytimeend', 'days2daytimecloudcover',
    'days2daytimeconditioncode', 'days2daytimehumidity', 'days2daytimeprecipitationamount',
    'days2daytimeprecipitationchance', 'days2daytimeprecipitationtype', 'days2daytimesnowfallamount',
    'days2daytimetemperaturemax', 'days2daytimetemperaturemin', 'days2daytimewinddirection',
    'days2daytimewindgustspeedmax', 'days2daytimewindspeed', 'days2daytimewindspeedmax', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f7a.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending day 2 full day and daytime forecast data...")
send_with_fallback(feeds['days2start']['key'], days_2_Start, "days2.forecastStart")
send_with_fallback(feeds['days2end']['key'], days_2_End, "days2.forecastEnd")
sp(io)
send_with_fallback(feeds['days2conditioncode']['key'], days_2_conditionCode, "days2.conditionCode")
send_with_fallback(feeds['days2maxuvindex']['key'], days_2_maxUvIndex, "days2.maxUvIndex")
send_with_fallback(feeds['days2moonphase']['key'], days_2_moonPhase, "days2.moonPhase")
send_with_fallback(feeds['days2moonrise']['key'], days_2_moonrise, "days2.moonrise")
send_with_fallback(feeds['days2moonset']['key'], days_2_moonset, "days2.moonset")
sp(io)
send_with_fallback(feeds['days2precipitationamount']['key'], days_2_precipitationAmount, "days2.precipitationAmount")
send_with_fallback(feeds['days2precipitationchance']['key'], days_2_precipitationChance, "days2.precipitationChance")
send_with_fallback(feeds['days2precipitationtype']['key'], days_2_precipitationType, "days2.precipitationType")
send_with_fallback(feeds['days2snowfallamount']['key'], days_2_snowfallAmount, "days2.snowfallAmount")
send_with_fallback(feeds['days2solarmidnight']['key'], days_2_solarMidnight, "days2.solarMidnight")
sp(io)
send_with_fallback(feeds['days2solarnoon']['key'], days_2_solarNoon, "days2.solarNoon")
send_with_fallback(feeds['days2sunrise']['key'], days_2_sunrise, "days2.sunrise")
send_with_fallback(feeds['days2sunrisecivil']['key'], days_2_sunriseCivil, "days2.sunriseCivil")
send_with_fallback(feeds['days2sunrisenautical']['key'], days_2_sunriseNautical, "days2.sunriseNautical")
send_with_fallback(feeds['days2sunriseastronomical']['key'], days_2_sunriseAstronomical, "days2.sunriseAstronomical")
sp(io)
send_with_fallback(feeds['days2sunset']['key'], days_2_sunset, "days2.sunset")
send_with_fallback(feeds['days2sunsetcivil']['key'], days_2_sunsetCivil, "days2.sunsetCivil")
send_with_fallback(feeds['days2sunsetnautical']['key'], days_2_sunsetNautical, "days2.sunsetNautical")
send_with_fallback(feeds['days2sunsetastronomical']['key'], days_2_sunsetAstronomical, "days2.sunsetAstronomical")
sp(io)
send_with_fallback(feeds['days2temperaturemax']['key'], days_2_temperatureMax, "days2.temperatureMax")
send_with_fallback(feeds['days2temperaturemin']['key'], days_2_temperatureMin, "days2.temperatureMin")
send_with_fallback(feeds['days2windgustspeedmax']['key'], days_2_windGustSpeedMax, "days2.windGustSpeedMax")
send_with_fallback(feeds['days2windspeedavg']['key'], days_2_windSpeedAvg, "days2.windSpeedAvg")
send_with_fallback(feeds['days2windspeedmax']['key'], days_2_windSpeedMax, "days2.windSpeedMax")
sp(io)
send_with_fallback(feeds['days2daytimestart']['key'], days_2_daytime_start, "days2daytime.forecastStart")
send_with_fallback(feeds['days2daytimeend']['key'], days_2_daytime_end, "days2daytime.forecastEnd")
send_with_fallback(feeds['days2daytimecloudcover']['key'], days_2_daytime_cloudCover, "days2daytime.cloudCover")
send_with_fallback(feeds['days2daytimeconditioncode']['key'], days_2_daytime_conditionCode, "days2daytime.conditionCode")
send_with_fallback(feeds['days2daytimehumidity']['key'], days_2_daytime_humidity, "days2daytime.humidity")
sp(io)
send_with_fallback(feeds['days2daytimeprecipitationamount']['key'], days_2_daytime_precipitationAmount, "days2daytime.precipitationAmount")
send_with_fallback(feeds['days2daytimeprecipitationchance']['key'], days_2_daytime_precipitationChance, "days2daytime.precipitationChance")
send_with_fallback(feeds['days2daytimeprecipitationtype']['key'], days_2_daytime_precipitationType, "days2daytime.precipitationType")
send_with_fallback(feeds['days2daytimesnowfallamount']['key'], days_2_daytime_snowfallAmount, "days2daytime.snowfallAmount")
send_with_fallback(feeds['days2daytimetemperaturemax']['key'], days_2_daytime_temperatureMax, "days2daytime.temperatureMax")
sp(io)
send_with_fallback(feeds['days2daytimetemperaturemin']['key'], days_2_daytime_temperatureMin, "days2daytime.temperatureMin")
send_with_fallback(feeds['days2daytimewinddirection']['key'], days_2_daytime_windDirection, "days2daytime.windDirection")
send_with_fallback(feeds['days2daytimewindgustspeedmax']['key'], days_2_daytime_windGustSpeedMax, "days2daytime.windGustSpeedMax")
send_with_fallback(feeds['days2daytimewindspeed']['key'], days_2_daytime_windSpeed, "days2daytime.windSpeed")
send_with_fallback(feeds['days2daytimewindspeedmax']['key'], days_2_daytime_windSpeedMax, "days2daytime.windSpeedMax")
sp(io)

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f7a.py")

time.sleep(SLEEP_DURATION_SHORT)
print("f7a.py ran successfully")
supervisor.set_next_code_file("f8.py")
supervisor.reload()
