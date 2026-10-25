#ifndef TEST_WEATHERKIT_DAY_FIXTURE_H
#define TEST_WEATHERKIT_DAY_FIXTURE_H

constexpr char kDay1Payload[] = R"({
  "forecastStart":"2026-10-25T05:00:00Z","forecastEnd":"2026-10-26T05:00:00Z",
  "conditionCode":"Clear","maxUvIndex":3,"moonPhase":"Full",
  "moonrise":"2026-10-25T22:00:00Z","moonset":"2026-10-25T10:00:00Z",
  "precipitationAmount":0,"precipitationChance":0.65,"precipitationType":"clear",
  "snowfallAmount":0,"solarMidnight":"2026-10-25T05:30:00Z",
  "solarNoon":"2026-10-25T17:30:00Z","sunrise":"2026-10-25T12:00:00Z",
  "sunriseCivil":"2026-10-25T11:30:00Z","sunriseNautical":"2026-10-25T11:00:00Z",
  "sunriseAstronomical":"2026-10-25T10:30:00Z","sunset":"2026-10-25T23:00:00Z",
  "sunsetCivil":"2026-10-25T23:30:00Z","sunsetNautical":"2026-10-26T00:00:00Z",
  "sunsetAstronomical":"2026-10-26T00:30:00Z","temperatureMax":20,"temperatureMin":10,
  "windGustSpeedMax":32.18688,"windSpeedAvg":16.09344,"windSpeedMax":24.14016,
  "daytimeForecast":{
    "forecastStart":"2026-10-25T12:00:00Z","forecastEnd":"2026-10-25T23:00:00Z",
    "cloudCover":0.25,"conditionCode":"MostlyClear","humidity":0.65,
    "precipitationAmount":0,"precipitationChance":0.5,"precipitationType":"rain",
    "snowfallAmount":0,"temperatureMax":20,"temperatureMin":10,
    "windDirection":225,"windGustSpeedMax":32.18688,"windSpeed":16.09344,
    "windSpeedMax":24.14016
  },
  "overnightForecast":{
    "forecastStart":"2026-10-25T23:00:00Z","forecastEnd":"2026-10-26T12:00:00Z",
    "cloudCover":0.75,"conditionCode":"Cloudy","humidity":0.8,
    "precipitationAmount":1.25,"precipitationChance":0.75,"precipitationType":"snow",
    "snowfallAmount":2,"temperatureMax":0,"temperatureMin":-10,
    "windDirection":90,"windGustSpeedMax":16.09344,"windSpeed":0,
    "windSpeedMax":32.18688
  }
})";

#endif
