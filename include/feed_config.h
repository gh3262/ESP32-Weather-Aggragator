#ifndef FEED_CONFIG_H
#define FEED_CONFIG_H

namespace feed_config {

enum class Conversion { Text, Boolean, Percent, Temperature, Miles, Mph, Raw, Bearing, Compass };

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

}

#endif
