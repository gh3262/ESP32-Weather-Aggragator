import board
import time
import os
import rtc
import ssl
import wifi
import socketpool
import adafruit_requests
import gc
import supervisor
from tm1637_display import TM1637Display
import neopixel
from adafruit_io.adafruit_io import IO_HTTP, AdafruitIO_RequestError
from jpcolors import Color
import microcontroller

time.sleep(2)

# Configuration Constants
UPDATE_INTERVAL = 900  # seconds between weather updates (15 minutes)
PAST_DATA_INTERVAL = 24  # update cycles before refreshing past data
LED_BRIGHTNESS = 0.1
FORECAST_DAYS_PAST = 3  # reduced to ease memory usage
FORECAST_DAYS_FUTURE = 1
API_RETRY_DELAY = 2  # seconds
API_MAX_RETRIES = 3
AIO_SEND_DELAY = 2  # seconds between Adafruit IO sends (rate limiting - increased to avoid throttling)
DEBUG = False

MONTHS = [
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
]

# Wind direction lookup table
WIND_DIRS = ["North", "NNE", "NE", "ENE", "East", "ESE", "SE", "SSE",
             "South", "SSW", "SW", "WSW", "West", "WNW", "NW", "NNW"]


def safe_api_call(func, max_retries=API_MAX_RETRIES, delay=API_RETRY_DELAY):
    """Safely call a function with retry logic instead of reloading."""
    for attempt in range(max_retries):
        try:
            return func()
        except Exception as e:
            print(f"Attempt {attempt+1} failed: {e}")
            if attempt < max_retries - 1:
                time.sleep(delay)
            else:
                print("Max retries reached. Reloading...")
                time.sleep(15)
                supervisor.reload()


def send_with_delay(feed, value, metadata=None, delay=AIO_SEND_DELAY, retries=API_MAX_RETRIES):
    """Send to Adafruit IO with retry and built-in pacing."""
    key = feed['key'] if isinstance(feed, dict) else feed
    for attempt in range(retries):
        try:
            if metadata is None:
                io.send_data(key, value)
            else:
                io.send_data(key, value, metadata)
            time.sleep(delay)
            return
        except Exception as e:
            print(f"send_with_delay failed attempt {attempt+1}/{retries} for {key}: {e}")
            if attempt < retries - 1:
                time.sleep(API_RETRY_DELAY)



def m(i):
    if not DEBUG:
        return
    total_allocated = gc.mem_alloc()
    total_free = gc.mem_free()
    before = total_free
    gc.collect()
    print("\n>>> ", i, before, gc.mem_free())

def windir(wd):
    """Convert wind degrees to cardinal direction using lookup table."""
    index = int((wd + 11.25) / 22.5) % 16
    return WIND_DIRS[index]

def convert_to_struct_time(datetime_str):
    # Parse the date-time string
    year, month, day = map(int, datetime_str[:10].split('-'))
    hour, minute = map(int, datetime_str[11:16].split(':'))

    # Create struct_time
    dt = time.struct_time((year, month, day, hour, minute, 0, -1, -1, -1))
    
    return dt


def pretty_date_time(dt):
    hour_12 = dt.tm_hour % 12 or 12
    period = "am" if dt.tm_hour < 12 else "pm"
    return f"{dt.tm_mday} {MONTHS[dt.tm_mon - 1]} at {hour_12}:{dt.tm_min:02d} {period}"
        
    
