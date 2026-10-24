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
    {true, "visibility", "hours1visibility", Conversion::Raw},
    {true, "windDirection", "hours1winddirection", Conversion::Compass},
    {true, "windGust", "hours1windgust", Conversion::Mph},
    {true, "windSpeed", "hours1windspeed", Conversion::Mph},
};

}

#endif
