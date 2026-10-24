# CircuitPython weather feed catalog

Reviewed: 2026-10-19.

## Scope and how to use this catalog

Sections 1-10 inventory the supplied source, rather than measuring running boards.
They cover all eight project folders under [circuitpython](circuitpython/),
including the WeatherKit helper and chained handlers. Section 11 adds observed
status-message history from the supplied Adafruit IO `rt` CSV export.
No firmware was changed, no PlatformIO/Arduino tooling was installed, and no live
API requests or feed writes were made.

The companion [CSV catalog](WEATHER_FEED_CATALOG.csv) lists every destination
explicitly, with its incoming source/field, transformation, script, and estimated
interval. Repeated rows are intentional where multiple scripts/projects write
one destination, or multiple input feeds contribute to one output.

- **391 distinct destination keys** appear across the projects.
- The CSV has **440 data rows**, including a console-only Visual Crossing row.
- Counts include shared `rt` once within each project, but only once overall.
- Feed names are the literal keys requested/sent by the source. Their existence,
  current contents, and account ownership were not checked against Adafruit IO.
- Intervals in sections 1-10 are derived from executed sleeps and control flow, not comments.
  Network latency, startup/reloads, throttle waits, retries, and errors add time.
- Polling frequency is not the same as the weather provider's refresh frequency.
  The refresh/cache policy of Adafruit IO's weather integration is not in these files.
- Credentials and configuration values from settings/secrets are not reproduced.
  Configured coordinates and timezone are referenced symbolically except where
  the code itself hardcodes an endpoint or location identifier.

## 1. Project summary

| Project | Incoming source | Destination keys, including `rt` | Estimated normal polling / publishing |
|---|---|---:|---|
| [Apple_Weatherkit](circuitpython/Apple_Weatherkit/code.py) | Adafruit IO weather integration, `receive_weather(2738)`; Apple WeatherKit as identified for this project | 300 | Current + minute forecasts: mean at least 8.19 min. Hourly and daily subsets rotate at different rates; see section 2. |
| [OM](circuitpython/OM/code.py) | Open-Meteo forecast endpoint; current data and 3 previous days | 29 | Current: at least 15 min 35 s plus startup/network. Historical publishing: every 24 cycles, roughly 6.25+ hours. |
| [Open_Meteo](circuitpython/Open_Meteo/code.py) | Same Open-Meteo endpoint; current data and 7 previous days | 33 | Current: at least 15 min 33 s plus startup/network, despite the 300-second constant. Historical publishing: every 24 cycles, roughly 6.25+ hours. |
| [Open_Weather](circuitpython/Open_Weather/code.py) | OpenWeather current weather, `/data/2.5/weather`, metric query | 24 | At least 30 min 12 s plus network/reload; includes 29.5 min of display/idling. |
| [WAPI](circuitpython/WAPI/code.py) | WeatherAPI current conditions, `/v1/current.json`, `aqi=yes` | 17 | At least 21 min 24 s plus network; reload after 12 fetches. |
| [NWS Alerts](circuitpython/NWS%20Alerts/code.py) | NWS active alerts at configured latitude/longitude | 5 | Fetch about every 5 min; rotate alert publishing on a 15-second check, suppressing unchanged values. |
| [NWS Forecasts](circuitpython/NWS%20Forecasts/code.py) | NWS `/gridpoints/MEG/54,68/forecast` | 16 | 14 separate NWS GETs per batch; next batch after 1 hour + 14 s of pauses + network. |
| [Feed-Avg](circuitpython/Feed-Avg/code.py) | Four current-temperature feeds already on Adafruit IO | 1 | About 5-10 min plus network, depending on eligible input count; approximately 10 min with all four valid. |

`OM` and `Open_Meteo` are overlapping source implementations, not distinct
providers. Both target the same current, scalar historical, and status keys.
The owner reports no duplicate Open-Meteo uploads and suspects that only one
device is running; the extra source copy was likely included accidentally.
Which version is deployed remains unconfirmed. Retain both here as source
variants, not evidence of two active producers. Do not combine their counts
when counting unique feeds or assume both should run on the ESP32.

## 2. Apple WeatherKit via Adafruit IO

### MQTT availability update (2026-10-24)

The integration API's `links.mqtt` response supplied by the owner advertises
these 11 source types at
`{username}/integration/weather/{location_id}/{type}`:

- `current`
- `forecast_minutes_5`, `forecast_minutes_30`
- `forecast_hours_1`, `forecast_hours_2`, `forecast_hours_6`,
  **`forecast_hours_12`**, `forecast_hours_24`
- `forecast_days_1`, `forecast_days_2`, `forecast_days_5`

The provider's documentation topic list omits `forecast_hours_12`, but the API
explicitly supplies its MQTT path. Live hour-12 delivery remains untested.
The diagnostic currently subscribes to the other ten topics; the main firmware
subscribes only to current, m5, m30, and hour1. This availability correction does
not enable additional subscriptions or change destination mappings.

The legacy hour-12 outputs are already inventoried under `f4.py` below and in
the companion CSV; no additional destination rows are needed.

### 2.1 Fetches, script chain, and timing

Each execution of the controller or a forecast handler independently calls
`io.receive_weather(LOCATION_ID)`, where `LOCATION_ID = 2738` in
[weather_helpers.py](circuitpython/Apple_Weatherkit/lib/weather_helpers.py).
There is no shared cached weather payload passed between files.

Normal flow:

```text
code.py -> selected handler:
  f1.py  (hour 1)  -> code.py
  f2.py  (hour 2)  -> code.py
  f3.py  (hour 6)  -> code.py
  f4.py  (hour 12) -> code.py
  f5.py  (hour 24) -> code.py
  f6.py -> f6a.py -> f7.py -> f7a.py -> f8.py -> f8a.py -> code.py
  f7.py -> f7a.py -> f8.py -> f8a.py -> code.py
  f8.py -> f8a.py -> code.py
```

