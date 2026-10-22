"""
Day 1 Full + Daytime Forecast Handler
Retrieves and transmits day 1 complete day and daytime forecast data to Adafruit IO.
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

print("This is file: f6a.py - Day 1 Full + Daytime Forecast")

# Initialize WiFi and IO client
io = init_wifi_and_io()

last_time = time.monotonic()
print(last_time)

# Fetch weather data
set_stage_color('weather')
print("********")
forecast = io.receive_weather(LOCATION_ID) or {}
missing_labels = set()

# Extract day 1 full day data
days_1 = safe_get(forecast, "forecast_days_1", {}, missing_labels, "forecast_days_1") or {}
days_1_Start = safe_get(days_1, "forecastStart", "0", missing_labels, "days1.forecastStart")
days_1_End = safe_get(days_1, "forecastEnd", "0", missing_labels, "days1.forecastEnd")
days_1_conditionCode = safe_get(days_1, "conditionCode", "0", missing_labels, "days1.conditionCode")
days_1_maxUvIndex = safe_get(days_1, "maxUvIndex", 0, missing_labels, "days1.maxUvIndex")
days_1_moonPhase = safe_get(days_1, "moonPhase", "0", missing_labels, "days1.moonPhase")
days_1_moonrise = safe_get(days_1, "moonrise", "0", missing_labels, "days1.moonrise")
days_1_moonset = safe_get(days_1, "moonset", "0", missing_labels, "days1.moonset")
days_1_precipitationAmount = safe_get(days_1, "precipitationAmount", 0, missing_labels, "days1.precipitationAmount")
days_1_precipitationChance = pct(safe_get(days_1, "precipitationChance", 0, missing_labels, "days1.precipitationChance"))
days_1_precipitationType = safe_get(days_1, "precipitationType", "0", missing_labels, "days1.precipitationType")
days_1_snowfallAmount = safe_get(days_1, "snowfallAmount", 0, missing_labels, "days1.snowfallAmount")
days_1_solarMidnight = safe_get(days_1, "solarMidnight", "0", missing_labels, "days1.solarMidnight")
days_1_solarNoon = safe_get(days_1, "solarNoon", "0", missing_labels, "days1.solarNoon")
days_1_sunrise = safe_get(days_1, "sunrise", "0", missing_labels, "days1.sunrise")
days_1_sunriseCivil = safe_get(days_1, "sunriseCivil", "0", missing_labels, "days1.sunriseCivil")
days_1_sunriseNautical = safe_get(days_1, "sunriseNautical", "0", missing_labels, "days1.sunriseNautical")
days_1_sunriseAstronomical = safe_get(days_1, "sunriseAstronomical", "0", missing_labels, "days1.sunriseAstronomical")
days_1_sunset = safe_get(days_1, "sunset", "0", missing_labels, "days1.sunset")
days_1_sunsetCivil = safe_get(days_1, "sunsetCivil", "0", missing_labels, "days1.sunsetCivil")
days_1_sunsetNautical = safe_get(days_1, "sunsetNautical", "0", missing_labels, "days1.sunsetNautical")
days_1_sunsetAstronomical = safe_get(days_1, "sunsetAstronomical", "0", missing_labels, "days1.sunsetAstronomical")
days_1_temperatureMax = tof(safe_get(days_1, "temperatureMax", 0, missing_labels, "days1.temperatureMax"))
days_1_temperatureMin = tof(safe_get(days_1, "temperatureMin", 0, missing_labels, "days1.temperatureMin"))
days_1_windGustSpeedMax = tomph(safe_get(days_1, "windGustSpeedMax", 0, missing_labels, "days1.windGustSpeedMax"))
days_1_windSpeedAvg = tomph(safe_get(days_1, "windSpeedAvg", 0, missing_labels, "days1.windSpeedAvg"))
days_1_windSpeedMax = tomph(safe_get(days_1, "windSpeedMax", 0, missing_labels, "days1.windSpeedMax"))

# Extract day 1 daytime data
days_1_daytime_forecast = safe_get(days_1, "daytimeForecast", {}, missing_labels, "days1.daytimeForecast") or {}
days_1_daytime_start = safe_get(days_1_daytime_forecast, 'forecastStart', "0", missing_labels, "days1daytime.forecastStart")
days_1_daytime_end = safe_get(days_1_daytime_forecast, 'forecastEnd', "0", missing_labels, "days1daytime.forecastEnd")
days_1_daytime_cloudCover = pct(safe_get(days_1_daytime_forecast, 'cloudCover', 0, missing_labels, "days1daytime.cloudCover"))
days_1_daytime_conditionCode = safe_get(days_1_daytime_forecast, 'conditionCode', "0", missing_labels, "days1daytime.conditionCode")
days_1_daytime_humidity = pct(safe_get(days_1_daytime_forecast, 'humidity', 0, missing_labels, "days1daytime.humidity"))
days_1_daytime_precipitationAmount = safe_get(days_1_daytime_forecast, 'precipitationAmount', 0, missing_labels, "days1daytime.precipitationAmount")
days_1_daytime_precipitationChance = pct(safe_get(days_1_daytime_forecast, 'precipitationChance', 0, missing_labels, "days1daytime.precipitationChance"))
days_1_daytime_precipitationType = safe_get(days_1_daytime_forecast, 'precipitationType', "0", missing_labels, "days1daytime.precipitationType")
days_1_daytime_snowfallAmount = safe_get(days_1_daytime_forecast, 'snowfallAmount', 0, missing_labels, "days1daytime.snowfallAmount")
days_1_daytime_temperatureMax = tof(safe_get(days_1_daytime_forecast, 'temperatureMax', 0, missing_labels, "days1daytime.temperatureMax"))
days_1_daytime_temperatureMin = tof(safe_get(days_1_daytime_forecast, 'temperatureMin', 0, missing_labels, "days1daytime.temperatureMin"))
days_1_daytime_windDirection = windir(safe_get(days_1_daytime_forecast, 'windDirection', 0, missing_labels, "days1daytime.windDirection"))
days_1_daytime_windGustSpeedMax = tomph(safe_get(days_1_daytime_forecast, 'windGustSpeedMax', 0, missing_labels, "days1daytime.windGustSpeedMax"))
days_1_daytime_windSpeed = tomph(safe_get(days_1_daytime_forecast, 'windSpeed', 0, missing_labels, "days1daytime.windSpeed"))
days_1_daytime_windSpeedMax = tomph(safe_get(days_1_daytime_forecast, 'windSpeedMax', 0, missing_labels, "days1daytime.windSpeedMax"))

# Batch retrieve all feeds
set_stage_color('feeds')
print("Fetching feeds...")
feed_names = [
    'days1start', 'days1end', 'days1conditioncode', 'days1maxuvindex', 'days1moonphase',
    'days1moonrise', 'days1moonset', 'days1precipitationamount', 'days1precipitationchance',
    'days1precipitationtype', 'days1snowfallamount', 'days1solarmidnight', 'days1solarnoon',
    'days1sunrise', 'days1sunrisecivil', 'days1sunrisenautical', 'days1sunriseastronomical',
    'days1sunset', 'days1sunsetcivil', 'days1sunsetnautical', 'days1sunsetastronomical',
    'days1temperaturemax', 'days1temperaturemin', 'days1windgustspeedmax', 'days1windspeedavg',
    'days1windspeedmax', 'days1daytimestart', 'days1daytimeend', 'days1daytimecloudcover',
    'days1daytimeconditioncode', 'days1daytimehumidity', 'days1daytimeprecipitationamount',
    'days1daytimeprecipitationchance', 'days1daytimeprecipitationtype', 'days1daytimesnowfallamount',
    'days1daytimetemperaturemax', 'days1daytimetemperaturemin', 'days1daytimewinddirection',
    'days1daytimewindgustspeedmax', 'days1daytimewindspeed', 'days1daytimewindspeedmax', 'rt'
]
feeds = get_feeds_batch(io, feed_names)
sp(io)

if missing_labels:
    log_missing_labels(io, feeds["rt"]["key"], missing_labels, "f6a.py")

send_with_fallback = make_send_with_fallback(io, missing_labels)

# Send all data
set_stage_color('send')
print("Sending day 1 full day and daytime forecast data...")
send_with_fallback(feeds['days1start']['key'], days_1_Start, "days1.forecastStart")
send_with_fallback(feeds['days1end']['key'], days_1_End, "days1.forecastEnd")
sp(io)
send_with_fallback(feeds['days1conditioncode']['key'], days_1_conditionCode, "days1.conditionCode")
send_with_fallback(feeds['days1maxuvindex']['key'], days_1_maxUvIndex, "days1.maxUvIndex")
send_with_fallback(feeds['days1moonphase']['key'], days_1_moonPhase, "days1.moonPhase")
send_with_fallback(feeds['days1moonrise']['key'], days_1_moonrise, "days1.moonrise")
send_with_fallback(feeds['days1moonset']['key'], days_1_moonset, "days1.moonset")
sp(io)
send_with_fallback(feeds['days1precipitationamount']['key'], days_1_precipitationAmount, "days1.precipitationAmount")
send_with_fallback(feeds['days1precipitationchance']['key'], days_1_precipitationChance, "days1.precipitationChance")
send_with_fallback(feeds['days1precipitationtype']['key'], days_1_precipitationType, "days1.precipitationType")
send_with_fallback(feeds['days1snowfallamount']['key'], days_1_snowfallAmount, "days1.snowfallAmount")
send_with_fallback(feeds['days1solarmidnight']['key'], days_1_solarMidnight, "days1.solarMidnight")
sp(io)
send_with_fallback(feeds['days1solarnoon']['key'], days_1_solarNoon, "days1.solarNoon")
send_with_fallback(feeds['days1sunrise']['key'], days_1_sunrise, "days1.sunrise")
send_with_fallback(feeds['days1sunrisecivil']['key'], days_1_sunriseCivil, "days1.sunriseCivil")
send_with_fallback(feeds['days1sunrisenautical']['key'], days_1_sunriseNautical, "days1.sunriseNautical")
send_with_fallback(feeds['days1sunriseastronomical']['key'], days_1_sunriseAstronomical, "days1.sunriseAstronomical")
sp(io)
send_with_fallback(feeds['days1sunset']['key'], days_1_sunset, "days1.sunset")
send_with_fallback(feeds['days1sunsetcivil']['key'], days_1_sunsetCivil, "days1.sunsetCivil")
send_with_fallback(feeds['days1sunsetnautical']['key'], days_1_sunsetNautical, "days1.sunsetNautical")
send_with_fallback(feeds['days1sunsetastronomical']['key'], days_1_sunsetAstronomical, "days1.sunsetAstronomical")
sp(io)
send_with_fallback(feeds['days1temperaturemax']['key'], days_1_temperatureMax, "days1.temperatureMax")
send_with_fallback(feeds['days1temperaturemin']['key'], days_1_temperatureMin, "days1.temperatureMin")
send_with_fallback(feeds['days1windgustspeedmax']['key'], days_1_windGustSpeedMax, "days1.windGustSpeedMax")
send_with_fallback(feeds['days1windspeedavg']['key'], days_1_windSpeedAvg, "days1.windSpeedAvg")
send_with_fallback(feeds['days1windspeedmax']['key'], days_1_windSpeedMax, "days1.windSpeedMax")
sp(io)
send_with_fallback(feeds['days1daytimestart']['key'], days_1_daytime_start, "days1daytime.forecastStart")
send_with_fallback(feeds['days1daytimeend']['key'], days_1_daytime_end, "days1daytime.forecastEnd")
send_with_fallback(feeds['days1daytimecloudcover']['key'], days_1_daytime_cloudCover, "days1daytime.cloudCover")
send_with_fallback(feeds['days1daytimeconditioncode']['key'], days_1_daytime_conditionCode, "days1daytime.conditionCode")
send_with_fallback(feeds['days1daytimehumidity']['key'], days_1_daytime_humidity, "days1daytime.humidity")
sp(io)
send_with_fallback(feeds['days1daytimeprecipitationamount']['key'], days_1_daytime_precipitationAmount, "days1daytime.precipitationAmount")
send_with_fallback(feeds['days1daytimeprecipitationchance']['key'], days_1_daytime_precipitationChance, "days1daytime.precipitationChance")
send_with_fallback(feeds['days1daytimeprecipitationtype']['key'], days_1_daytime_precipitationType, "days1daytime.precipitationType")
send_with_fallback(feeds['days1daytimesnowfallamount']['key'], days_1_daytime_snowfallAmount, "days1daytime.snowfallAmount")
send_with_fallback(feeds['days1daytimetemperaturemax']['key'], days_1_daytime_temperatureMax, "days1daytime.temperatureMax")
sp(io)
send_with_fallback(feeds['days1daytimetemperaturemin']['key'], days_1_daytime_temperatureMin, "days1daytime.temperatureMin")
send_with_fallback(feeds['days1daytimewinddirection']['key'], days_1_daytime_windDirection, "days1daytime.windDirection")
send_with_fallback(feeds['days1daytimewindgustspeedmax']['key'], days_1_daytime_windGustSpeedMax, "days1daytime.windGustSpeedMax")
send_with_fallback(feeds['days1daytimewindspeed']['key'], days_1_daytime_windSpeed, "days1daytime.windSpeed")
send_with_fallback(feeds['days1daytimewindspeedmax']['key'], days_1_daytime_windSpeedMax, "days1daytime.windSpeedMax")
sp(io)

set_stage_color('complete')
io.send_data(feeds["rt"]["key"], "> IB Weather Rotation: Last: f6a.py")

time.sleep(SLEEP_DURATION_SHORT)
print("f6a.py ran successfully")
supervisor.set_next_code_file("f7.py")
supervisor.reload()