wc = {0 : 'Clear sky',
            1 : 'Mainly clear',
            2 : 'Partly cloudy',
            3 : 'Overcast',
            45 : 'Fog',
            48 : 'Depositing rime fog',
            51 : 'Light drizzle',
            53 : 'Moderate drizzle',
            55 : 'Dense drizzle',
            56 : 'Light freezing drizzle',
            57 : 'Dense freezing drizzle',
            61 : 'Slight rain',
            62 : 'Moderate rain',
            63 : 'Rain',
            65 : 'Heavy rain',
            66 : 'Light freezing rain',
            67 : 'Heavy freezing rain',
            71 : 'Slight snowfall',
            73 : 'Moderate snowfall',
            75 : 'Heavy snowfall',
            77 : 'Snow grains',
            80 : 'Slight rain showers',
            81 : 'Moderate rain showers',
            82 : 'Violent rain showers',
            85 : 'Slight snow showers',
            86 : 'Heavy snow showers',
            95 : 'Thunderstorms - slight or moderate',
            96 : 'Thunderstorms with slight hail',
            113 : 'Clear', 
            116 : 'Partly cloudy', 
            119 : 'Cloudy', 
            122 : 'Overcast', 
            143 : 'Mist', 
            176 : 'Patchy rain possible', 
            179 : 'Patchy snow possible', 
            182 : 'Patchy sleet possible', 
            185 : 'Patchy freezing drizzle possible', 
            200 : 'Thundery outbreaks possible', 
            227 : 'Blowing snow', 
            230 : 'Blizzard', 
            248 : 'Fog', 
            260 : 'Freezing fog', 
            263 : 'Patchy light drizzle', 
            266 : 'Light drizzle', 
            281 : 'Freezing drizzle', 
            284 : 'Heavy freezing drizzle', 
            293 : 'Patchy light rain', 
            296 : 'Light rain', 
            299 : 'Moderate rain at times', 
            302 : 'Moderate rain', 
            305 : 'Heavy rain at times', 
            308 : 'Heavy rain', 
            311 : 'Light freezing rain'
}
#time.sleep(300)
m(1)
print("starting")
dgts = TM1637Display(board.SCL, board.SDA)
dgts.print("0123")
np = neopixel.NeoPixel(board.NEOPIXEL, 1)
np.brightness = LED_BRIGHTNESS
np[0] = Color.red
m(2)
#pixel_pin = board.NEOPIXEL
#num_pixels = 32
#pixels =  neopixel.NeoPixel(pixel_pin, num_pixels, brightness=0.5, auto_write=False)
m(3)
try:
    pool = socketpool.SocketPool(wifi.radio)
    https = ssl.create_default_context()
    #print(f"Connecting to {os.getenv('CIRCUITPY_WIFI_SSID')}")
    wifi.radio.connect(
        os.getenv("CIRCUITPY_WIFI_SSID"), os.getenv("CIRCUITPY_WIFI_PASSWORD"), timeout=5
    )
    print(f"Connected to {os.getenv('CIRCUITPY_WIFI_SSID')}")
    print(f"My IP address: {wifi.radio.ipv4_address}")
    time.sleep(1)
except Exception as e:
    print("Error connecting to wifi: ", e)
    time.sleep(15)
    supervisor.reload()
np[0] = Color.green
m(4)
aio_username = os.getenv('AIO_USERNAME')
aio_key = os.getenv('AIO_KEY')
tz = os.getenv('TIMEZONE')
lat = os.getenv('LATITUDE')
lon = os.getenv('LONGITUDE')
m(5)
requests = adafruit_requests.Session(pool, ssl.create_default_context())
m(6)
io = IO_HTTP(aio_username, aio_key, requests)
m(7)

# Set the RTC from Adafruit IO time server
print("Setting RTC from Adafruit IO...")
try:
    current_time = io.receive_time(timezone=tz)
    rtc.RTC().datetime = current_time
    print(f"RTC set to: {current_time}")
except Exception as e:
    print(f"Warning: Could not set RTC: {e}")
    print("Continuing with existing RTC time...")

before = FORECAST_DAYS_PAST
after = FORECAST_DAYS_FUTURE
periods = before + after
# Read past counter from NVM (persistent across reboots)
past = microcontroller.nvm[0] if microcontroller.nvm[0] < 255 else 0
print("Past counter from NVM: " + str(past))

