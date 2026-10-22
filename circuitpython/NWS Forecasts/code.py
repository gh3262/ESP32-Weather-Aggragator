import board
import time
import rtc
from adafruit_datetime import datetime, date, timezone
import terminalio
import displayio
from adafruit_display_text import label
from adafruit_display_text import bitmap_label
from adafruit_bitmap_font import bitmap_font
import microcontroller
import os
import ipaddress
import ssl
import wifi
import socketpool
import adafruit_requests
from fourwire import FourWire
from adafruit_io.adafruit_io import IO_HTTP

def set_clock():
    print("Setting Clock")
    now = time.struct_time(io.receive_time(os.getenv("TIMEZONE")))
    #print(now)
    r = rtc.RTC()
    r.datetime = time.struct_time((now.tm_year, now.tm_mon, now.tm_mday,
                                    now.tm_hour, now.tm_min, now.tm_sec, 0, -1, -1))
    print(r.datetime)
    
def get_time():
    r = rtc.RTC()
    now = r.datetime
    year = now.tm_year
    month = now.tm_mon
    day = now.tm_mday
    hour = now.tm_hour
    minute = now.tm_min
    period = "am" if hour < 12 else "pm"
    hour = hour % 12
    if hour == 0:
        hour = 12
    formatted_time = f"{month}/{day}/{year} {hour}:{minute:02d} {period}"
    return formatted_time

def just_date():
    r = rtc.RTC()
    now = r.datetime
    year = now.tm_year
    month = now.tm_mon
    day = now.tm_mday
    hour = now.tm_hour
    minute = now.tm_min
    period = "am" if hour < 12 else "pm"
    hour = hour % 12
    if hour == 0:
        hour = 12
    formatted_time = f"{month}/{day}/{year}"
    return formatted_time

def just_time():
    r = rtc.RTC()
    now = r.datetime
    year = now.tm_year
    month = now.tm_mon
    day = now.tm_mday
    hour = now.tm_hour
    minute = now.tm_min
    period = "am" if hour < 12 else "pm"
    hour = hour % 12
    if hour == 0:
        hour = 12
    formatted_time = f"{hour}:{minute:02d} {period}"
    return formatted_time
    

def get_forecast(gpd):
    response = requests.get(JSON_WEATHER_URL)
    pd_zero = response.json()["properties"]["periods"][gpd]["number"]
    # print(pd_zero)
    dfc = response.json()["properties"]["periods"][gpd]["detailedForecast"]
    fc = response.json()["properties"]["periods"][gpd]["shortForecast"]
    st = response.json()["properties"]["periods"][gpd]["startTime"]
    et = response.json()["properties"]["periods"][gpd]["endTime"]
    sst = datetime.fromisoformat(st)
    sett = datetime.fromisoformat(et)
    if sst.hour > 12:
        ampm = "pm"
        ssth = sst.hour - 12
    else:
        ampm = "am"
        ssth = sst.hour
    # vt = f"{ssth:01}:{sst.minute:02}" + " " + ampm
    vt = f"{ssth:01}" + ampm

    if sett.hour > 12:
        ampme = "pm"
        setth = sett.hour - 12
    else:
        ampme = "am"
        setth = sett.hour
    vte = f"{setth:01}" + ampme

    ptime = vt + " to " + vte

    hr = sst.hour
    mint = sst.minute
    mn = sst.month
    monthname = mnames[mn]
    fdate = monthname + str(sst.day)
    # print("Date: ", fdate)
    dte = sst.day
    yr = sst.year

    # print(sst)
    pname = response.json()["properties"]["periods"][gpd]["name"]
    temp = response.json()["properties"]["periods"][gpd]["temperature"]
    dl = response.json()["properties"]["periods"][gpd]["isDaytime"]
    if dl == True:
        ms = "high near "
    else:
        ms = "low near "
    tempText = ms + str(temp) + "F"
    ws = response.json()["properties"]["periods"][gpd]["windSpeed"]
    wd = response.json()["properties"]["periods"][gpd]["windDirection"]
    wind = " winds: " + wd + " at " + ws + "."
    # print(pd_zero)
    # print(pname, fdate, ptime,"   (" + str(pd_zero) + ")")
    # print("Temp: ", tempText, wind)
    # print(fc, ":", dfc)

    textout = (
        pname
        + " "
        + fdate
        + ", "
        + ptime
        + " - "
        + tempText
        + " with "
        + wind
        + " \n"
        + dfc
    )
    return nwsf[gpd], textout
    # print(feed_pd)

# Function to parse the ISO 8601 date-time string
def parse_iso_datetime(iso_str):
    # Remove the 'Z' if present
    if iso_str.endswith("Z"):
        iso_str = iso_str[:-1]
    datetime_str = str(datetime.fromisoformat(iso_str))
    year = int(datetime_str[0:4])
    month = int(datetime_str[5:7])
    day = int(datetime_str[8:10])
    hour = int(datetime_str[11:13])
    minute = int(datetime_str[14:16])
    second = int(datetime_str[17:19])

    datetime_struct = time.struct_time((year, month, day, hour, minute, second, -1, -1, -1))

    return datetime_struct

def adjust_to_central_time(datetime_struct):
    # Convert struct_time to seconds since epoch (UTC)
    utc_timestamp = time.mktime(datetime_struct)
    # Adjust by -6 hours (6 * 60 * 60 seconds)
    central_timestamp = utc_timestamp - (6 * 60 * 60)
    # Convert back to struct_time
    central_time = time.localtime(central_timestamp)
    formatted_datetime_central = "{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}".format(
    central_time.tm_year,
    central_time.tm_mon,
    central_time.tm_mday,
    central_time.tm_hour,
    central_time.tm_min,
    central_time.tm_sec,
)
    return formatted_datetime_central