The day handlers cover **days 1, 2, and 5**, not days 1, 2, and 3.
Selecting day 1 also updates days 2 and 5; selecting day 2 also updates day 5.
These are separate reads, so a chained set need not represent one atomic snapshot.

| Script | Weather payload subset | Weather outputs, excluding `rt` | Explicit successful-path wait |
|---|---|---:|---:|
| [code.py](circuitpython/Apple_Weatherkit/code.py) | `current`, `forecast_minutes_5`, `forecast_minutes_30` | 26 | 8 throttle checks with 10 s each + 10 s and 60 s display waits = 150 s |
| [f1.py](circuitpython/Apple_Weatherkit/f1.py) | `forecast_hours_1` | 21 | 5 checks x 10 s + 300 s = 350 s |
| [f2.py](circuitpython/Apple_Weatherkit/f2.py) | `forecast_hours_2` | 21 | 350 s |
| [f3.py](circuitpython/Apple_Weatherkit/f3.py) | `forecast_hours_6` | 21 | 350 s |
| [f4.py](circuitpython/Apple_Weatherkit/f4.py) | `forecast_hours_12` | 21 | 350 s |
| [f5.py](circuitpython/Apple_Weatherkit/f5.py) | `forecast_hours_24` | 21 | 350 s |
| [f6.py](circuitpython/Apple_Weatherkit/f6.py), [f7.py](circuitpython/Apple_Weatherkit/f7.py), [f8.py](circuitpython/Apple_Weatherkit/f8.py) | Day 1/2/5 `overnightForecast` | 15 each | 4 checks x 10 s + 10 s = 50 s each |
| [f6a.py](circuitpython/Apple_Weatherkit/f6a.py), [f7a.py](circuitpython/Apple_Weatherkit/f7a.py), [f8a.py](circuitpython/Apple_Weatherkit/f8a.py) | Day 1/2/5 whole-day and `daytimeForecast` | 41 each | 10 checks x 10 s + 10 s = 110 s each |

The helper's `sp(io)` additionally waits in 15-second increments while remaining
throttle allowance is below 20. Its final 10-second wait happens even without
throttling. Despite its name/docstring, `get_feeds_batch()` performs one
`get_feed()` request per name, not one HTTP batch operation.

#### NVM rotation details

The `runs` list contains 76 entries, but `MAX_NVM_VALUE` is 74. The controller:

1. Uses `runs[nvm[0]]` for values 0 through 74.
2. For a value above 74, resets the counter to 0 and sets `next_run = 0`.
3. Since 0 is not in `FILE_MAP`, selects the fallback `f8.py`.
4. Increments the counter before reloading, so subsequent normal rotation starts at 1.

Thus `runs[75]` is never selected, and `runs[0]` is used on a fresh counter but
not revisited during the normal steady rotation. This affects estimated rates.

The steady pattern is `runs[1:75]` followed by the fallback day-5 selection:
75 controller passes with 11 hour-1 selections, 12 each of hours 2/6/12/24,
5 day-1 selections, 5 day-2 selections, and 6 direct day-5 selections.
Its explicit waits total **36,860 seconds (10 h 14 min 20 s)**.

| Output set | Updates per steady rotation | Mean interval from explicit waits alone |
|---|---:|---:|
| Current and both minute sets | 75 | 8.19 min |
| Hour 1 | 11 | 55.85 min |
| Hours 2, 6, 12, 24, each | 12 | 51.19 min |
| Day 1 whole/daytime/overnight | 5 | 122.87 min |
| Day 2 whole/daytime/overnight | 10, including calls reached from day 1 | 61.43 min |
| Day 5 whole/daytime/overnight | 16, including calls reached from days 1 and 2 | 38.40 min |

These are **means, not fixed schedules**; actual means are longer. Current-data
sleep-only gaps vary from 310 to 630 seconds depending on the selected chain.
The minute labels describe forecast horizons, not 5- or 30-minute polling timers.
No board NVM value was available, so the current point within the rotation is unknown.

### 2.2 Current and minute mappings

All fields in the first table are relative to `current`.

| Incoming field | Adafruit IO feed | Published representation |
|---|---|---|
| `asOf` | `currentasof` | Raw timestamp |
| `cloudCover` | `currentcloudcover` | `int(x * 100)` percent |
| `cloudCoverLowAltPct` | `currentcloudcoverlowaltpct` | Percent |
| `cloudCoverMidAltPct` | `currentcloudcovermidaltpct` | Percent |
| `cloudCoverHighAltPct` | `currentcloudcoverhighaltpct` | Percent |
| `conditionCode` | `currentconditioncode` | Raw condition code |
| `daylight` | `currentdaylight` | `Its light` / `Its dark` |
| `humidity` | `currenthumidity` | Percent |
| `precipitationIntensity` | `currentprecipitationintensity` | Raw |
| `pressure` | `currentpressure` | Raw |
| `pressureTrend` | `currentpressuretrend` | Raw |
| `temperature` | `currenttemperature` | Celsius to Fahrenheit, 1 decimal |
| `temperatureApparent` | `currenttemperatureapparent` | Fahrenheit, 1 decimal |
| `temperatureDewPoint` | `currenttemperaturedewpoint` | Fahrenheit, 1 decimal |
| `visibility` | `currentvisibility` | Meters to whole miles, truncated |
| `windDirection` | `currentwinddegrees` | Degrees |
| `windDirection` | `currentwinddirection` | 16-point compass text |
| `windGust` | `currentwindgust` | `int(x * 0.62137119)` |
| `windSpeed` | `currentwindspeed` | `int(x * 0.62137119)` |
| `uvIndex` | `currentuv` | Raw index |

