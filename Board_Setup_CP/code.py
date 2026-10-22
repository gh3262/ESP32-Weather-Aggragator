import gc
import math
from random import randint
import time
import rtc
import displayio
import fourwire
import board
import vectorio
import terminalio
import simpleio
from os import getenv
import ipaddress
import ssl
import wifi
import socketpool
from adafruit_st7789 import ST7789
from adafruit_bitmap_font import bitmap_font
from adafruit_display_text import label, wrap_text_to_lines, wrap_text_to_pixels
from adafruit_display_shapes.rect import Rect
from adafruit_display_shapes.circle import Circle
from adafruit_display_shapes.roundrect import RoundRect
from adafruit_display_shapes.triangle import Triangle
from adafruit_display_shapes.line import Line
import adafruit_requests
from adafruit_io.adafruit_io import IO_HTTP

displayio.release_displays()

spi = board.SPI()
tft_cs = board.TX
tft_dc = board.RX

display_bus = fourwire.FourWire(
    spi, command=tft_dc, chip_select=tft_cs, reset=None
)

display = ST7789(display_bus, width=320, height=172, colstart=34, rotation=270)

# Get WiFi details, ensure these are setup in settings.toml
ssid = getenv("CIRCUITPY_WIFI_SSID")
password = getenv("CIRCUITPY_WIFI_PASSWORD")

if None in [ssid, password]:
    raise RuntimeError(
        "WiFi settings are kept in settings.toml, "
        "please add them there. The settings file must contain "
        "'CIRCUITPY_WIFI_SSID', 'CIRCUITPY_WIFI_PASSWORD', "
        "at a minimum."
    )

print()
print("Connecting to WiFi")

#  connect to your SSID
try:
    wifi.radio.connect(ssid, password)
except TypeError:
    print("Could not find WiFi info. Check your settings.toml file!")
    raise

print("Connected to WiFi")

pool = socketpool.SocketPool(wifi.radio)

#  prints MAC address to REPL
print("My MAC addr:", [hex(i) for i in wifi.radio.mac_address])

#  prints IP address to REPL
print(f"My IP address is {wifi.radio.ipv4_address}")

#  pings Google
ipv4 = ipaddress.ip_address("8.8.4.4")
print("Ping google.com: %f ms" % (wifi.radio.ping(ipv4)*1000))

# Initialize RTC
r = rtc.RTC()

# Initialize temperature storage
current_temperature = None
last_temp_update = 0
last_hist_update = 0
temp_hist = []

# Initialize om_current text storage
om_current_text = None
last_om_update = 0

# Initialize NWS forecast text storage
nws_00_text = None
nws_01_text = None
nws_02_text = None
last_nws_update = 0

# Get AdafruitIO credentials from settings.toml
aio_username = getenv("AIO_USERNAME")
aio_key = getenv("AIO_KEY")
timezone = getenv("TIMEZONE", "UTC")

if None in [aio_username, aio_key]:
    print("WARNING: AdafruitIO credentials not found in settings.toml")
else:
    # Initialize AdafruitIO with SSL context
    ssl_context = ssl.create_default_context()
    requests = adafruit_requests.Session(pool, ssl_context)
    io = IO_HTTP(aio_username, aio_key, requests)
    
    try:
        print("Getting time from AdafruitIO...")
        # Get current time from AdafruitIO
        io_time = io.receive_time(timezone)
        print(f"AdafruitIO time: {io_time}")
        
        # Set the RTC with the time from AdafruitIO
        r.datetime = io_time
        print(f"RTC initialized with time: {r.datetime}")
        
        # Get initial temperature and fill history
        print("Getting initial temperature from AdafruitIO...")
        temp_value = io.receive_data("feed-avg")
        current_temperature = float(temp_value['value'])
        last_temp_update = time.monotonic()
        last_hist_update = time.monotonic()
        # Fill temp_hist with 48 copies of the initial temperature (24 hours at 30-min intervals)
        temp_hist = [current_temperature] * 48
        print(f"Temperature history initialized with {current_temperature}°F (48 values)")
    except Exception as e:
        print(f"Error during AdafruitIO initialization: {e}")

