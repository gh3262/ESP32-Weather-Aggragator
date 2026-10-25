#ifndef FEED_CONFIG_H
#define FEED_CONFIG_H

namespace feed_config {

enum class Conversion {
  Text, Boolean, ForecastDaylight, Percent, Temperature, Miles, Mph, Raw, Bearing, Compass
};

struct Mapping {
  bool enabled;
  const char* field;
  const char* feed;
  Conversion conversion;
};

// Enabled, source JSON field/path, existing AIO destination key, output conversion.
constexpr Mapping weatherkitCurrent[] = {
    {true, "asOf", "currentasof", Conversion::Text},
    {true, "cloudCover", "currentcloudcover", Conversion::Percent},
    {true, "cloudCoverLowAltPct", "currentcloudcoverlowaltpct", Conversion::Percent},
    {true, "cloudCoverMidAltPct", "currentcloudcovermidaltpct", Conversion::Percent},
    {true, "cloudCoverHighAltPct", "currentcloudcoverhighaltpct", Conversion::Percent},
    {true, "conditionCode", "currentconditioncode", Conversion::Text},
    {true, "daylight", "currentdaylight", Conversion::Boolean},
    {true, "humidity", "currenthumidity", Conversion::Percent},
    {true, "precipitationIntensity", "currentprecipitationintensity", Conversion::Raw},
    {true, "pressure", "currentpressure", Conversion::Raw},
    {true, "pressureTrend", "currentpressuretrend", Conversion::Text},
    {true, "temperature", "currenttemperature", Conversion::Temperature},
    {true, "temperatureApparent", "currenttemperatureapparent", Conversion::Temperature},
    {true, "temperatureDewPoint", "currenttemperaturedewpoint", Conversion::Temperature},
    {true, "visibility", "currentvisibility", Conversion::Miles},
    {true, "windDirection", "currentwinddegrees", Conversion::Bearing},
    {true, "windDirection", "currentwinddirection", Conversion::Compass},
    {true, "windGust", "currentwindgust", Conversion::Mph},
    {true, "windSpeed", "currentwindspeed", Conversion::Mph},
    {true, "uvIndex", "currentuv", Conversion::Raw},
};

constexpr Mapping weatherkitMinutes5[] = {
    {true, "startTime", "m5starttime", Conversion::Text},
    {true, "precipitationChance", "m5precipitationchance", Conversion::Percent},
    {true, "precipitationIntensity", "m5precipitationintensity", Conversion::Raw},
};

constexpr Mapping weatherkitMinutes30[] = {
    {true, "startTime", "m30starttime", Conversion::Text},
    {true, "precipitationChance", "m30precipitationchance", Conversion::Percent},
    {true, "precipitationIntensity", "m30precipitationintensity", Conversion::Raw},
};

constexpr Mapping weatherkitHour1[] = {
    {true, "forecastStart", "hours1start", Conversion::Text},
    {true, "cloudCover", "hours1cloudcover", Conversion::Percent},
    {true, "conditionCode", "hours1conditioncode", Conversion::Text},
    {true, "daylight", "hours1daylight", Conversion::ForecastDaylight},
    {true, "humidity", "hours1humidity", Conversion::Percent},
    {true, "precipitationAmount", "hours1precipitationamount", Conversion::Raw},
    {true, "precipitationChance", "hours1precipitationchance", Conversion::Percent},
    {true, "precipitationType", "hours1precipitationtype", Conversion::Text},
    {true, "precipitationIntensity", "hours1precipitationintensity", Conversion::Raw},
    {true, "pressure", "hours1pressure", Conversion::Raw},
    {true, "pressureTrend", "hours1pressuretrend", Conversion::Text},
    {true, "snowfallIntensity", "hours1snowfallintensity", Conversion::Raw},
    {true, "snowfallAmount", "hours1snowfallamount", Conversion::Raw},
    {true, "temperature", "hours1temperature", Conversion::Temperature},
    {true, "temperatureApparent", "hours1temperatureapparent", Conversion::Temperature},
    {true, "temperatureDewPoint", "hours1temperaturedewpoint", Conversion::Temperature},
    {true, "uvIndex", "hours1uvindex", Conversion::Raw},
    {true, "visibility", "hours1visibility", Conversion::Miles},
    {true, "windDirection", "hours1winddirection", Conversion::Compass},
    {true, "windGust", "hours1windgust", Conversion::Mph},
    {true, "windSpeed", "hours1windspeed", Conversion::Mph},
};

constexpr Mapping weatherkitHour2[] = {
    {true, "forecastStart", "hours2start", Conversion::Text},
    {true, "cloudCover", "hours2cloudcover", Conversion::Percent},
    {true, "conditionCode", "hours2conditioncode", Conversion::Text},
    {true, "daylight", "hours2daylight", Conversion::ForecastDaylight},
    {true, "humidity", "hours2humidity", Conversion::Percent},
    {true, "precipitationAmount", "hours2precipitationamount", Conversion::Raw},
    {true, "precipitationChance", "hours2precipitationchance", Conversion::Percent},
    {true, "precipitationType", "hours2precipitationtype", Conversion::Text},
    {true, "precipitationIntensity", "hours2precipitationintensity", Conversion::Raw},
    {true, "pressure", "hours2pressure", Conversion::Raw},
    {true, "pressureTrend", "hours2pressuretrend", Conversion::Text},
    {true, "snowfallIntensity", "hours2snowfallintensity", Conversion::Raw},
    {true, "snowfallAmount", "hours2snowfallamount", Conversion::Raw},
    {true, "temperature", "hours2temperature", Conversion::Temperature},
    {true, "temperatureApparent", "hours2temperatureapparent", Conversion::Temperature},
    {true, "temperatureDewPoint", "hours2temperaturedewpoint", Conversion::Temperature},
    {true, "uvIndex", "hours2uvindex", Conversion::Raw},
    {true, "visibility", "hours2visibility", Conversion::Miles},
    {true, "windDirection", "hours2winddirection", Conversion::Compass},
    {true, "windGust", "hours2windgust", Conversion::Mph},
    {true, "windSpeed", "hours2windspeed", Conversion::Mph},
};

constexpr Mapping weatherkitHour6[] = {
    {true, "forecastStart", "hours6start", Conversion::Text},
    {true, "cloudCover", "hours6cloudcover", Conversion::Percent},
    {true, "conditionCode", "hours6conditioncode", Conversion::Text},
    {true, "daylight", "hours6daylight", Conversion::ForecastDaylight},
    {true, "humidity", "hours6humidity", Conversion::Percent},
    {true, "precipitationAmount", "hours6precipitationamount", Conversion::Raw},
    {true, "precipitationChance", "hours6precipitationchance", Conversion::Percent},
    {true, "precipitationType", "hours6precipitationtype", Conversion::Text},
    {true, "precipitationIntensity", "hours6precipitationintensity", Conversion::Raw},
    {true, "pressure", "hours6pressure", Conversion::Raw},
    {true, "pressureTrend", "hours6pressuretrend", Conversion::Text},
    {true, "snowfallIntensity", "hours6snowfallintensity", Conversion::Raw},
    {true, "snowfallAmount", "hours6snowfallamount", Conversion::Raw},
    {true, "temperature", "hours6temperature", Conversion::Temperature},
    {true, "temperatureApparent", "hours6temperatureapparent", Conversion::Temperature},
    {true, "temperatureDewPoint", "hours6temperaturedewpoint", Conversion::Temperature},
    {true, "uvIndex", "hours6uvindex", Conversion::Raw},
    {true, "visibility", "hours6visibility", Conversion::Miles},
    {true, "windDirection", "hours6winddirection", Conversion::Compass},
    {true, "windGust", "hours6windgust", Conversion::Mph},
    {true, "windSpeed", "hours6windspeed", Conversion::Mph},
};

constexpr Mapping weatherkitHour12[] = {
    {true, "forecastStart", "hours12start", Conversion::Text},
    {true, "cloudCover", "hours12cloudcover", Conversion::Percent},
    {true, "conditionCode", "hours12conditioncode", Conversion::Text},
    {true, "daylight", "hours12daylight", Conversion::ForecastDaylight},
    {true, "humidity", "hours12humidity", Conversion::Percent},
    {true, "precipitationAmount", "hours12precipitationamount", Conversion::Raw},
    {true, "precipitationChance", "hours12precipitationchance", Conversion::Percent},
    {true, "precipitationType", "hours12precipitationtype", Conversion::Text},
    {true, "precipitationIntensity", "hours12precipitationintensity", Conversion::Raw},
    {true, "pressure", "hours12pressure", Conversion::Raw},
    {true, "pressureTrend", "hours12pressuretrend", Conversion::Text},
    {true, "snowfallIntensity", "hours12snowfallintensity", Conversion::Raw},
    {true, "snowfallAmount", "hours12snowfallamount", Conversion::Raw},
    {true, "temperature", "hours12temperature", Conversion::Temperature},
    {true, "temperatureApparent", "hours12temperatureapparent", Conversion::Temperature},
    {true, "temperatureDewPoint", "hours12temperaturedewpoint", Conversion::Temperature},
    {true, "uvIndex", "hours12uvindex", Conversion::Raw},
    {true, "visibility", "hours12visibility", Conversion::Miles},
    {true, "windDirection", "hours12winddirection", Conversion::Compass},
    {true, "windGust", "hours12windgust", Conversion::Mph},
    {true, "windSpeed", "hours12windspeed", Conversion::Mph},
};

constexpr Mapping weatherkitHour24[] = {
    {true, "forecastStart", "hours24start", Conversion::Text},
    {true, "cloudCover", "hours24cloudcover", Conversion::Percent},
    {true, "conditionCode", "hours24conditioncode", Conversion::Text},
    {true, "daylight", "hours24daylight", Conversion::ForecastDaylight},
    {true, "humidity", "hours24humidity", Conversion::Percent},
    {true, "precipitationAmount", "hours24precipitationamount", Conversion::Raw},
    {true, "precipitationChance", "hours24precipitationchance", Conversion::Percent},
    {true, "precipitationType", "hours24precipitationtype", Conversion::Text},
    {true, "precipitationIntensity", "hours24precipitationintensity", Conversion::Raw},
    {true, "pressure", "hours24pressure", Conversion::Raw},
    {true, "pressureTrend", "hours24pressuretrend", Conversion::Text},
    {true, "snowfallIntensity", "hours24snowfallintensity", Conversion::Raw},
    {true, "snowfallAmount", "hours24snowfallamount", Conversion::Raw},
    {true, "temperature", "hours24temperature", Conversion::Temperature},
    {true, "temperatureApparent", "hours24temperatureapparent", Conversion::Temperature},
    {true, "temperatureDewPoint", "hours24temperaturedewpoint", Conversion::Temperature},
    {true, "uvIndex", "hours24uvindex", Conversion::Raw},
    {true, "visibility", "hours24visibility", Conversion::Miles},
    {true, "windDirection", "hours24winddirection", Conversion::Compass},
    {true, "windGust", "hours24windgust", Conversion::Mph},
    {true, "windSpeed", "hours24windspeed", Conversion::Mph},
};

constexpr Mapping weatherkitDay1[] = {
    {true, "forecastStart", "days1start", Conversion::Text},
    {true, "forecastEnd", "days1end", Conversion::Text},
    {true, "conditionCode", "days1conditioncode", Conversion::Text},
    {true, "maxUvIndex", "days1maxuvindex", Conversion::Raw},
    {true, "moonPhase", "days1moonphase", Conversion::Text},
    {true, "moonrise", "days1moonrise", Conversion::Text},
    {true, "moonset", "days1moonset", Conversion::Text},
    {true, "precipitationAmount", "days1precipitationamount", Conversion::Raw},
    {true, "precipitationChance", "days1precipitationchance", Conversion::Percent},
    {true, "precipitationType", "days1precipitationtype", Conversion::Text},
    {true, "snowfallAmount", "days1snowfallamount", Conversion::Raw},
    {true, "solarMidnight", "days1solarmidnight", Conversion::Text},
    {true, "solarNoon", "days1solarnoon", Conversion::Text},
    {true, "sunrise", "days1sunrise", Conversion::Text},
    {true, "sunriseCivil", "days1sunrisecivil", Conversion::Text},
    {true, "sunriseNautical", "days1sunrisenautical", Conversion::Text},
    {true, "sunriseAstronomical", "days1sunriseastronomical", Conversion::Text},
    {true, "sunset", "days1sunset", Conversion::Text},
    {true, "sunsetCivil", "days1sunsetcivil", Conversion::Text},
    {true, "sunsetNautical", "days1sunsetnautical", Conversion::Text},
    {true, "sunsetAstronomical", "days1sunsetastronomical", Conversion::Text},
    {true, "temperatureMax", "days1temperaturemax", Conversion::Temperature},
    {true, "temperatureMin", "days1temperaturemin", Conversion::Temperature},
    {true, "windGustSpeedMax", "days1windgustspeedmax", Conversion::Mph},
    {true, "windSpeedAvg", "days1windspeedavg", Conversion::Mph},
    {true, "windSpeedMax", "days1windspeedmax", Conversion::Mph},
    {true, "daytimeForecast.forecastStart", "days1daytimestart", Conversion::Text},
    {true, "daytimeForecast.forecastEnd", "days1daytimeend", Conversion::Text},
    {true, "daytimeForecast.cloudCover", "days1daytimecloudcover", Conversion::Percent},
    {true, "daytimeForecast.conditionCode", "days1daytimeconditioncode", Conversion::Text},
    {true, "daytimeForecast.humidity", "days1daytimehumidity", Conversion::Percent},
    {true, "daytimeForecast.precipitationAmount", "days1daytimeprecipitationamount", Conversion::Raw},
    {true, "daytimeForecast.precipitationChance", "days1daytimeprecipitationchance", Conversion::Percent},
    {true, "daytimeForecast.precipitationType", "days1daytimeprecipitationtype", Conversion::Text},
    {true, "daytimeForecast.snowfallAmount", "days1daytimesnowfallamount", Conversion::Raw},
    {true, "daytimeForecast.temperatureMax", "days1daytimetemperaturemax", Conversion::Temperature},
    {true, "daytimeForecast.temperatureMin", "days1daytimetemperaturemin", Conversion::Temperature},
    {true, "daytimeForecast.windDirection", "days1daytimewinddirection", Conversion::Compass},
    {true, "daytimeForecast.windGustSpeedMax", "days1daytimewindgustspeedmax", Conversion::Mph},
    {true, "daytimeForecast.windSpeed", "days1daytimewindspeed", Conversion::Mph},
    {true, "daytimeForecast.windSpeedMax", "days1daytimewindspeedmax", Conversion::Mph},
    {true, "overnightForecast.forecastStart", "days1ovntstart", Conversion::Text},
    {true, "overnightForecast.forecastEnd", "days1ovntend", Conversion::Text},
    {true, "overnightForecast.cloudCover", "days1ovntcloudcover", Conversion::Percent},
    {true, "overnightForecast.conditionCode", "days1ovntconditioncode", Conversion::Text},
    {true, "overnightForecast.humidity", "days1ovnthumidity", Conversion::Percent},
    {true, "overnightForecast.precipitationAmount", "days1ovntprecipitationamount", Conversion::Raw},
    {true, "overnightForecast.precipitationChance", "days1ovntprecipitationchance", Conversion::Percent},
    {true, "overnightForecast.precipitationType", "days1ovntprecipitationtype", Conversion::Text},
    {true, "overnightForecast.snowfallAmount", "days1ovntsnowfallamount", Conversion::Raw},
    {true, "overnightForecast.temperatureMax", "days1ovnttemperaturemax", Conversion::Temperature},
    {true, "overnightForecast.temperatureMin", "days1ovnttemperaturemin", Conversion::Temperature},
    {true, "overnightForecast.windDirection", "days1ovntwinddirection", Conversion::Compass},
    {true, "overnightForecast.windGustSpeedMax", "days1ovntwindgustspeedmax", Conversion::Mph},
    {true, "overnightForecast.windSpeed", "days1ovntwindspeed", Conversion::Mph},
    {true, "overnightForecast.windSpeedMax", "days1ovntwindspeedmax", Conversion::Mph},
};

}

#endif