| Incoming path | Adafruit IO feed | Published representation |
|---|---|---|
| `forecast_minutes_5.startTime` | `m5starttime` | Raw timestamp |
| `forecast_minutes_5.precipitationChance` | `m5precipitationchance` | Percent |
| `forecast_minutes_5.precipitationIntensity` | `m5precipitationintensity` | Raw |
| `forecast_minutes_30.startTime` | `m30starttime` | Raw timestamp |
| `forecast_minutes_30.precipitationChance` | `m30precipitationchance` | Percent |
| `forecast_minutes_30.precipitationIntensity` | `m30precipitationintensity` | Raw |

### 2.3 Hourly mappings

For each `H` in **1, 2, 6, 12, 24**, read `forecast_hours_H` and publish the
following 21 feeds. `hoursH` is literal concatenation: for example,
`forecast_hours_12.temperature` -> `hours12temperature`.
All 105 expanded destinations appear individually in the CSV.

| Incoming field | Destination suffix after `hoursH` | Representation |
|---|---|---|
| `forecastStart` | `start` | Raw timestamp |
| `cloudCover` | `cloudcover` | Percent |
| `conditionCode` | `conditioncode` | Raw |
| `daylight` | `daylight` | `It will be light` / `It will be dark` |
| `humidity` | `humidity` | Percent |
| `precipitationAmount` | `precipitationamount` | Raw |
| `precipitationChance` | `precipitationchance` | Percent |
| `precipitationType` | `precipitationtype` | Raw |
| `precipitationIntensity` | `precipitationintensity` | Raw |
| `pressure` | `pressure` | Raw |
| `pressureTrend` | `pressuretrend` | Raw |
| `snowfallIntensity` | `snowfallintensity` | Raw |
| `snowfallAmount` | `snowfallamount` | Raw |
| `temperature` | `temperature` | Fahrenheit, 1 decimal |
| `temperatureApparent` | `temperatureapparent` | Fahrenheit, 1 decimal |
| `temperatureDewPoint` | `temperaturedewpoint` | Fahrenheit, 1 decimal |
| `uvIndex` | `uvindex` | Raw |
| `visibility` | `visibility` | Raw, unlike converted current visibility |
| `windDirection` | `winddirection` | 16-point compass text |
| `windGust` | `windgust` | Integer conversion with multiplier 0.62137119 |
| `windSpeed` | `windspeed` | Same conversion |

### 2.4 Daily mappings

For each `D` in **1, 2, 5**, read `forecast_days_D`.
Each day has 26 whole-day feeds, 15 daytime feeds, and 15 overnight feeds:
**56 per day, 168 total**.
All destinations are expanded in the CSV.

| Whole-day incoming field | Destination suffix after `daysD` | Representation |
|---|---|---|
| `forecastStart` | `start` | Raw timestamp |
| `forecastEnd` | `end` | Raw timestamp |
| `conditionCode` | `conditioncode` | Raw |
| `maxUvIndex` | `maxuvindex` | Raw |
| `moonPhase` | `moonphase` | Raw |
| `moonrise` | `moonrise` | Raw timestamp |
| `moonset` | `moonset` | Raw timestamp |
| `precipitationAmount` | `precipitationamount` | Raw |
| `precipitationChance` | `precipitationchance` | Percent |
| `precipitationType` | `precipitationtype` | Raw |
| `snowfallAmount` | `snowfallamount` | Raw |
| `solarMidnight` | `solarmidnight` | Raw timestamp |
| `solarNoon` | `solarnoon` | Raw timestamp |
| `sunrise` | `sunrise` | Raw timestamp |
| `sunriseCivil` | `sunrisecivil` | Raw timestamp |
| `sunriseNautical` | `sunrisenautical` | Raw timestamp |
| `sunriseAstronomical` | `sunriseastronomical` | Raw timestamp |
| `sunset` | `sunset` | Raw timestamp |
| `sunsetCivil` | `sunsetcivil` | Raw timestamp |
| `sunsetNautical` | `sunsetnautical` | Raw timestamp |
| `sunsetAstronomical` | `sunsetastronomical` | Raw timestamp |
| `temperatureMax` | `temperaturemax` | Fahrenheit, 1 decimal |
| `temperatureMin` | `temperaturemin` | Fahrenheit, 1 decimal |
| `windGustSpeedMax` | `windgustspeedmax` | Integer conversion with multiplier 0.62137119 |
| `windSpeedAvg` | `windspeedavg` | Same conversion |
| `windSpeedMax` | `windspeedmax` | Same conversion |

The following mapping applies twice per day:

- `forecast_days_D.daytimeForecast.FIELD` -> `daysDdaytimeSUFFIX`
- `forecast_days_D.overnightForecast.FIELD` -> `daysDovntSUFFIX`

For example, day-5 overnight temperature minimum goes to `days5ovnttemperaturemin`.

| Incoming subforecast field | Destination suffix | Representation |
|---|---|---|
| `forecastStart` | `start` | Raw timestamp |
| `forecastEnd` | `end` | Raw timestamp |
| `cloudCover` | `cloudcover` | Percent |
| `conditionCode` | `conditioncode` | Raw |
| `humidity` | `humidity` | Percent |
| `precipitationAmount` | `precipitationamount` | Raw |
| `precipitationChance` | `precipitationchance` | Percent |
| `precipitationType` | `precipitationtype` | Raw |
| `snowfallAmount` | `snowfallamount` | Raw |
| `temperatureMax` | `temperaturemax` | Fahrenheit, 1 decimal |
| `temperatureMin` | `temperaturemin` | Fahrenheit, 1 decimal |
| `windDirection` | `winddirection` | 16-point compass text |
| `windGustSpeedMax` | `windgustspeedmax` | Integer conversion with multiplier 0.62137119 |
| `windSpeed` | `windspeed` | Same conversion |
| `windSpeedMax` | `windspeedmax` | Same conversion |