# Cache feed objects outside the main loop to avoid repeated API calls
print("Caching feed objects...")
try:
    FEEDS = {
        'cur_totprec': io.get_feed('om-totprec'),
        'cur_rain': io.get_feed('om-rain'),
        'cur_show': io.get_feed('om-showers'),
        'cur_snow': io.get_feed('om-snow'),
        'cur_wind': io.get_feed('om-winds'),
        'cur_windspeed': io.get_feed('om-windspeed'),
        'cur_windgust': io.get_feed('om-windgust'),
        'cur_rh': io.get_feed('om-hum'),
        'cur_clouds': io.get_feed('om-clouds'),
        'cur_data': io.get_feed('om-current'),
        'cur_temp': io.get_feed('om-temp'),
        'cur_when': io.get_feed('om-when'),
        'cur_desc': io.get_feed('om-desc'),
        'cur_fl': io.get_feed('om-fl'),
        'cur_baro': io.get_feed('om-pres'),
        'baro_trend': io.get_feed('om-pres-trend'),
        'rt': io.get_feed("rt"),
        'past_data': io.get_feed('om-past'),
        'past_prec': io.get_feed('omp-totprec'),
        'past_rain': io.get_feed('omp-rain'),
        'past_show': io.get_feed('omp-showers'),
        'past_snow': io.get_feed('omp-snow'),
        'past_maxtemp': io.get_feed('omp-maxtemp'),
        'past_mintemp': io.get_feed('omp-mintemp'),
        'past_daylight': io.get_feed('omp-daylighthours'),
        'past_sunlight': io.get_feed('omp-sunshinehrs'),
    }
    print("Feed cache complete!")
except Exception as e:
    print(f"Error caching feeds: {e}")
    time.sleep(15)
    supervisor.reload()

# Cache past-day feeds to avoid per-loop lookups
PAST_FEEDS = {}
for i in range(FORECAST_DAYS_PAST):
    p_name = f"ompast.{FORECAST_DAYS_PAST - i - 1}"
    try:
        PAST_FEEDS[p_name] = io.get_feed(p_name)
    except Exception as e:
        print(f"Warning: could not cache feed {p_name}: {e}")
        PAST_FEEDS[p_name] = None