def get_formatted_time():
    """Returns the time from RTC in 12-hour format as 'hh:mm am/pm'"""
    current = r.datetime
    hour = current.tm_hour
    minute = current.tm_min
    
    # Convert to 12-hour format
    if hour == 0:
        hour_12 = 12
        period = "am"
    elif hour < 12:
        hour_12 = hour
        period = "am"
    elif hour == 12:
        hour_12 = 12
        period = "pm"
    else:
        hour_12 = hour - 12
        period = "pm"
    
    return f"{hour_12}:{minute:02d} {period}"

def get_formatted_full_date():
    """Returns the current date as 'Monday, December 12, 2026'"""
    current = r.datetime
    
    # Day names
    days = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
    # Month names
    months = ["January", "February", "March", "April", "May", "June",
              "July", "August", "September", "October", "November", "December"]
    
    day_name = days[current.tm_wday]
    month_name = months[current.tm_mon - 1]
    day = current.tm_mday
    year = current.tm_year
    
    return f"{day_name}, {month_name} {day}, {year}"


def get_formatted_date():
    """Returns the current date as 'mm/dd/yyyy'"""
    current = r.datetime
    
    month = current.tm_mon
    day = current.tm_mday
    year = current.tm_year
    
    return f"{month:02d}/{day:02d}/{year}"


def update_temperature():
    """Fetch the latest temperature from AdafruitIO feed 'feed-avg'"""
    global current_temperature, last_temp_update, last_hist_update, temp_hist
    try:
        temp_value = io.receive_data("feed-avg")
        current_temperature = float(temp_value['value'])
        last_temp_update = time.monotonic()
        
        # Add to history every 30 minutes (1800 seconds)
        if (last_temp_update - last_hist_update) >= 1800:
            # Maintain rolling window of 48 temperatures (24 hours at 30-min intervals)
            if len(temp_hist) >= 48:
                temp_hist.pop(0)  # Remove oldest temperature
            temp_hist.append(current_temperature)
            last_hist_update = last_temp_update
            print(f">Temperature updated: {current_temperature}°F (History: {len(temp_hist)} values)")
        else:
            print(f">Temperature updated: {current_temperature}°F (no history update yet)")
    except Exception as e:
        print(f"Error fetching temperature: {e}")

def get_temperature():
    """Returns the current temperature, fetching from AdafruitIO if needed"""
    global current_temperature, last_temp_update
    
    # Check if we need to update (every 15 minutes = 900 seconds)
    current_time = time.monotonic()
    if current_temperature is None or (current_time - last_temp_update) >= 900:
        update_temperature()
    
    return current_temperature

def get_temperature_stats():
    """Returns a tuple of (min_temp, max_temp, avg_temp) over the last 24 hours,
    ignoring initial padding values that are identical.
    Returns (None, None, None) if no real temperature data exists."""
    global temp_hist
    
    if not temp_hist:
        return None, None, None
    
    # Skip all values that are identical to the first value (initial padding)
    initial_value = temp_hist[0]
    real_data = [temp for temp in temp_hist if temp != initial_value]
    
    # If all data is initial padding, return None
    if not real_data:
        return None, None, None
    
    min_temp = min(real_data)
    max_temp = max(real_data)
    avg_temp = sum(real_data) / len(real_data)
    
    return min_temp, max_temp, avg_temp

def update_om_current():
    """Fetch the latest text from AdafruitIO feed 'om-current'"""
    global om_current_text, last_om_update
    try:
        om_value = io.receive_data("om-current")
        om_current_text = str(om_value['value'])
        last_om_update = time.monotonic()
        print(f">OM current text updated: {len(om_current_text)} characters")
    except Exception as e:
        print(f"Error fetching om-current: {e}")

def get_om_current():
    """Returns the current om_current text, fetching from AdafruitIO if needed"""
    global om_current_text, last_om_update
    
    # Check if we need to update (every 15 minutes = 900 seconds)
    current_time = time.monotonic()
    if om_current_text is None or (current_time - last_om_update) >= 900:
        update_om_current()
    
    return om_current_text if om_current_text is not None else "The quick brown fox jumps over the lazy dog."