### 2.5 Status, missing data, and unit caveats

- `rt` receives last-script status from all 12 active scripts, next-script status
  from the controller, and one message per missing label.
- Weather metrics total **299**: 26 current/minute + 105 hourly + 168 daily.
  Adding shared `rt` gives **300** destination keys.
- Missing labels are logged and published as string `"0"` by the fallback sender;
  missing data can therefore look like a real zero to consumers.
- Wind conversion is documented **as implemented**. The helper calls the input
  km/h and multiplies by 0.62137119. Actual integration response units need
  confirmation before deciding whether to preserve or correct that conversion.
- Raw pressure, precipitation, snowfall, and hourly visibility units are not
  established by these files. Current visibility is explicitly converted from
  meters, whereas hourly visibility is forwarded unchanged.

### 2.6 Additional file: Visual Crossing

[code1.py](circuitpython/Apple_Weatherkit/code1.py) is not part of the active
WeatherKit chain and does not publish weather data to Adafruit IO.
It queries Visual Crossing's timeline endpoint at hardcoded coordinates using
`unitGroup=us`.

It reads `currentConditions.datetime`, and loops over 15 dates, today through
today + 14 days, reading each date's `datetime`, `description`, and 24 hourly
entries: `datetime`, `temp`, `feelslike`, `humidity`, `dew`, `precip`,
`precipprob`, `preciptype`, `windgust`, `windspeed`, `winddir`, `pressure`,
`cloudcover`, `severerisk`, and `conditions`.
Only console output is produced; several precipitation/risk fields are read
without appearing in the printed summary.

For a complete successful iteration, the intended interval is 10 s +
15 x (24 x 0.5 s + 10 s) + 3,600 s = **65 min 40 s**, plus network/printing.
The date-range URLs are constructed once at startup and do not advance at midnight.
It also references `Color.violet`, which the supplied
[jpcolors.py](circuitpython/Apple_Weatherkit/lib/jpcolors.py) does not define;
entering that LED branch would raise `AttributeError`. Its standalone runtime
behavior was not tested.

## 3. Open-Meteo: both project folders

### 3.1 Source and schedule differences

Both use `http://api.open-meteo.com/v1/forecast` with configured latitude,
longitude, and timezone; request Fahrenheit, mph, and inch precipitation; and
request both `current` and `daily` fields.

| Behavior | `OM` | `Open_Meteo` |
|---|---|---|
| Requested previous days | 3 | 7 |
| Requested forecast days | 1 | 1 |
| Current idle loop | 900 iterations x 1 s = 900 s | 300 iterations x 3 x 1 s = 900 s |
| Explicit current publishing delays | 16 x 2 s + extra 2 s = 34 s | 15 x 2 s + extra 2 s = 32 s |
| End-of-cycle pause | 1 s | 1 s |
| Reload | After every cycle | After every cycle |
| Historical refresh trigger | Persistent counter 0; resets when 24 | Same |
| Additional historical GET | Daily-only, `forecast_days=0` | Full current/daily request again |
| Time metadata | Parsed API local time | Parsed API time plus fixed 5 hours |

The historical counter is in `microcontroller.nvm[0]`, so a software reload
does not reset the 24-cycle cadence. A fresh zero counter triggers history on the
first cycle. History refresh adds at least 21 seconds per day, plus API operations,
on its refresh cycle. A normal cycle includes another startup and feed-cache pass.
The 24-cycle interval is therefore **at least about 6.25 hours**, not 2 hours.
Historical data is also included in the primary GET each cycle even when not
published that cycle.

The older [optimization notes](circuitpython/Open_Meteo/OPTIMIZATIONS.md) and
[throttle notes](circuitpython/Open_Meteo/THROTTLE_FIX.md) describe other values
and estimates; this catalog follows the executable source instead.

### 3.2 Shared current destinations

These 16 weather outputs are published once per current cycle in both projects.

| Incoming current field / derivation | Adafruit IO feed | Representation |
|---|---|---|
| `time` | `om-when` | API local timestamp string |
| Wind speed, direction, gust | `om-winds` | Combined text with compass direction |
| `wind_speed_10m` | `om-windspeed` | mph |
| `wind_gusts_10m` | `om-windgust` | mph |
| `precipitation` | `om-totprec` | Raw, requested precipitation units |
| `rain` | `om-rain` | Raw, requested precipitation units |
| `showers` | `om-showers` | Raw, requested precipitation units |
| `snowfall` | `om-snow` | Raw; check API snowfall units |
| `relative_humidity_2m` | `om-hum` | Percent |
| `cloud_cover` | `om-clouds` | Percent |
| Current fields including `is_day` and weather description | `om-current` | Combined text summary |
| `temperature_2m` | `om-temp` | Fahrenheit |
| `apparent_temperature` | `om-fl` | Fahrenheit |
| `pressure_msl` | `om-pres` | Raw API pressure units |
| Current pressure versus mean of latest 5 `om-pres` samples | `om-pres-trend` | `Rising`, `Falling`, `Steady`, or `Unknown` |
| `weather_code` | `om-desc` | Local description lookup |

There is no separate current weather-code, numeric wind-direction, or day/night
feed. Those values are used for description or combined text.
`OM` gives the current summary a friendlier date format than `Open_Meteo`.

### 3.3 Shared historical destinations

On each historical refresh, each previous day is processed oldest to newest.
The same eight scalar feeds receive **three writes each in OM**, or **seven
writes each in Open_Meteo**, with that day's date as `created_at` metadata.
They are historical time series, not separate per-day scalar keys.

