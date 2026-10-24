#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include <stddef.h>

struct WiFiCredential {
    const char* ssid;
    const char* password;
};

// Copy to credentials.h and replace the empty entries with your networks.
const WiFiCredential wifiNetworks[] = {
    {"", ""},
    {"", ""}
};

const size_t WIFI_NETWORK_COUNT =
    sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
// POSIX timezone rule, not an IANA location name. Set your local rule here.
#define TZ_STRING "UTC0"

#define AIO_USERNAME ""
#define AIO_KEY ""
// Adafruit IO WeatherKit Power-Up location ID (IO+), shown on the weather
// services page. Used as integration/weather/<ID>/<forecast type> over MQTT.
#define AIO_WEATHER_LOCATION_ID ""

// Data-provider credentials. Replace placeholders in credentials.h only.
#define OPENWEATHER_KEY "YOUR_OPENWEATHER_KEY"
#define WEATHERAPI_KEY "YOUR_WEATHERAPI_KEY"
#define VISUAL_CROSSING_KEY "YOUR_VISUAL_CROSSING_KEY"
#define WWO_KEY "YOUR_WORLD_WEATHER_ONLINE_KEY"
#define EXCHANGE_RATE_KEY "YOUR_EXCHANGE_RATE_KEY"

// Decimal-degree strings for location-based requests. Replace in credentials.h.
#define LATITUDE "0.0"
#define LONGITUDE "0.0"

// Location label and IANA timezone for provider API requests.
#define LOCATION "YOUR_CITY, COUNTRY"
#define TIMEZONE "Etc/UTC"

#endif
