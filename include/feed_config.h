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

// Enabled, source JSON field, existing AIO destination key, output conversion.
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

}

#endif