def update_nws_forecasts():
    """Fetch the latest forecast texts from AdafruitIO feeds 'nws.00', 'nws.01', 'nws.02'"""
    global nws_00_text, nws_01_text, nws_02_text, last_nws_update
    try:
        nws_00_value = io.receive_data("nws.00")
        nws_00_text = str(nws_00_value['value'])
        
        nws_01_value = io.receive_data("nws.01")
        nws_01_text = str(nws_01_value['value'])
        
        nws_02_value = io.receive_data("nws.02")
        nws_02_text = str(nws_02_value['value'])
        
        last_nws_update = time.monotonic()
        print(f">NWS forecasts updated")
    except Exception as e:
        print(f"Error fetching NWS forecasts: {e}")

def get_nws_00():
    """Returns the nws.00 forecast, fetching from AdafruitIO if needed"""
    global nws_00_text, last_nws_update
    
    # Check if we need to update (every 15 minutes = 900 seconds)
    current_time = time.monotonic()
    if nws_00_text is None or (current_time - last_nws_update) >= 900:
        update_nws_forecasts()
    
    return nws_00_text if nws_00_text is not None else "Forecast unavailable."

def get_nws_01():
    """Returns the nws.01 forecast, fetching from AdafruitIO if needed"""
    global nws_01_text, last_nws_update
    
    # Check if we need to update (every 15 minutes = 900 seconds)
    current_time = time.monotonic()
    if nws_01_text is None or (current_time - last_nws_update) >= 900:
        update_nws_forecasts()
    
    return nws_01_text if nws_01_text is not None else "Forecast unavailable."

def get_nws_02():
    """Returns the nws.02 forecast, fetching from AdafruitIO if needed"""
    global nws_02_text, last_nws_update
    
    # Check if we need to update (every 15 minutes = 900 seconds)
    current_time = time.monotonic()
    if nws_02_text is None or (current_time - last_nws_update) >= 900:
        update_nws_forecasts()
    
    return nws_02_text if nws_02_text is not None else "Forecast unavailable."

