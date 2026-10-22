import board
import time
import gc
import os
import rtc
import wifi
import supervisor
import adafruit_connection_manager
import adafruit_requests
import digitalio
from tm1637_display import TM1637Display
from adafruit_io.adafruit_io import IO_HTTP, AdafruitIO_RequestError

DEBUG = os.getenv("DEBUG", "1") == "1"


def dbg(msg):
    if DEBUG:
        print("[DEBUG]", msg)


def log_error(stage, err):
    print("[ERROR]", stage, err)


def send_feed(io, feed_key, value, metadata=None):
    try:
        if metadata is None:
            io.send_data(feed_key, value)
        else:
            io.send_data(feed_key, value, metadata)
        dbg("aio: sent " + feed_key)
        return True
    except Exception as e:
        log_error("aio: send failed for " + feed_key + ":", e)
        if metadata is not None:
            try:
                dbg("aio: retry without metadata for " + feed_key)
                io.send_data(feed_key, value)
                dbg("aio: sent without metadata " + feed_key)
                return True
            except Exception as e2:
                log_error("aio: retry failed for " + feed_key + ":", e2)
        return False

def set_clock(io):
    dbg("set_clock: requesting time from Adafruit IO")
    try:
        now = time.struct_time(io.receive_time(os.getenv("TIMEZONE")))
        r = rtc.RTC()
        r.datetime = time.struct_time((now.tm_year, now.tm_mon, now.tm_mday,
                                        now.tm_hour, now.tm_min, now.tm_sec, 0, -1, -1))
        print("RTC Updated from Adafruit IO")
    except Exception as e:
        log_error("set_clock: failed to set RTC from AIO:", e)


def sp(label=""):
    if label:
        dbg("pause: " + label)
    else:
        dbg("pause")
    bleep()
    time.sleep(3)
    bleep()

def mem():

    # Get the total allocated memory
    total_allocated = gc.mem_alloc()

    # Get the total free memory
    total_free = gc.mem_free()

    # Print the memory usage
    print("*"*40)
    print("Total allocated memory: {} bytes".format(total_allocated))
    print("Total free memory: {} bytes".format(total_free))
    print("Total memory: {} bytes".format(total_allocated + total_free))
    print("\n")

def windir(wd):
    directions = ["North", "NNE", "NE", "ENE", "East", "ESE", "SE", "SSE",
                  "South", "SSW", "SW", "WSW", "West", "WNW", "NW", "NNW"]
    # 360 degrees / 16 segments = 22.5 degrees per segment.
    # Offset by 11.25 to center the segment on the direction.
    index = int((wd + 11.25) / 22.5) % 16
    return directions[index]

def bleep():
    led.value =False
    time.sleep(.25)
    led.value = True
    time.sleep(.25)

def life():
    c = 0
    while c < 5:
        led.value = True
        time.sleep(.25)
        led.value = False
        time.sleep(.25)
        c = c + 1

wc = {1000 : 'Clear',
        1003 : 'Partly cloudy',
        1006 : 'Cloudy',
        1009 : 'Overcast',
        1030 : 'Mist',
        1063 : 'Patchy rain possible',
        1066 : 'Patchy snow possible',
        1069 : 'Patchy sleet possible',
        1072 : 'Patchy freezing drizzle possible',
        1087 : 'Thundery outbreaks possible',
        1114 : 'Blowing snow',
        1117 : 'Blizzard',
        1135 : 'Fog',
        1147 : 'Freezing fog',
        1150 : 'Patchy light drizzle',
        1153 : 'Light drizzle',
        1168 : 'Freezing drizzle',
        1171 : 'Heavy freezing drizzle',
        1180 : 'Patchy light rain',
        1183 : 'Light rain',
        1186 : 'Moderate rain at times',
        1189 : 'Moderate rain',
        1192 : 'Heavy rain at times',
        1195 : 'Heavy rain',
        1198 : 'Light freezing rain',
        1201 : 'Moderate or heavy freezing rain',
        1204 : 'Light sleet',
        1207 : 'Moderate or heavy sleet',
        1210 : 'Patchy light snow',
        1213 : 'Light snow',
        1216 : 'Patchy moderate snow',
        1219 : 'Moderate snow',
        1222 : 'Patchy heavy snow',
        1225 : 'Heavy snow',
        1237 : 'Ice pellets',
        1240 : 'Light rain shower',
        1243 : 'Moderate or heavy rain shower',
        1246 : 'Torrential rain shower',
        1249 : 'Light sleet showers',
        1252 : 'Moderate or heavy sleet showers',
        1255 : 'Light snow showers',
        1258 : 'Moderate or heavy snow showers',
        1261 : 'Light showers of ice pellets',
        1264 : 'Moderate or heavy showers of ice pellets',
        1273 : 'Patchy light rain with thunder',
        1276 : 'Moderate or heavy rain with thunder',
        1279 : 'Patchy light snow with thunder',
        1282 : 'Moderate or heavy snow with thunder'}