| Incoming daily field | Adafruit IO feed | Representation |
|---|---|---|
| `precipitation_sum[i]` | `omp-totprec` | Raw precipitation total |
| `rain_sum[i]` | `omp-rain` | Raw rain total |
| `showers_sum[i]` | `omp-showers` | Raw showers total |
| `snowfall_sum[i]` | `omp-snow` | Raw snowfall total |
| `temperature_2m_max[i]` | `omp-maxtemp` | Fahrenheit |
| `temperature_2m_min[i]` | `omp-mintemp` | Fahrenheit |
| `daylight_duration[i]` | `omp-daylighthours` | Seconds to hours, truncated to 1 decimal |
| `sunshine_duration[i]` | `omp-sunshinehrs` | Seconds to hours, truncated to 1 decimal |

Daily summary strings additionally include date, weather description, apparent
high/low temperatures, sunshine percentage, dominant wind direction, and maximum
gust. `precipitation_hours` is read but not included in the summary.

| Destination | `OM` | `Open_Meteo` | Meaning |
|---|---|---|---|
| `ompast.0` | Yes | Yes | Yesterday's summary |
| `ompast.1` | Yes | Yes | Two days ago |
| `ompast.2` | Yes | Yes | Three days ago |
| `ompast.3` | No | Yes | Four days ago |
| `ompast.4` | No | Yes | Five days ago |
| `ompast.5` | No | Yes | Six days ago |
| `ompast.6` | No | Yes | Seven days ago |
| `om-past` | Yes | Yes | **Current** summary copied during history refresh, not the daily history summary |
| `rt` | Yes | Yes | `OM Current Updated`; extra `117-OM Past Updated` on history refresh |

The endpoint also requests daily `sunrise`, `sunset`, and
`precipitation_probability_max` without publishing them. Its one forecast day
does not have a dedicated daily forecast output.

## 4. OpenWeather

Source: [Open_Weather/code.py](circuitpython/Open_Weather/code.py).
One metric current-weather GET and a read of the latest **six**
`openweather.owpressure` samples occur per cycle. The history read happens before
the new pressure value is sent.

All destinations below are prefixed `openweather.` and update once per cycle.

| Incoming field / derivation | Destination suffix | Representation |
|---|---|---|
| `weather[0].main` | `owmaintext` | Condition text |
| `weather[0].description` | `owdescription` | Description |
| `weather[0].id` | `owweatherid` | Numeric weather code |
| `weather[0].icon` | `owicon` | Icon identifier |
| `visibility` | `owvisibility` | Meters to whole miles, truncated |
| `dt` and `timezone` | `owasof` | Local `YYYY-MM-DD HH:MM` |
| `clouds.all` | `owclouds` | Percent |
| `main.pressure` | `owpressure` | hPa |
| `main.temp` | `owtemp` | Celsius to Fahrenheit |
| `main.feels_like` | `owfeelslike` | Fahrenheit |
| `main.temp_max` | `owmaxtemp` | Fahrenheit; not a daily forecast high |
| `main.temp_min` | `owmintemp` | Fahrenheit; not a daily forecast low |
| `wind.deg` | `owwinddirection` | Compass text using `windir2()` thresholds |
| `wind.speed` | `owwindspeed` | m/s to whole mph, truncated |
| `wind.gust` | `owwindgust` | m/s to whole mph, truncated |
| `rain["1h"]` | `owrain` | Raw mm; defaults to 0 if absent |
| `main.humidity` | `owhumidity` | Percent |
| `sys.sunrise` and `timezone` | `owsunrise` | Local `HH:MM` |
| `sys.sunset` and `timezone` | `owsunset` | Local `HH:MM` |
| `name` and `sys.country` | `owcity` | City, country |
| `timezone / 3600` | `owtimezone` | Offset hours |
| Current pressure versus mean of six historical samples | `owpresstrend` | `Rising`, `Falling`, `Steady` |
| Same trend | `owptval` | 1, -1, 0 respectively |

Shared `rt` receives `OpenWeather`.
Sea-level/ground-level pressure and coordinates are read but do not have
separate weather outputs. Missing max/min temperature defaults to 0 Celsius
before conversion, and missing gust defaults to zero.

Timing: each of 30 idle rounds contains two 4.5-second LED cycles, a 20-second
display rotation, and a 30-second sleep: **1,770 seconds** total.
Five `slow()` pauses add 40 seconds, and the post-fetch pause adds 2 seconds.
Minimum normal explicit wait is therefore **1,812 seconds (30 min 12 s)**.
Error animations and retries can substantially increase it.

## 5. WeatherAPI (WAPI)

Source: [WAPI/code.py](circuitpython/WAPI/code.py).
The request is `http://api.weatherapi.com/v1/current.json` for configured
coordinates, with air quality enabled.

| Incoming field / derivation | Adafruit IO feed | Representation |
|---|---|---|
| `current.air_quality["us-epa-index"]` | `wapi.aqi` | EPA index |
| `current.condition.code` | `wapi.cond-code` | Numeric code |
| `current.last_updated` | `wapi.as-of` | Raw timestamp |
| `current.cloud` | `wapi.clouds` | Percent |
| Condition-code lookup in local `wc` dictionary | `wapi.desc` | Description |
| `current.dewpoint_f` | `wapi.dew-point` | Fahrenheit |
| `current.feelslike_f` | `wapi.feels-like` | Fahrenheit |
| `current.heatindex_f` | `wapi.heat-index` | Fahrenheit |
| `current.humidity` | `wapi.humidity` | Percent |
| `current.precip_in` | `wapi.precip` | Inches |
| `current.pressure_mb` | `wapi.pressure` | Millibars |
| `current.temp_f` | `wapi.temperature` | Fahrenheit |
| `current.windchill_f` | `wapi.wind-chill` | Fahrenheit |
| `current.wind_degree` | `wapi.wind-dir` | Compass text |
| `current.gust_mph` | `wapi.wind-gust` | mph |
| `current.wind_mph` | `wapi.wind-speed` | mph |