def add_uptime_display(group, color=None):
    """Add uptime display label to the group at the bottom center of the screen"""
    if color is None:
        color = sky_blue
    
    # Convert monotonic time to days:hours:minutes:seconds format
    total_seconds = int(time.monotonic())
    days = total_seconds // 86400
    hours = (total_seconds % 86400) // 3600
    minutes = (total_seconds % 3600) // 60
    seconds = total_seconds % 60
    uptime_text = f"{days}:{hours:02d}:{minutes:02d}:{seconds:02d}"
    
    text_area_tm = label.Label(font12, text=uptime_text, color=color)
    text_area_tm.anchor_point = (0.5, 1)
    text_area_tm.anchored_position = (display.width // 2, display.height)
    group.append(text_area_tm)

font32 = bitmap_font.load_font("/fonts/ArialRoundedMTBold-32.bdf")
font24 = bitmap_font.load_font("/fonts/ArialRoundedMTBold-24.bdf")
font48 = bitmap_font.load_font("/fonts/ArialRoundedMTBold-48.bdf")
font16 = bitmap_font.load_font("/fonts/ComicSansMS-16.bdf")
font26 = bitmap_font.load_font("/fonts/ComicSansMS-Bold-26.bdf")
font10 = bitmap_font.load_font("/fonts/ComicSansMS-10.bdf")
font21 = bitmap_font.load_font("/fonts/ComicSansMS-21.bdf")
font12 = bitmap_font.load_font("/fonts/ComicSansMS-12.bdf")
font17 = bitmap_font.load_font("/fonts/ArialNarrow-17.bdf")
font19 = bitmap_font.load_font("/fonts/ArialNarrow-19.bdf")

bitmap = displayio.Bitmap(display.width, display.height, 3)

blush = 0xffaaaa
red = 0xff0000
yellow = 0xffff00
orange = 0xff5500
sky_blue = 0xccccff
blue = 0x0000ff
pink = 0xff00ff
purple = 0x5500ff
white = 0xffffff
green =  0x00ff00
aqua = 0x125690
cyan = 0x00ffff

palette = displayio.Palette(4)
palette[0] = 0x000000 # black
palette[1] = white
palette[2] = yellow
palette[3] = blush

palette.make_transparent(0)

tile_grid = displayio.TileGrid(bitmap, pixel_shader=palette)

group = displayio.Group()

def clean_up(group_name):
    for _ in range(len(group_name)):
        group_name.pop()
    gc.collect()

def show_date_time(text_color=white):
    print(f"show_date_time START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    current_time = get_formatted_time()
    current_date = get_formatted_date()
    text_area_time = label.Label(font48, text=current_time, color=text_color)
    text_area_time.anchor_point = (0.5, 0.5)
    text_area_time.anchored_position = (display.width / 2, display.height / 3)
    group.append(text_area_time)

    text_area_date = label.Label(font24, text=current_date, color=text_color)
    text_area_date.anchor_point = (0.5, 0.5)
    text_area_date.anchored_position = (display.width / 2, (display.height / 3) * 2)
    group.append(text_area_date)

    add_uptime_display(group)

    time.sleep(5)
    clean_up(group)
    print(f"show_time END - Free memory: {gc.mem_free()} bytes")

def show_time(text_color=white):
    print(f"show_time START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    current_time = get_formatted_time()
    text_area = label.Label(font48, text=current_time, color=text_color)
    text_area.anchor_point = (0.5, 0.5)
    text_area.anchored_position = (display.width / 2, display.height / 2)
    group.append(text_area)

    add_uptime_display(group)

    time.sleep(5)
    clean_up(group)
    print(f"show_time END - Free memory: {gc.mem_free()} bytes")

def show_full_date(text_color=white):
    print(f"show_date START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    current_date = get_formatted_full_date()
    max_width = int(display.width * 0.66)
    wrapped_date = "\n".join(wrap_text_to_pixels(current_date, max_width, font24))
    text_area = label.Label(font24, text=wrapped_date, color=text_color)
    text_area.anchor_point = (0.5, 0.5)
    text_area.anchored_position = (display.width / 2, display.height / 2)
    group.append(text_area)

    add_uptime_display(group)

    time.sleep(5)
    clean_up(group)
    print(f"show_date END - Free memory: {gc.mem_free()} bytes")

def show_date(text_color=white):
    print(f"show_date START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    current_date = get_formatted_date()
    max_width = int(display.width * 0.66)
    wrapped_date = "\n".join(wrap_text_to_pixels(current_date, max_width, font24))
    text_area = label.Label(font24, text=wrapped_date, color=text_color)
    text_area.anchor_point = (0.5, 0.5)
    text_area.anchored_position = (display.width / 2, display.height / 2)
    group.append(text_area)

    add_uptime_display(group)

    time.sleep(5)
    clean_up(group)
    print(f"show_date END - Free memory: {gc.mem_free()} bytes")

def show_ip(text_color=white):
    print(f"show_ip START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    current_ip = f"My IP address is {wifi.radio.ipv4_address}"
    max_width = int(display.width * 0.66)
    wrapped_ip = "\n".join(wrap_text_to_pixels(current_ip, max_width, font24))
    text_area = label.Label(font24, text=wrapped_ip, color=text_color)
    text_area.anchor_point = (0.5, 0.5)
    text_area.anchored_position = (display.width / 2, display.height / 2)
    group.append(text_area)

    add_uptime_display(group)

    time.sleep(5)
    clean_up(group)
    print(f"show_ip END - Free memory: {gc.mem_free()} bytes")

def show_temperature(text_color=white):
    print(f"show_temperature START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    temp = get_temperature()
    if temp is not None:
        temp_text = f"{temp:.1f}°F"
    else:
        temp_text = "--°F"
    text_area = label.Label(font48, text=temp_text, color=text_color)
    text_area.anchor_point = (0.5, 0.5)
    text_area.anchored_position = (display.width / 2, display.height / 3)
    group.append(text_area)

    add_uptime_display(group)

    text_area_time = label.Label(font24, text=get_formatted_time(), color=text_color)
    text_area_time.anchor_point = (0.5, 0.5)
    text_area_time.anchored_position = (display.width / 2, (display.height / 3) * 2)
    group.append(text_area_time)

    time.sleep(5)
    clean_up(group)
    print(f"show_temperature END - Free memory: {gc.mem_free()} bytes")

def show_temperature_stats(text_color=white):
    print(f"show_temperature_stats START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    min_temp, max_temp, avg_temp = get_temperature_stats()
    if min_temp is not None:
        temp_text = f"Min: {min_temp:.1f}°F\nMax: {max_temp:.1f}°F\nAvg: {avg_temp:.1f}°F"
    else:
        temp_text = "No data"
    text_area = label.Label(font24, text=temp_text, color=text_color)
    text_area.anchor_point = (0.0, 0.0)
    text_area.anchored_position = (15, 15)
    group.append(text_area)

    add_uptime_display(group)

    text_area_time = label.Label(font24, text=get_formatted_time(), color=text_color)
    text_area_time.anchor_point = (0.5, 0.5)
    text_area_time.anchored_position = (display.width / 2, (display.height / 3) * 2)
    group.append(text_area_time)

    time.sleep(5)
    clean_up(group)
    print(f"show_temperature END - Free memory: {gc.mem_free()} bytes")

def show_shapes():
    print(f"show_shapes START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    cx = int(display.width / 2)
    cy = int(display.height / 2)
    minor = min(cx, cy)
    pad = 5
    size = minor - pad
    half = int(size / 2)
    rect = Rect(cx - minor, cy - minor, size, size, stroke = 1, fill=blush, outline = blush)
    tri = Triangle(cx + pad, cy - pad, cx + pad + half, cy - minor,
                   cx + minor - 1, cy - pad, fill=green, outline = green)
    circ = Circle(cx - pad - half, cy + pad + half, half, fill=sky_blue, stroke = 1, outline = sky_blue)
    rnd = RoundRect(cx + pad, cy + pad, size, size, int(size / 5), stroke = 1,
                    fill=yellow, outline = yellow)

    group.append(rect)
    group.append(tri)
    group.append(circ)
    group.append(rnd)
    rect.fill = None
    tri.fill = None
    circ.fill = None
    rnd.fill = None

    time.sleep(2)

    rect.fill = blush
    tri.fill = green
    circ.fill = sky_blue
    rnd.fill = yellow
    time.sleep(2)
    clean_up(group)
    del rect
    del tri
    del circ
    del rnd
    gc.collect()
    print(f"show_shapes END - Free memory: {gc.mem_free()} bytes")

def sine_chart():
    print(f"sine_chart START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    cx = int(display.width / 2)
    cy = int(display.height / 2)
    minor = min(cx, cy)
    major = max(cx, cy)

    group.append(Line(cx, 0, cx, display.height, sky_blue)) # v
    group.append(Line(0, cy, display.width, cy, sky_blue)) # h

    for i in range(10):
        _n = simpleio.map_range(i, 0, 10, 0, major - 1)
        n = int(_n)
        group.append(Line(cx - n, cy - 5, cx - n, (cy - 5) + 11, sky_blue)) # v
        group.append(Line(cx + n, cy - 5, cx + n, (cy - 5) + 11, blush)) # v
        group.append(Line(cx - 5, cy - n, (cx - 5) + 11, cy - n, green)) # h
        group.append(Line(cx - 5, cy + n, (cx - 5) + 11, cy + n, yellow)) # h

    for x in range(display.width):
        y = cy - int(math.sin((x - cx) * 0.05) * float(minor * 0.5))
        bitmap[x, y] = 1
        z = cy - int(math.cos((x - cx) * 0.05) * float(minor * 0.5))
        bitmap[x, z] = 2
        w = cy - int(math.sin((x - cx) * 0.1) * -float(minor * 0.5))
        bitmap[x, w] = 3
    group.append(tile_grid)
    time.sleep(8)
    clean_up(group)
    print(f"sine_chart END - Free memory: {gc.mem_free()} bytes")

def widget0():
    print(f"widget0 START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    # Use temperature history data, or sample data if not enough history
    if len(temp_hist) >= 2:
        data = temp_hist
    else:
        data = [31, 42, 136, 58, 67, 88]
    num_points = len(data)
    
    # Calculate min/max for scaling
    if data:
        min_temp = min(data)
        max_temp = max(data)
        temp_range = max_temp - min_temp if max_temp != min_temp else 10
    else:
        min_temp, max_temp, temp_range = 0, 100, 100

    text_area = label.Label(terminalio.FONT, text="Temperature History", color=white)
    text_area.anchor_point = (0.5, 0.0)
    text_area.anchored_position = (display.width / 2, 3)
    group.append(text_area)
    for i in range(3):
        _y = simpleio.map_range(i, 0, 2, 20, display.height - 1)
        y = int(_y)
        group.append(Line(0, y, display.width, y, sky_blue))
    prev_x = 0
    _prev_y = simpleio.map_range(data[0], min_temp - 2, max_temp + 2, display.height - 1, 20)
    prev_y = int(_prev_y)
    for i in range(1, num_points):
        _new_x = simpleio.map_range(i, 0, num_points - 1, 0, display.width - 1)
        new_x = int(_new_x)
        _new_y = simpleio.map_range(data[i], min_temp - 2, max_temp + 2, display.height - 1, 20)
        new_y = int(_new_y)
        group.append(Line(prev_x, prev_y, new_x, new_y, aqua))
        prev_x = new_x
        prev_y = new_y

    for i in range(num_points):
        _x = simpleio.map_range(i, 0, num_points - 1, 0, display.width - 1)
        x = int(_x)
        _y = simpleio.map_range(data[i], min_temp - 2, max_temp + 2, display.height - 1, 20)
        y = int(_y)
        group.append(Circle(x, y, 1, fill=white, stroke = 1, outline = white))

    time.sleep(8)
    clean_up(group)
    print(f"widget0 END - Free memory: {gc.mem_free()} bytes")

def widget1():
    print(f"widget1 START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    # Use temperature history data, or sample data if not enough history
    if len(temp_hist) >= 2:
        data = temp_hist
    else:
        data = [31, 42, 36, 58, 67, 88]
    num_points = len(data)
    #bar_width = int(display.width / num_points) - 4
    bar_width = 2
    x_mapped_w = display.width + 2
    h_mapped_h = display.height + 20
    
    # Calculate min/max for scaling
    if data:
        min_temp = min(data)
        max_temp = max(data)
    else:
        min_temp, max_temp = 0, 100

    text_area = label.Label(terminalio.FONT, text="Temperature History", color=white)
    text_area.anchor_point = (0.5, 0.0)
    text_area.anchored_position = (display.width / 2, 3)
    group.append(text_area)
    for i in range(4):
        _y = simpleio.map_range(i, 0, 3, 20, display.height - 1)
        y = int(_y)
        group.append(Line(0, y, display.width, y, sky_blue))
    for i in range(num_points):
        _x = simpleio.map_range(i, 0, num_points, 0, x_mapped_w)
        x = int(_x)
        _height = simpleio.map_range(data[i], min_temp - 2, max_temp + 2, h_mapped_h, 0)
        height = int(_height)
        group.append(vectorio.Rectangle(pixel_shader=palette, width=bar_width,
                     height=display.height + 1, x=x, y=height, color_index = 2))

    time.sleep(8)
    clean_up(group)
    print(f"widget1 END - Free memory: {gc.mem_free()} bytes")

def text_align():
    print(f"text_align START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    
    # Set font for this display
    display_font = font19
    
    # Get current time and temperature
    current_time = get_formatted_time()
    temp = get_temperature()
    temp_text = f"{temp:.1f}°F" if temp is not None else "--°F"
    
    # Alternate time and temperature in the 9 boxes (left to right, top to bottom)
    # Pattern: time, temp, time, temp, time, temp, time, temp, time
    text_area_top_left = label.Label(display_font, text=current_time, color=blush)
    text_area_top_left.anchor_point = (0.0, 0.0)
    text_area_top_left.anchored_position = (0, 0)

    text_area_top_middle = label.Label(display_font, text=temp_text, color=orange)
    text_area_top_middle.anchor_point = (0.5, 0.0)
    text_area_top_middle.anchored_position = (display.width / 2, 0)

    text_area_top_right = label.Label(display_font, text=current_time, color=yellow)
    text_area_top_right.anchor_point = (1.0, 0.0)
    text_area_top_right.anchored_position = (display.width, 0)

    text_area_middle_left = label.Label(display_font, text=temp_text, color=green)
    text_area_middle_left.anchor_point = (0.0, 0.5)
    text_area_middle_left.anchored_position = (0, display.height / 2)

    text_area_middle_middle = label.Label(display_font, text=current_time, color=cyan)
    text_area_middle_middle.anchor_point = (0.5, 0.5)
    text_area_middle_middle.anchored_position = (display.width / 2, display.height / 2)

    text_area_middle_right = label.Label(display_font, text=temp_text, color=sky_blue)
    text_area_middle_right.anchor_point = (1.0, 0.5)
    text_area_middle_right.anchored_position = (display.width, display.height / 2)

    text_area_bottom_left = label.Label(display_font, text=current_time, color=purple)
    text_area_bottom_left.anchor_point = (0.0, 1.0)
    text_area_bottom_left.anchored_position = (0, display.height)

    text_area_bottom_middle = label.Label(display_font, text=temp_text, color=pink)
    text_area_bottom_middle.anchor_point = (0.5, 1.0)
    text_area_bottom_middle.anchored_position = (display.width / 2, display.height)

    text_area_bottom_right = label.Label(display_font, text=current_time, color=white)
    text_area_bottom_right.anchor_point = (1.0, 1.0)
    text_area_bottom_right.anchored_position = (display.width, display.height)

    # Add dividing lines to separate the 9 areas
    third_width = int(display.width / 3)
    two_thirds_width = int(2 * display.width / 3)
    third_height = int(display.height / 3)
    two_thirds_height = int(2 * display.height / 3)
    
    # Vertical lines
    group.append(Line(third_width, 0, third_width, display.height - 1, yellow))
    group.append(Line(two_thirds_width, 0, two_thirds_width, display.height - 1, white))
    
    # Horizontal lines
    group.append(Line(0, third_height, display.width - 1, third_height, orange))
    group.append(Line(0, two_thirds_height, display.width - 1, two_thirds_height, green))

    group.append(text_area_top_middle)
    group.append(text_area_top_left)
    group.append(text_area_top_right)
    group.append(text_area_middle_middle)
    group.append(text_area_middle_left)
    group.append(text_area_middle_right)
    group.append(text_area_bottom_middle)
    group.append(text_area_bottom_left)
    group.append(text_area_bottom_right)

    time.sleep(8)
    clean_up(group)
    print(f"text_align END - Free memory: {gc.mem_free()} bytes")

def currently():
    print(f"currently START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    #my_font = bitmap_font.load_font("/fonts/ComicSansMS-21.bdf")
    text_sample = get_om_current()
    text_sample = "\n".join(wrap_text_to_pixels(text_sample, display.width - 10, font17))
    
    text_area = label.Label(font24, text="Currently", color=yellow)
    text_area.anchor_point = (0.0, 0.0)
    text_area.anchored_position = (5, 5)

    sample_text = label.Label(font17, text=text_sample)
    sample_text.anchor_point = (0.5, 0.0)
    sample_text.anchored_position = (display.width / 2, display.height / 5)

    group.append(text_area)
    group.append(sample_text)

    time.sleep(10)
    clean_up(group)

    #del my_font
    gc.collect()
    print(f"currently END - Free memory: {gc.mem_free()} bytes")

def current_forecast():
    print(f"current_forecast START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    text_sample = get_nws_00()
    text_sample = "\n".join(wrap_text_to_pixels(text_sample, display.width - 10, font17))
    
    text_area = label.Label(font24, text="Current Forecast", color=sky_blue)
    text_area.anchor_point = (0.0, 0.0)
    text_area.anchored_position = (5, 5)

    sample_text = label.Label(font17, text=text_sample)
    sample_text.anchor_point = (0.5, 0.0)
    sample_text.anchored_position = (display.width / 2, display.height / 5)

    group.append(text_area)
    group.append(sample_text)

    time.sleep(10)
    clean_up(group)
    gc.collect()
    print(f"current_forecast END - Free memory: {gc.mem_free()} bytes")

def next_forecast():
    print(f"next_forecast START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    text_sample = get_nws_01()
    text_sample = "\n".join(wrap_text_to_pixels(text_sample, display.width - 10, font17))
    
    text_area = label.Label(font24, text="Next Forecast", color=green)
    text_area.anchor_point = (0.0, 0.0)
    text_area.anchored_position = (5, 5)

    sample_text = label.Label(font17, text=text_sample)
    sample_text.anchor_point = (0.5, 0.0)
    sample_text.anchored_position = (display.width / 2, display.height / 5)

    group.append(text_area)
    group.append(sample_text)

    time.sleep(10)
    clean_up(group)
    gc.collect()
    print(f"next_forecast END - Free memory: {gc.mem_free()} bytes")

def later_forecast():
    print(f"later_forecast START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    text_sample = get_nws_02()
    text_sample = "\n".join(wrap_text_to_pixels(text_sample, display.width - 10, font17))
    
    text_area = label.Label(font24, text="Later Forecast", color=pink)
    text_area.anchor_point = (0.0, 0.0)
    text_area.anchored_position = (5, 5)

    sample_text = label.Label(font17, text=text_sample)
    sample_text.anchor_point = (0.5, 0.0)
    sample_text.anchored_position = (display.width / 2, display.height / 5)

    group.append(text_area)
    group.append(sample_text)

    time.sleep(10)
    clean_up(group)
    gc.collect()
    print(f"later_forecast END - Free memory: {gc.mem_free()} bytes")

def bitmap_example():
    print(f"bitmap_example START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    blinka_bitmap = displayio.OnDiskBitmap("/blinka_computer.bmp")
    blinka_grid = displayio.TileGrid(blinka_bitmap, pixel_shader=blinka_bitmap.pixel_shader)
    gc.collect()
    group.append(blinka_grid)

    time.sleep(2)
    clean_up(group)

    del blinka_grid
    del blinka_bitmap
    gc.collect()
    print(f"bitmap_example END - Free memory: {gc.mem_free()} bytes")

def sensor_values():
    print(f"sensor_values START - Free memory: {gc.mem_free()} bytes")
    gc.collect()
    text_x = "X: %d" % randint(-25, 25)
    text_y = "Y: %d" % randint(-25, 25)
    text_z = "Z: %d" % randint(-25, 25)
    x_text = label.Label(terminalio.FONT, text=text_x, color=blush)
    x_text.anchor_point = (0.0, 0.0)
    x_text.anchored_position = (2, 0)
    y_text = label.Label(terminalio.FONT, text=text_y, color=green)
    y_text.anchor_point = (0.0, 0.0)
    y_text.anchored_position = (2, 10)
    z_text = label.Label(terminalio.FONT, text=text_z, color=sky_blue)
    z_text.anchor_point = (0.0, 0.0)
    z_text.anchored_position = (2, 20)
    group.append(x_text)
    group.append(y_text)
    group.append(z_text)

    for i in range(40):
        if i == 10:
            group.scale = 2
        elif i == 20:
            group.scale = 3
        elif i == 30:
            group.scale = 4
        x_text.text = "X: %d" % randint(-50, 50)
        y_text.text = "Y: %d" % randint(-50, 50)
        z_text.text = "Z: %d" % randint(-50, 50)
        time.sleep(0.1)
    time.sleep(0.1)
    clean_up(group)
    group.scale = 1
    print(f"sensor_values END - Free memory: {gc.mem_free()} bytes")

display.root_group = group

x = 0
while True:
    x += 1
    print("-"*60 + "\nIteration:", x)
    print(" - Current Time:", get_formatted_time())
    show_temperature_stats(green)
    show_ip(orange)
    show_time(blush)
    #print("Current Date:", get_formatted_date())
    show_full_date(sky_blue)
    #show_shapes()
    show_date_time(white)
    show_temperature(yellow)
    #sine_chart()
    show_time(green)
    widget0()
    show_temperature(cyan)
    show_temperature_stats(pink)
    widget1()
    #text_align()
    show_time(orange)
    show_temperature_stats(cyan)
    currently()
    current_forecast()
    next_forecast()
    later_forecast()
    #bitmap_example()
    #sensor_values()
    show_date(yellow)
    show_temperature(cyan)
    print("\n")