while True:
    # Print current time from RTC at start of loop
    current_rtc_time = time.localtime()
    print("\n" + "="*60)
    # Format time components with zero padding
    year = str(current_rtc_time.tm_year)
    mon = str(current_rtc_time.tm_mon) if current_rtc_time.tm_mon >= 10 else "0" + str(current_rtc_time.tm_mon)
    day = str(current_rtc_time.tm_mday) if current_rtc_time.tm_mday >= 10 else "0" + str(current_rtc_time.tm_mday)
    hour = str(current_rtc_time.tm_hour) if current_rtc_time.tm_hour >= 10 else "0" + str(current_rtc_time.tm_hour)
    minute = str(current_rtc_time.tm_min) if current_rtc_time.tm_min >= 10 else "0" + str(current_rtc_time.tm_min)
    sec = str(current_rtc_time.tm_sec) if current_rtc_time.tm_sec >= 10 else "0" + str(current_rtc_time.tm_sec)
    time_str = year + "-" + mon + "-" + day + " " + hour + ":" + minute + ":" + sec
    print("Loop started at: " + time_str)
    print("="*60 + "\n")
    
    np.brightness = LED_BRIGHTNESS
    np[0] = Color.amber
    m(8)
    jwu_base = "http://api.open-meteo.com/v1/forecast?"
    jwu_loc = f"latitude={lat}&longitude={lon}&timezone={tz}"
    jwu_units = "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch"
    jwu_front = jwu_base + jwu_loc + jwu_units
    jwu_current = "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,rain,showers,snowfall,weather_code,cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
    jwu_days = f"&past_days={before}&forecast_days={after}"
    jwu_daily = "&daily=weather_code,temperature_2m_max,temperature_2m_min,apparent_temperature_max,apparent_temperature_min,sunrise,sunset,daylight_duration,sunshine_duration,precipitation_sum,rain_sum,showers_sum,snowfall_sum,precipitation_hours,precipitation_probability_max,wind_gusts_10m_max,wind_direction_10m_dominant"
    jwu = jwu_front + jwu_daily + jwu_days + jwu_current
    # Smaller past-only query to save RAM during historical fetch
    jwu_past = jwu_front + jwu_daily + f"&past_days={before}&forecast_days=0"
    print(jwu)
    #time.sleep(3)
    #print("proceed")
    m(9)
    # Fetch weather data with retry logic
    no_error = 0
    response = None
    
    while no_error == 0:
        try:
            response = requests.get(jwu)
            no_error = 1
            print("Weather data fetched successfully")
        except OSError as e:
            print(f"Error fetching - try again: {e}")
            time.sleep(API_RETRY_DELAY)
            continue
    
    # Parse JSON once and cache the result
    m(10)
    
    try:
        data = response.json()
        current_units = data['current_units']
        current = data['current']
        
        # Cache units
        cu_time = current_units['time']
        cu_temp = current_units['temperature_2m']
        cu_rh = current_units['relative_humidity_2m']
        cu_feelsLike = current_units['apparent_temperature']
        cu_precip = current_units['precipitation']
        cu_rain = current_units['rain']
        cu_showers = current_units['showers']
        cu_snow = current_units['snowfall']
        cu_clouds = current_units['cloud_cover']
        cu_baro = current_units['pressure_msl']
        cu_windspeed = current_units['wind_speed_10m']
        cu_winddir = current_units['wind_direction_10m']
        cu_windgust = current_units['wind_gusts_10m']
        
        m(11)
        # Parse current values
        c_time = current['time']
        print(c_time)
        c_mtime = convert_to_struct_time(c_time)
        c_atime = f"{c_mtime.tm_year}-{c_mtime.tm_mon}-{c_mtime.tm_mday}T{c_mtime.tm_hour}:{c_mtime.tm_min}"
        c_friendly_time = pretty_date_time(c_mtime)
        print(c_atime, c_mtime, "")
        
        c_temp = current['temperature_2m']
        c_rh = current['relative_humidity_2m']
        c_feelsLike = current['apparent_temperature']
        c_precip = current['precipitation']
        c_day = current['is_day']
        isday = "Daytime" if c_day == 1 else "Nighttime"
        c_rain = current['rain']
        c_showers = current['showers']
        c_snow = current['snowfall']
        c_wc = current['weather_code']
        c_wcvalue = wc[c_wc]
        c_clouds = current['cloud_cover']
        c_baro = current['pressure_msl']
        c_windspeed = current['wind_speed_10m']
        c_winddir = current['wind_direction_10m']
        c_winddirord = windir(c_winddir)
        c_windgust = current['wind_gusts_10m']
    except Exception as e:
        print(f"Error parsing JSON: {e}")
        if response:
            response.close()
        time.sleep(15)
        supervisor.reload()
    m(12)
    # Build output strings using modern formatting
    if c_precip < .01:
        precip_text = "No precipitation"
    else:
        precip_text = f"Total Precip {c_precip}{cu_precip}s Rain: {c_rain}{cu_rain}s Showers: {c_showers}{cu_showers}s Snow: {c_snow}{cu_snow}s."
    
    c_outfull = [
        f"On {c_friendly_time}", isday, f" it is {c_wcvalue}", 
        f" Temp {c_temp}{cu_temp}", f"Humidity {c_rh}{cu_rh}", 
        f" Feels Like {c_feelsLike}{cu_feelsLike}", 
        precip_text, f" Clouds {c_clouds}{cu_clouds}", 
        f"Barometer {c_baro} {cu_baro}", 
        f"Wind Speed {c_windspeed}{cu_windspeed}", f"from the {c_winddirord}", 
        f"gusting {c_windgust}{cu_windgust}"
    ]
    print(c_outfull)  
    xf = ", ".join(c_outfull)
    print(f"## {len(c_outfull)}")
    print(xf)
    print("sending current")
    m(30)
    try:
        # Get barometer history for trend calculation
        BaroHist = io.receive_n_data(FEEDS['cur_baro']['key'], 5)

        # Calculate barometer trend with safety check
        if BaroHist and len(BaroHist) > 0:
            BaroHistVals = [float(data["value"]) for data in BaroHist]
            BaroHistAvg = sum(BaroHistVals) / len(BaroHistVals)
            if BaroHistAvg < c_baro:
                ptrend = "Rising"
            elif BaroHistAvg > c_baro:
                ptrend = "Falling"
            else:
                ptrend = "Steady"
        else:
            BaroHistVals = []
            BaroHistAvg = c_baro
            ptrend = "Unknown"
            
    except Exception as e:
        print(f"Error getting barometer history: {e}")
        BaroHistVals = []
        BaroHistAvg = c_baro
        ptrend = "Unknown"
        
    print("="*60)
    print(BaroHistVals, "", BaroHistAvg, ptrend)
    print("="*60)
    
    # Clean up current weather response now that we've extracted all data
    if 'response' in locals() and response:
        response.close()
        del response
    if 'data' in locals():
        del data
    if 'current' in locals():
        del current
    if 'current_units' in locals():
        del current_units
    gc.collect()
    
    winds = f"Winds: {c_windspeed}{cu_windspeed} from the {c_winddirord} gusting {c_windgust}"

    metadata = {"lat": os.getenv('LATITUDE'), "lon": os.getenv('LONGITUDE'), "created_at": c_atime}
    try:
        m(100)
        # Send current weather data using cached feeds (helper handles pacing/retry)
        send_with_delay(FEEDS['cur_when'], c_time, metadata)
        m(101)
        send_with_delay(FEEDS['cur_wind'], winds)
        m(102)
        send_with_delay(FEEDS['cur_windspeed'], c_windspeed, metadata)
        send_with_delay(FEEDS['cur_windgust'], c_windgust, metadata)
        send_with_delay(FEEDS['cur_totprec'], c_precip, metadata)
        send_with_delay(FEEDS['cur_rain'], c_rain, metadata)
        send_with_delay(FEEDS['cur_show'], c_showers, metadata)
        send_with_delay(FEEDS['cur_snow'], c_snow, metadata)
        send_with_delay(FEEDS['cur_rh'], c_rh, metadata)
        send_with_delay(FEEDS['cur_clouds'], c_clouds, metadata)
        send_with_delay(FEEDS['cur_data'], xf, metadata)
        send_with_delay(FEEDS['cur_temp'], c_temp, metadata)
        dgts.print(c_temp)
        send_with_delay(FEEDS['cur_fl'], c_feelsLike, metadata)
        send_with_delay(FEEDS['cur_baro'], c_baro, metadata)
        send_with_delay(FEEDS['baro_trend'], ptrend, metadata)
        send_with_delay(FEEDS['cur_desc'], c_wcvalue, metadata)
    except Exception as e:
        print(f"Failed to send data: {e}")
        time.sleep(5)
    
    time.sleep(AIO_SEND_DELAY)  # Extra delay before RT update
    try:
        io.send_data(FEEDS['rt']['key'], "OM Current Updated")
        print("Current Sent")
    except Exception as e:
        print(f"Failed to send RT: {e}")

    # Free memory before past-data work
    gc.collect()

    #get past days - only run every PAST_DATA_INTERVAL cycles
    print(f"\n\nPast: {past}\n")
    if past == PAST_DATA_INTERVAL:
        past = 0
        microcontroller.nvm[0] = 0
    
    if past == 0:
        np[0] = Color.magenta
        
        # Fetch fresh data for past analysis
        gc.collect()
        response = requests.get(jwu_past)
        past_data = response.json()
        daily = past_data['daily']
        
        for i in range(periods-after):
            m(60+1)
            p_name = f'ompast.{before - i - 1}'
            print(p_name)
            np[0] = Color.white
            time.sleep(1)
            
            # Initialize p_time early so it's available for metadata
            p_time = None
            
            try:
                # Cache daily data access
                p_time = daily['time'][i]
                p_wc = daily['weather_code'][i]
                p_wctxt = wc[p_wc]
                p_tempmax = daily['temperature_2m_max'][i]
                p_tempmin = daily['temperature_2m_min'][i]
                p_flmax = daily['apparent_temperature_max'][i]
                p_flmin = daily['apparent_temperature_min'][i]
                p_daylightsec = daily['daylight_duration'][i]
                p_daylighthrs = int(p_daylightsec / 3600 * 10) / 10
                p_sunshinesec = daily['sunshine_duration'][i]
                p_sunshinehrs = int(p_sunshinesec / 3600 * 10) / 10
                p_pct_ss = int((p_sunshinesec / p_daylightsec) * 100) if p_daylightsec > 0 else 0
                p_precip = daily['precipitation_sum'][i]
                p_rain = daily['rain_sum'][i]
                p_showers = daily['showers_sum'][i]
                p_snow = daily['snowfall_sum'][i]
                p_preciphrs = daily['precipitation_hours'][i]
                p_windgust = daily['wind_gusts_10m_max'][i]
                p_winddird = daily['wind_direction_10m_dominant'][i]
                p_winddir = windir(p_winddird)

                # Build summary string
                if p_precip == 0:
                    precip_str = "There was no precipitation recorded. "
                else:
                    precip_str = "Precipitation totalled " + str(p_precip) + " " + cu_rain + "s of which was " + str(p_rain) + " " + cu_rain + "s rain, " + str(p_showers) + " " + cu_rain + "s showers and " + str(p_snow) + " " + cu_rain + "s snow. "
                
                xf = "For " + p_time + " - " + p_wctxt + " with high of " + str(p_tempmax) + cu_temp + " and low of " + str(p_tempmin) + cu_temp + " "
                xf = xf + "feeling like a high of " + str(p_flmax) + cu_temp + " and a low of " + str(p_flmin) + cu_temp + ". "
                xf = xf + "Total daylight " + str(p_daylighthrs) + " hours with sunshine for " + str(p_sunshinehrs) + " hours (" + str(p_pct_ss) + "%). "
                xf = xf + precip_str
                xf = xf + "Winds were primarily from the " + p_winddir + " gusting to " + str(p_windgust) + " " + cu_windspeed + "."
                print(xf)
                
                # Send data to Adafruit IO - still within the try block
                np[0] = Color.black
                metadata = {"lat": os.getenv('LATITUDE'), "lon": os.getenv('LONGITUDE'), "created_at": p_time}
                
                # Get individual past feed (these are dynamic per day)
                print(f"Getting feed: {p_name}")
                PastDatai = PAST_FEEDS.get(p_name)
                if PastDatai is None:
                    PastDatai = io.get_feed(p_name)
                    PAST_FEEDS[p_name] = PastDatai
                time.sleep(AIO_SEND_DELAY)  # Delay after getting feed
                print(f"Sending data to {p_name}")
                
                # Send data using cached and individual feeds (helper paces/retries)
                send_with_delay(PastDatai, xf)
                send_with_delay(FEEDS['past_prec'], p_precip, metadata)
                send_with_delay(FEEDS['past_rain'], p_rain, metadata)
                send_with_delay(FEEDS['past_show'], p_showers, metadata)
                send_with_delay(FEEDS['past_snow'], p_snow, metadata)
                send_with_delay(FEEDS['past_maxtemp'], p_tempmax, metadata)
                send_with_delay(FEEDS['past_mintemp'], p_tempmin, metadata)
                send_with_delay(FEEDS['past_daylight'], p_daylighthrs, metadata)
                send_with_delay(FEEDS['past_sunlight'], p_sunshinehrs, metadata)
                print(f"Successfully sent data for {p_name}")
            except Exception as e:
                print(f"Error processing past data for {p_name if 'p_name' in locals() else 'unknown'}: {e}")
                import sys
                sys.print_exception(e)
                time.sleep(5)
                # Continue to next day even if this one fails
            finally:
                gc.collect()
        
        # Clean up response object after all past data is processed
        try:
            response.close()
            del response
            del past_data
            del daily
            gc.collect()
        except Exception as e:
            print(f"Error cleaning up past data: {e}")
        
        np[0] = Color.green
        
        # Send summary using current weather data
        try:
            xf = ", ".join(c_outfull)
            io.send_data(FEEDS['past_data']['key'], xf)
            print(xf)
        except Exception as e:
            print(f"Failed to send past data summary: {e}")
            time.sleep(5)
        
        try:
            io.send_data(FEEDS['rt']['key'], "117-OM Past Updated")
        except Exception as e:
            print(f"Failed to send RT update: {e}")
        past += 1
        microcontroller.nvm[0] = past
    else:
        past += 1
        microcontroller.nvm[0] = past
    
    np[0] = 0x0000FF
    # Set LED colors based on past counter
    c0 = 0xFF0000 if past < 14 else 0xFFFFFF
    c1 = 0x0000FF if past < 18 else 0x000000
    c2 = 0x888855 if past < 22 else 0x00FF00
    display_sequence = [
        (c_temp, c0),
        (c_feelsLike, c1),
        (c_windspeed, c2),
    ]
    print(f"Past counter: {past}, LED colors set to c0: {hex(c0)}, c1: {hex(c1)}, c2: {hex(c2)}")
    # Display loop - runs for UPDATE_INTERVAL seconds total
    for life in range(UPDATE_INTERVAL):
        b = min((life / UPDATE_INTERVAL) + 0.01, 1.0)
        np.brightness = b
        value, color = display_sequence[life % len(display_sequence)]
        np[0] = color
        dgts.print(value)
        time.sleep(1)

    print("Reloading for next update cycle...")
    print("==="*20)
    time.sleep(1)
    supervisor.reload()