Shared `rt` receives `WeatherAPI: <last_updated>`.
Location coordinates and `current.last_updated_epoch` are used to construct
metadata. Most metric writes include this metadata; condition code does not.
A failed metadata write is retried without metadata.

The sleeper has **30 rounds x 7 display items x (1 + 5 seconds) = 1,260 seconds**.
The two additional countdown displays make this 21 minutes, not the 15 minutes
suggested by earlier comments, and not the printed "5 minutes".
Six pre-send pauses add 24 seconds, giving **21 min 24 s + network** per fetch.
The board reloads after 12 fetches, at least **4 h 16 min 48 s** plus overhead.
The NWS alert URL assigned in this file is never requested.

## 6. NWS active alerts

Source: [NWS Alerts/code.py](circuitpython/NWS%20Alerts/code.py).
Input endpoint: `https://api.weather.gov/alerts/active?point={LATITUDE},{LONGITUDE}`.
Only the first **five** returned features are retained.

| Incoming fields / derivation | Adafruit IO feed | Publishing behavior |
|---|---|---|
| Sender, sent time, severity, urgency, event, `parameters.NWSheadline`, headline; alert ordinal/count | `weather-alerts` | Short formatted text for the selected alert |
| `parameters.NWSheadline` + headline | `headline-alert` | Headline text for the selected alert |
| Short alert text + description | `big-alert` | Full formatted alert |
| Number of retained features | `nbr-alerts` | 0-5, not necessarily the total number returned by NWS |
| Count + local fetch time | `rt` | Fetch status |

Fetching is checked on a 300-second monotonic timer. Publishing is checked on a
15-second timer and rotates one alert at a time. Each individual alert would
therefore recur about every `15 x retained_count` seconds while alerts are active.
All feeds suppress values equal to the last successfully sent value for that feed.
With only one unchanged alert, it is not repeatedly re-sent; with multiple alerts,
alternating values normally cause writes each rotation.

When there are no alerts, all three text feeds receive `No Current Alerts`,
then unchanged repeats are skipped. The no-alert animation/idling takes about
10.1 seconds per loop, so 15-second publishing checks are sampled roughly every
20.2 seconds in that state. Active-alert loops have about 1.1 seconds of LED waits.
The count is updated during successful fetches, only if changed. Failed fetches
retain the prior alert list. A fetch resets the rotation to the first alert.

`areaDesc` and `status` are extracted but not included in published text.
Metadata carries configured coordinates and local time; it does not defeat the
value-only deduplication.

## 7. NWS forecasts

Source: [NWS Forecasts/code.py](circuitpython/NWS%20Forecasts/code.py).
The source is a **hardcoded forecast gridpoint**:
`https://api.weather.gov/gridpoints/MEG/54,68/forecast`.
It does not derive the forecast URL from configured latitude/longitude.

| Incoming period | Adafruit IO feed |
|---|---|
| `properties.periods[0]` | `nws.00` |
| `properties.periods[1]` | `nws.01` |
| `properties.periods[2]` | `nws.02` |
| `properties.periods[3]` | `nws.03` |
| `properties.periods[4]` | `nws.04` |
| `properties.periods[5]` | `nws.05` |
| `properties.periods[6]` | `nws.06` |
| `properties.periods[7]` | `nws.07` |
| `properties.periods[8]` | `nws.08` |
| `properties.periods[9]` | `nws.09` |
| `properties.periods[10]` | `nws.10` |
| `properties.periods[11]` | `nws.11` |
| `properties.periods[12]` | `nws.12` |
| `properties.periods[13]` | `nws.13` |

Each output is combined text containing period name, date, start/end hours,
daytime high/nighttime low temperature labeled Fahrenheit, wind direction/speed,
and `detailedForecast`. These are NWS forecast periods, generally day/night
segments, **not 14 hourly forecasts or 14 whole days**.
`number` and `shortForecast` are read but not included in the output.

Each call to `get_forecast(period)` performs a new full forecast GET. Thus one
batch makes **14 NWS requests**, not one, and the periods need not all come from
one snapshot. Each is uploaded with a 1-second sleep afterward.
After the batch, the code reads the `nws.13` Adafruit IO data endpoint:

| Incoming / derivation | Destination | Behavior |
|---|---|---|
| Latest `nws.13` data point's `created_at` | `nws.when` | Formatted timestamp with fixed -6-hour adjustment |
| Successful nonempty `nws.13` read | `rt` | `Forecasts Updated` |

Then it sleeps for 3,600 seconds. Both auxiliary writes are skipped if that
history is empty. Startup has a separate 15-second pause.
[gettime.py](circuitpython/NWS%20Forecasts/gettime.py) is an unreferenced,
indented method fragment, not an additional forecast producer.

## 8. Temperature feed averager

Source: [Feed-Avg/code.py](circuitpython/Feed-Avg/code.py).
It reads each input's latest `value` and `created_at` from Adafruit IO.

| Incoming Adafruit IO feed | Producer in this source collection | Outgoing feed |
|---|---|---|
| `om-temp` | `OM` or `Open_Meteo` | `feed-avg` |
| `currenttemperature` | WeatherKit controller | `feed-avg` |
| `openweather.owtemp` | OpenWeather | `feed-avg` |
| `wapi.temperature` | WeatherAPI | `feed-avg` |

`gtemp` is in a commented-out configuration only and is **not** an active input.

The output is the arithmetic mean in Fahrenheit of successfully read inputs with
calculated age strictly below 30 minutes. It includes metadata `ele = valid_count`.
If no input is eligible, no average is sent.

Timing is data-dependent. Each of 100 sleep rounds blinks once per valid input
(1 second per blink), then sleeps 2 seconds:

| Eligible inputs | Idle time | Extra pre-send blinks |
|---:|---:|---:|
| 0 | 300 s, using one fallback blink | 1 s, no send |
| 1 | 300 s | 1 s |
| 2 | 400 s | 2 s |
| 3 | 500 s | 3 s |
| 4 | 600 s | 4 s |

Network/logging time is additional. A reload is checked at the start of an
iteration once elapsed time exceeds an hour, so it is not an exact hourly timer.
The age calculation uses local RTC time and a fixed **+5-hour correction**;
the diagnostic display separately subtracts six hours. Eligibility around DST
transitions or with mismatched timestamps needs validation before migration.
Negative calculated ages are not rejected by the current comparison.

## 9. Cross-project integration notes for the future ESP32

These are observations for later implementation, not firmware changes made here.

1. **Preserve the exact feed contract.** The CSV enumerates all keys, including
   punctuation and zero-padded NWS indices. Temperature averaging depends on four
   specific keys, not provider names.
2. **Confirm the deployed Open-Meteo version.** The owner reports no duplicate
   uploads and suspects only one device is active. Treat the two source folders
   as alternatives; confirm the deployed version's history depth and behavior
   before choosing the migration baseline.
3. **Use a shared upload budget.** `rt` is written by every project, and
   WeatherKit alone has 299 weather outputs. An individual script's pacing does
   not coordinate account-wide traffic from other boards.
4. **Fetch once, distribute many fields where feasible.** WeatherKit re-fetches
   in each script; NWS forecasts re-fetch the whole payload 14 times.
   Consolidation can avoid those redundant reads while retaining output keys.
5. **Separate fetch and publish schedules.** Forecast horizon labels are not
   polling intervals; alert rotation is distinct from NWS polling; historical
   Open-Meteo outputs are burst/backfill writes.
6. **Confirm units and timestamp policy with real payloads.** WeatherKit wind
   conversion and raw visibility differ by output family. Open-Meteo returns
   per-field unit metadata, including snowfall. Fixed timezone corrections differ
   among projects and affect the averager's freshness filter.
7. **Decide which quirks are intentional before porting.** Examples are the
   WeatherKit NVM boundary/fallback, day-5 rather than day-3 outputs, zero-filled
   missing WeatherKit values, capped alert count, and `om-past` containing current
   conditions. They are cataloged here rather than silently corrected.
8. **Revisit freshness targets.** The OpenWeather cycle slightly exceeds the
   averager's 30-minute cutoff even before additional overhead, so that input may
   periodically be excluded. Provider observation timestamps and metadata make
   actual eligibility more complex than loop frequency alone.

## 10. Verification and limits

The catalog was checked by parsing the source without importing or executing
CircuitPython board code:

- All 12 active WeatherKit scripts' send mappings were extracted; normalized
  comparisons confirmed the hour/day handler variants share the documented
  schema, with the final day handler returning to the controller.
- The rotation list and NVM boundary were used to calculate the steady-state
  means, including indirect daily-handler executions.
- Destination counts were checked per project and deduplicated overall.
- The CSV was checked against the source feed declarations and send sites.

No live provider response, Adafruit IO account, installed library implementation,
board clock, board NVM state, or deployed firmware version was inspected.
Consequently this describes the supplied files, not guaranteed current device
behavior or provider-side update rates.

## 11. Observed operation from the supplied `rt` export

### 11.1 Evidence and interpretation

The owner supplied `rt-20261020-0133.csv` from Downloads. It was read locally;
no API calls or credentials were needed. All **65,816 CSV records** parsed
successfully. Records span **2026-08-21 01:21:20 UTC through
2026-10-20 01:32:57 UTC**, almost 60 days.

This section uses the CSV's `created_at` timestamp, not the timestamp embedded
in a status value. All dates/times below are **UTC**. The recent-week window
is the seven days ending at the final export timestamp; the recent-48-hour
window ends at that same timestamp.

Status events are not direct measurements of every weather metric's upload.
In particular, `rt` can be sent after an earlier upload failed, and missing
statuses do not by themselves prove a device was powered off. Feed-Avg does not
write `rt`, so this export cannot establish its runtime activity.
The CSV inventory remains a source-level mapping; this section supplies the
separate runtime evidence.

### 11.2 Active status writers and observed intervals

| Status family | Events in recent week | Median gap in recent week | Interpretation |
|---|---:|---|---|
| WeatherKit controller `Last: code.py` | 801 | 8 min 58 s | Current/minute controller is active; includes a long reporting gap in this window |
| `OM Current Updated` | 613 | 16 min 21 s | Stable current-update cadence, with no overlapping fast second cadence evident |
| `117-OM Past Updated` | 25 | 6 h 33 min 40 s | Exactly 24 current-status events between each adjacent pair of these recent history statuses |
| `OpenWeather` | 234 | 30 min 57 s | Normal cadence near source estimate, but with a long reporting gap and irregular recent restart activity |
| `WeatherAPI: ...` | 272 | 48 min 53 s | Bimodal intervals; this single median is not a useful normal schedule |
| `Forecasts Updated` | 150 | 60 min 56 s | Stable hourly reporting after resumption |
| `Fetched N alerts ...` | 1,986 | 5 min 4 s | NWS fetch statuses; all report zero alerts in this week |
| `Garage update` | 2,016 | 5 min | Additional shared-feed writer not represented in the supplied project folders |

Across the full export, normal medians are approximately the same for
Open-Meteo, OpenWeather, NWS forecasts, and NWS alerts.
WeatherAPI's full-export median is instead 23 min 15 s, illustrating why a
single historical median can conceal behavior changes or alternating delays.