def sleeper(t, fl, wc, hi, dp, current_loop_idx):
    slp = 1
    xr = 5
    # Total loops = 12. current_loop_idx goes 0..11.
    # Each sleeper call runs 30 sub-loops.
    # Total time per sub-loop is roughly:
    # 5 items * (1s + 5s) = 30s + overhead? No.
    # Let's see: values list has 5 items.
    # Inner loop runs 5 times.
    # Each inner item: 1s label + 5s value = 6s.
    # Total inner loop = 5 * 6s = 30s.
    # Total sleeper run = 30 * 30s = 900s (15 mins).
    
    # We want to display:
    # 5: Time to next fetch (in minutes)
    # 6: Time to next reboot (in minutes)
    
    # Remaining sub-loops in this sleeper call: 30 - i
    # Loops remaining until reboot: (12 - current_loop_idx - 1) * 30 + (30 - i)
    
    values = [t, fl, wc, hi, dp]
    
    # Loop 30 times (each iteration is ~30 seconds)
    for i in range(30):
        # Calculate times
        loops_left_in_sleeper = 30 - i
        minutes_to_fetch = int((loops_left_in_sleeper * 30) / 60)
        
        # Reboot is at end of loop 11.
        loops_remaining_total = ((11 - current_loop_idx) * 30) + loops_left_in_sleeper
        minutes_to_reboot = int((loops_remaining_total * 30) / 60)

        # Standard values 0-4
        for idx, val in enumerate(values):
            dgts.print(f"{idx}   ")
            time.sleep(slp)
            dgts.print(str(int(val)))
            time.sleep(slp * xr)
            
        # 5: Time to Fetch (Minutes)
        dgts.print("5   ")
        time.sleep(slp)
        dgts.print(f"{minutes_to_fetch:04d}") # Show as 0015 etc
        time.sleep(slp * xr)
        
        # 6: Time to Reboot (Minutes)
        dgts.print("6   ")
        time.sleep(slp)
        dgts.print(f"{minutes_to_reboot:04d}")
        time.sleep(slp * xr)