nwsf = [
    "nws.00",
    "nws.01",
    "nws.02",
    "nws.03",
    "nws.04",
    "nws.05",
    "nws.06",
    "nws.07",
    "nws.08",
    "nws.09",
    "nws.10",
    "nws.11",
    "nws.12",
    "nws.13",
]

mnames = (
    None,
    "Jan ",
    "Feb ",
    "Mar ",
    "Apr ",
    "May ",
    "Jun ",
    "Jul ",
    "Aug ",
    "Sep ",
    "Oct ",
    "Nov ",
    "Dec ",
)

print("sleeping")
time.sleep(15)
print("starting")
aio_username = os.getenv("aio_username")
aio_key = os.getenv("aio_key")
timezone = os.getenv("TIMEZONE")
TIME_URL = f"https://io.adafruit.com/api/v2/{aio_username}/integrations/time/strftime?x-aio-key={aio_key}&tz={timezone}"
TIME_URL += "&fmt=%25Y, %25m, %25d, %25H, %25M, %25S, %25w, -1, -1"
JSON_WEATHER_URL = "https://api.weather.gov/gridpoints/MEG/54,68/forecast"

BACKGROUND_COLOR = 0xFFFF99
TEXT_COLOR = 0x0000FF
FONTSCALE = 1
font10 = bitmap_font.load_font("fonts/ComicSansMS-10.bdf")
font11 = bitmap_font.load_font("fonts/ComicSansMS-11.bdf")
font12 = bitmap_font.load_font("fonts/ComicSansMS-12.bdf")
font14 = bitmap_font.load_font("fonts/ComicSansMS-14.bdf")
font16 = bitmap_font.load_font("fonts/ComicSansMS-16.bdf")
font17 = bitmap_font.load_font("fonts/ComicSansMS-17.bdf")
font19 = bitmap_font.load_font("fonts/ComicSansMS-19.bdf")

display = board.DISPLAY
display.rotation = 270
display.brightness = .4
'''
default_bitmap = displayio.OnDiskBitmap("sample2.bmp")
default_tile_grid = displayio.TileGrid(default_bitmap, pixel_shader=default_bitmap.pixel_shader)
'''
group = displayio.Group()
display.root_group = group
#display.rotation = 180

color_bitmap = displayio.Bitmap(display.width, display.height, 1)
color_palette = displayio.Palette(1)
color_palette[0] = BACKGROUND_COLOR

bg_sprite = displayio.TileGrid(color_bitmap, pixel_shader=color_palette, x=0, y=0)
group.append(bg_sprite)

text = "Start"
#text_area = label.Label(FONT.terminalio, text=text, color=TEXT_COLOR)
text_area = label.Label(font17, text=text, color=TEXT_COLOR)
text_width = 20 
text_group = displayio.Group(
    #scale=FONTSCALE,
    x=25,#display.width //2 - text_width // 2,
    y=25,#display.height //2,
)
text_group.append(text_area)
group.append(text_group)

pool = socketpool.SocketPool(wifi.radio)
print(f"Connecting to {os.getenv('CIRCUITPY_WIFI_SSID')}")
wifi.radio.connect(
    os.getenv("CIRCUITPY_WIFI_SSID"), os.getenv("CIRCUITPY_WIFI_PASSWORD")
)
print(f"Connected to {os.getenv('CIRCUITPY_WIFI_SSID')}")

requests = adafruit_requests.Session(pool, ssl.create_default_context())
io = IO_HTTP(aio_username, aio_key, requests)
print("setting clock")
set_clock()
print(get_time())
while True:
    pd = 0
    while pd < 14:
        sendval = get_forecast(pd)
        #text_group.scale=3
        text_group.x = 60
        text_group.y = 40
        text_area.text = str(pd)
        print(sendval[0])
        print(sendval[1])
        print("-" * 20)
        io = IO_HTTP(aio_username, aio_key, requests)
        fval = io.get_feed(sendval[0])
        io.send_data(fval["key"], sendval[1])

        time.sleep(1)
        pd = pd + 1
    feed_url = f"https://io.adafruit.com/api/v2/{aio_username}/feeds/nws.13/data"

    # Fetch feed data
    response = requests.get(feed_url, headers={"X-AIO-Key": aio_key})
    feed_data = response.json()

    # Extract the date and time when the feed was last updated
    if feed_data:
        last_updated = feed_data[0]["created_at"]
        lus = parse_iso_datetime(last_updated)
        #lus = get_time()
        lus_cst = adjust_to_central_time(lus)
        #print(lus)
        lus_ct = adjust_to_central_time(lus)
        #lus_ct = lus
        #print(lus_ct)

        fval = io.get_feed("nws.when")
        io.send_data(fval["key"], lus_ct)
        lfr = io.get_feed("rt")
        io.send_data(lfr["key"], "Forecasts Updated")
    else:
        pass
    print()
    print()
    #text_group.font = font12
    text_group.x = 15
    text_group.y = 10
    text_area.text = "   " + just_time() + "\n" + just_date() + "\n" + "> Sleeping <"
    print("-"*20,"\n",get_time(), "\n > Sleeping <", "\n", "="*20)
    time.sleep(3600)
    print("\n \n \n \n \n Starting")