**Open-Meteo version:** The single approximately 16-minute stream and the owner's
observation are consistent with one active producer, not duplicate weather uploads.
Recent historical completion statuses occur **70-75 seconds after the preceding
current status**, with a median of 71 seconds. That timing fits the three-day
`OM` version's roughly 63 seconds of per-day waits plus request/send overhead.
The supplied seven-day `Open_Meteo` path has at least 147 seconds of those waits
on a successful complete history pass. This makes **`OM` the better-supported
deployment hypothesis**, not a confirmed identity: errors, changed deployed code,
or incomplete passes could produce different timings. Recent historical feed
contents or the actual board files would settle the question.

**WeatherAPI timing:** In the recent-week portion before 2026-10-20 00:00 UTC,
the 264 consecutive status gaps split into:

- **119 gaps of 21-25 minutes**, median **21 min 46 s**.
- **145 gaps of 45-52 minutes**, median **51 min 13 s**.

The second group is substantially slower than the supplied source's normal
successful path. The export alone cannot determine whether this is caused by
failed/missing status sends, retries, connection behavior, or a different deployed
version. Later events near the export end include much shorter gaps, so a
single fixed polling interval should not be inferred from this history.

### 11.3 WeatherKit: observed handler completion rates

These measurements use `Last: <script>` completion statuses in the **last
48 hours**, which avoids the long controller gaps earlier in the recent week.
The mean is the mean of consecutive observed gaps for that script, not a
provider refresh rate. Daily updates are deliberately clustered by chaining.

| Script/output group | Completion events | Mean gap | Median gap |
|---|---:|---|---|
| Controller: current + minute sets | 313 | 9.21 min | 8.97 min |
| Hour 1, `f1.py` | 45 | 62.67 min | 45.33 min |
| Hour 2, `f2.py` | 49 | 57.38 min | 45.10 min |
| Hour 6, `f3.py` | 49 | 57.58 min | 45.08 min |
| Hour 12, `f4.py` | 49 | 57.58 min | 45.08 min |
| Hour 24, `f5.py` | 50 | 57.94 min | 45.12 min |
| Day 1 overnight, `f6.py` | 22 | 135.20 min | 164.90 min |
| Day 1 whole/daytime, `f6a.py` | 22 | 135.20 min | 164.88 min |
| Day 2 overnight, `f7.py` | 44 | 66.27 min | 10.90 min |
| Day 2 whole/daytime, `f7a.py` | 44 | 66.27 min | 10.92 min |
| Day 5 overnight, `f8.py` | 69 | 41.90 min | 10.18 min |
| Day 5 whole/daytime, `f8a.py` | 69 | 41.90 min | 10.18 min |

The small day-2/day-5 medians do **not** mean those outputs update uniformly every
10 minutes. Their longer gaps reach about 198 and 185 minutes respectively in
this window. The observed chain near the end of the export includes:

```text
2026-10-20 UTC
01:07:35  Next: f6.py
01:09:28  Last: f6.py
01:12:04  Last: f6a.py
01:13:06  Last: f7.py
01:15:43  Last: f7a.py
01:16:45  Last: f8.py
01:19:23  Last: f8a.py
01:21:14  Last: code.py
```

This directly supports the documented day-1 -> day-2 -> day-5 chain.
There is also one mismatch between an announced next handler and the next
WeatherKit status in the 48-hour window: `Next: f3.py` at 00:31:04 on October 20
is followed by `Last: code.py` at 00:40:16, without an intervening `Last: f3.py`.
That shows an incomplete status sequence, not proof of whether the handler ran.

### 11.4 Reporting gaps and irregular recent events

| Writer | Last status before gap (UTC) | First status after gap (UTC) | Gap |
|---|---|---|---|
| NWS forecasts | Oct 8 15:58:59 | Oct 13 19:20:25 | 123 h 21 min 26 s |
| WeatherKit controller | Oct 15 10:40:51 | Oct 17 04:44:55 | 42 h 4 min 4 s |
| WeatherKit controller | Oct 17 12:23:56 | Oct 17 15:48:12 | 3 h 24 min 16 s |
| OpenWeather | Oct 18 01:20:13 | Oct 20 00:20:42 | 47 h 0 min 29 s |

Other writers continued reporting during these intervals. The largest gap
between **any** successive `rt` records in the recent week is only 5 min 1 s.
These are therefore gaps in particular status streams, not equivalent gaps in
the entire exported feed. Their causes are not established by `rt`.

OpenWeather's final six statuses occur at 00:20:42, 00:41:05, 00:43:14,
00:55:30, 01:02:00, and 01:32:57 UTC on October 20.
Several intervals are much shorter than the normal 30-minute loop and are
consistent with restarts/manual activity or different execution paths; the
export does not identify the cause. Its last interval returns to 30 min 57 s.

### 11.5 Missing-field messages and remaining unknowns

There are **166 WeatherKit missing-field messages** in the full export, all
for daily moonrise/moonset fields:

| Missing label | Messages |
|---|---:|
| `days5.moonrise` | 44 |
| `days2.moonset` | 38 |
| `days5.moonset` | 33 |
| `days2.moonrise` | 21 |
| `days1.moonset` | 19 |
| `days1.moonrise` | 11 |

They span August 22 through October 11; none occur in the recent week.
The helper publishes string `"0"` for these missing fields. Missing moon events
may be legitimate for a particular day; these messages alone do not establish
an API failure.

The export confirms recent activity for WeatherKit, Open-Meteo, OpenWeather,
WeatherAPI, NWS alerts, and NWS forecasts. It does **not** establish:

- Whether Feed-Avg is currently running or which temperatures it accepted.
- Whether every weather-feed upload succeeded when a status was emitted.
- Which exact Open-Meteo source copy is deployed.
- Why a particular writer stopped reporting or later emitted rapid statuses.
- Actual provider-side observation/cache refresh rates.

`Garage update` also demonstrates that the account-wide upload budget includes
at least one writer outside the supplied weather project collection.