try:
    print("Starting Now")
    dbg("startup: initializing display")
    dgts = TM1637Display(board.GP5, board.GP4, length=4, digit_order=(0, 1, 2, 3))
    dgts.print("0123")
    dbg("startup: initializing LED")
    led = digitalio.DigitalInOut(board.LED)
    led.direction = digitalio.Direction.OUTPUT
    dbg("startup: first life blink")
    life()
    aio_username = os.getenv('AIO_USERNAME')
    aio_key = os.getenv('AIO_KEY')
    wapi = os.getenv('WEATHERAPI_KEY')
    wapi_loc = os.getenv('LATITUDE') + "," + os.getenv('LONGITUDE')
    #print(wapi_loc)
    life()
    try:
        dbg("wifi: connecting")
        wifi.radio.connect(
        os.getenv("CIRCUITPY_WIFI_SSID"), os.getenv("CIRCUITPY_WIFI_PASSWORD")
        )
        print(f"Connected to {os.getenv('CIRCUITPY_WIFI_SSID')}")
        print(wifi.radio.ipv4_address)
    except Exception as e:
        log_error("wifi: failed to connect:", e)
        time.sleep(15)
        supervisor.reload()
    life()
    dbg("network: creating requests session")
    radio = wifi.radio
    pool = adafruit_connection_manager.get_radio_socketpool(radio)
    ssl_context = adafruit_connection_manager.get_radio_ssl_context(radio)
    requests = adafruit_requests.Session(pool, ssl_context)
    life()
    # Initialize IO and Feeds outside the loop
    aio_username = os.getenv('AIO_USERNAME')
    aio_key = os.getenv('AIO_KEY')
    dbg("aio: creating IO_HTTP client")
    io = IO_HTTP(aio_username, aio_key, requests)

    # Set RTC on startup (and effective every ~1h reload)
    set_clock(io)

    FEED_KEYS = {
        "air_quality": "wapi.aqi",
        "cond_code": "wapi.cond-code",
        "as_of": "wapi.as-of",
        "clouds": "wapi.clouds",
        "weather_desc": "wapi.desc",
        "dew_point": "wapi.dew-point",
        "feels_like": "wapi.feels-like",
        "heat_index": "wapi.heat-index",
        "humidity": "wapi.humidity",
        "precip": "wapi.precip",
        "pressure": "wapi.pressure",
        "temp": "wapi.temperature",
        "wind_chill": "wapi.wind-chill",
        "wind_dir": "wapi.wind-dir",
        "wind_gust": "wapi.wind-gust",
        "wind_speed": "wapi.wind-speed",
        "rt": "rt",
    }
    print("Using static Adafruit IO feed keys (skipping startup feed lookup).")
    dbg("main: entering fetch/send loop")

    x = 0
    while x < 12:
        dbg("loop: starting cycle " + str(x + 1) + " of 12")
        led.value = False
        #mem()
        JSON_CURRENT = "http://api.weatherapi.com/v1/current.json?key="
        JSON_CURRENT = JSON_CURRENT + wapi + "&q="
        JSON_CURRENT = JSON_CURRENT + wapi_loc + "&aqi=yes"
        dbg("weatherapi: request URL built")
        print(JSON_CURRENT)
        JSON_LOCAL_ALERTS = "https://api.weather.gov/alerts/active?zone=TNZ088"


        try:
            dbg("weatherapi: requesting current conditions")
            response = requests.get(JSON_CURRENT)
        except Exception as e:
            log_error("weatherapi: failed to get data:", e)
            time.sleep(15)
            supervisor.reload()
        dbg("weatherapi: response received, parsing JSON")
        try:
            payload = response.json()
        except Exception as e:
            log_error("weatherapi: invalid JSON payload:", e)
            response.close()
            time.sleep(15)
            supervisor.reload()

        lat = payload['location']['lat']
        lon = payload['location']['lon']
        as_of = payload['current']['last_updated']

        as_of_epoch = payload['current']['last_updated_epoch']
        bt = as_of_epoch - (60 * 60 * 6)
        tt = time.localtime(as_of_epoch)
        ut = str(tt.tm_year) + "-" + str(tt.tm_mon) + "-" + str(tt.tm_mday) + " " + str(tt.tm_hour) + ":" + str(tt.tm_min)
        metadata = {"lat": lat, "lon": lon, "created_at": ut}

        weather_code = payload['current']['condition']['code']
        weather_desc = wc[weather_code]
        print("Weather Desc: ", weather_desc)
        temp = payload['current']['temp_f']
        feels_like = payload['current']['feelslike_f']
        wind_chill = payload['current']['windchill_f']
        heat_index = payload['current']['heatindex_f']
        dew_point = payload['current']['dewpoint_f']
        air_quality = payload['current']['air_quality']['us-epa-index']
        print(air_quality)
        wind_speed = payload['current']['wind_mph']
        wind_gust = payload['current']['gust_mph']
        wind_degree = payload['current']['wind_degree']
        wind_dir = windir(wind_degree)
        humidity = payload['current']['humidity']
        pressure = payload['current']['pressure_mb']
        precip = payload['current']['precip_in']
        clouds = payload['current']['cloud']

        response.close()
        dbg("weatherapi: parse complete, response closed")

        # Feed Definitions moved outside loop

        sp("before send batch 1")
        dbg("aio: sending batch 1")
        send_feed(io, FEED_KEYS["air_quality"], air_quality, metadata)
        send_feed(io, FEED_KEYS["as_of"], as_of, metadata)
        send_feed(io, FEED_KEYS["clouds"], clouds, metadata)
        dbg("aio: batch 1 complete")

        sp("before send batch 2")
        dbg("aio: sending batch 2")
        send_feed(io, FEED_KEYS["weather_desc"], weather_desc, metadata)
        send_feed(io, FEED_KEYS["cond_code"], weather_code)
        send_feed(io, FEED_KEYS["dew_point"], dew_point, metadata)
        send_feed(io, FEED_KEYS["feels_like"], feels_like, metadata)
        dbg("aio: batch 2 complete")

        sp("before send batch 3")
        dbg("aio: sending batch 3")
        send_feed(io, FEED_KEYS["heat_index"], heat_index, metadata)
        send_feed(io, FEED_KEYS["humidity"], humidity, metadata)
        send_feed(io, FEED_KEYS["precip"], precip, metadata)
        dbg("aio: batch 3 complete")

        sp("before send batch 4")
        dbg("aio: sending batch 4")
        send_feed(io, FEED_KEYS["pressure"], pressure, metadata)
        send_feed(io, FEED_KEYS["temp"], temp, metadata)
        send_feed(io, FEED_KEYS["wind_chill"], wind_chill, metadata)
        dbg("aio: batch 4 complete")

        sp("before send batch 5")
        dbg("aio: sending batch 5")
        send_feed(io, FEED_KEYS["wind_dir"], wind_dir, metadata)
        send_feed(io, FEED_KEYS["wind_gust"], wind_gust, metadata)
        send_feed(io, FEED_KEYS["wind_speed"], wind_speed, metadata)
        dbg("aio: batch 5 complete")

        sp("before RT heartbeat")
        print("Done sending")
        #print(as_of_epoch, weather_desc, as_of, temp, feels_like, wind_chill, heat_index, wind_degree, wind_dir, dew_point, air_quality, humidity, pressure, wind_speed, wind_gust)
        try:
            dbg("aio: sending rt heartbeat")
            io.send_data(FEED_KEYS["rt"], "WeatherAPI: " + as_of)
            print("Sent RT")
        except Exception as e:
            log_error("aio: could not update RT:", e)
        led.value = True
        print("pausing 5 minutes")
        #time.sleep(300)
        dbg("sleep: entering sleeper loop")
        sleeper(temp, feels_like, wind_chill, heat_index, dew_point, x)
        dbg("sleep: sleeper loop complete")
        x = x + 1

    supervisor.reload()
except Exception as e:
    log_error("fatal: unhandled exception in main:", e)
    time.sleep(15)
    supervisor.reload()
