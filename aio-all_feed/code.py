import neopixel
import time
import board
import keypad
import displayio
from adafruit_onewire.bus import OneWireBus
import adafruit_ds18x20
import digitalio
from os import getenv
import rtc
import gc
import wifi
import adafruit_connection_manager
import adafruit_requests
from adafruit_io.adafruit_io import IO_HTTP, AdafruitIO_RequestError
import adafruit_minimqtt.adafruit_minimqtt as MQTT
from adafruit_display_text import label
from adafruit_display_text.bitmap_label import Label
from fourwire import FourWire
from vectorio import Circle
from adafruit_st7789 import ST7789
from adafruit_bitmap_font import bitmap_font
import busio
import analogio
import microcontroller
from watchdog import WatchDogMode

TEMP_REFRESH_SEC = 10
TIME_SYNC_INTERVAL_SEC = 60 * 60 * 2
TIME_SYNC_RETRY_SEC = 60 * 5
LIFE_FREQ = .75 # flash on and off every LIFE_FREQ seconds to show the program is running
MQTT_RECONNECT_SEC = 10
FEED_SUMMARY_INTERVAL_SEC = 15 * 60
AGGREGATOR_FEED_KEY = "aggregator.weather-log"
SUMMARY_UPLOAD_RETRY_SEC = 15
WIFI_CONNECT_RETRIES = 3
WIFI_RETRY_DELAY_SEC = 15
WATCHDOG_SECONDS = 8
NETWORK_STALL_SECONDS = 300
HTTP_TIMEOUT_SECONDS = 3


class MonitorSession(adafruit_requests.Session):
    def get(self, url, **kwargs):
        kwargs.setdefault("timeout", HTTP_TIMEOUT_SECONDS)
        return super().get(url, **kwargs)

    def post(self, url, **kwargs):
        kwargs.setdefault("timeout", HTTP_TIMEOUT_SECONDS)
        return super().post(url, **kwargs)


def check_monitor_watchdog(now):
    if now - last_feed_message >= NETWORK_STALL_SECONDS:
        print("WATCHDOG: no feed messages for 5 minutes; resetting")
        microcontroller.reset()
        return
    if (summary_pending_since is not None
            and now - summary_pending_since >= NETWORK_STALL_SECONDS):
        print("WATCHDOG: summary upload stalled for 5 minutes; resetting")
        microcontroller.reset()
        return
    microcontroller.watchdog.feed()

# QoS (Quality of Service) for MQTT subscriptions:
#   0 = "at most once"  - fire-and-forget, no acknowledgement (fastest, lightest)
#   1 = "at least once" - broker resends until acknowledged (may get duplicates)
# Adafruit IO supports QoS 0 and 1. QoS 0 is fine for display/dashboard use.
MQTT_QOS = 0

R1 = 221.05
R2 = 221.20
DIVIDER_RATIO = (R1 + R2) / R2
VREF = 3.33


def read_volts(vmon):
    pin_voltage = (vmon.value * VREF) / 65535
    battery_voltage = pin_voltage * DIVIDER_RATIO
    return battery_voltage

def c2f(c):
    return c * 9 / 5 + 32

def center_label(label, display_width, display_height):
    """Center a Label using its current bounding box."""
    box_x, box_y, box_width, box_height = label.bounding_box
    label.x = (display_width // 2) - (box_width // 2) - box_x
    label.y = (display_height // 2) - (box_height // 2) - box_y

def get_option_state():
    return "ON" if optn.value else "OFF"

def read_temperature_f():
    return c2f(ds18b20.temperature)

def build_display_text(temp_f):
    return " {0:0.1f} °F\n{1}".format(temp_f, get_short_time())

def update_display_text(temp_f=None):
    if temp_f is None:
        temp_f = read_temperature_f()
    text_value = build_display_text(temp_f)
    #text_area.text = text_value
    print("Display text updated:", text_value)
    #center_label(text_area, display.width, display.height)
    return text_value

def get_short_time():
    """Get current time from RTC in 12-hour format."""
    try:
        current_time = rtc_clock.datetime

        # Format time in 12-hour format with AM/PM
        hour_12 = current_time.tm_hour
        am_pm = "AM"
        if hour_12 >= 12:
            am_pm = "PM"
            if hour_12 > 12:
                hour_12 -= 12
        elif hour_12 == 0:
            hour_12 = 12

        return "{}:{:02d} {}".format(
            hour_12, current_time.tm_min, am_pm)
    except Exception as e:
        print("Time formatting error: {}".format(e))
        return "RTC not set"

TIMEZONE = getenv('TIMEZONE', 'UTC')

def sync_time_from_adafruit_io():
    """Sync RTC time from Adafruit IO time service with local timezone"""
    global io

    try:
        pixels[0] = COLORS['CYAN']
        print("Syncing time from Adafruit IO...")

        # Check if WiFi is connected
        if not wifi.radio.connected:
            print("WiFi not connected - skipping time sync")
            return False

        # Get timezone from settings.toml
        print(f"Using timezone: {TIMEZONE}")

        # Initialize Adafruit IO client if not already done
        if io is None:
            aio_username = getenv('AIO_USERNAME')
            aio_key = getenv('AIO_KEY')

            if not aio_username or not aio_key:
                print("Adafruit IO credentials not found in settings.toml")
                pixels[0] = COLORS['RED']
                return False

            try:
                io = IO_HTTP(aio_username, aio_key, requests)
                print("Adafruit IO client initialized")
            except Exception as e:
                print(f"Failed to initialize Adafruit IO client: {e}")
                pixels[0] = COLORS['RED']
                return False

        # Get time from Adafruit IO using the proper method
        try:
            # Use the authenticated IO method to get time with timezone
            current_time = io.receive_time(TIMEZONE)
            print(f"Received time from Adafruit IO: {current_time}")

            # Set RTC time directly from the time.struct_time object
            rtc_clock.datetime = current_time

            pixels[0] = COLORS['GREEN']
            print(f"RTC time set successfully for timezone: {TIMEZONE}")
            return True

        except AdafruitIO_RequestError as e:
            print(f"Adafruit IO request error: {e}")
            pixels[0] = COLORS['ORANGE']
            return False

    except Exception as e:
        pixels[0] = COLORS['RED']
        print(f"Time sync failed: {e}")
        return False

# -- Adafruit IO MQTT callbacks --
def on_connect(client, userdata, flags, result):
    client.subscribe(aio_username + "/feeds/+", qos=MQTT_QOS)
    client.subscribe(aio_username + "/throttle", qos=MQTT_QOS)
    client.subscribe(aio_username + "/errors", qos=MQTT_QOS)
    print("MQTT CONNECTED: base-feed, throttle and error subscriptions completed")

def on_disconnect(client, userdata, rc):
    print("MQTT DISCONNECTED")

FEED_KEY_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789-."
feed_counts = {}
pending_summaries = []

def is_feed_key(name):
    """True if name looks like a canonical AIO feed key (not an alias)."""
    if not name or name.isdigit():
        return False
    for ch in name:
        if ch not in FEED_KEY_CHARS:
            return False
    return True

def on_message(client, topic, message):
    global last_feed_message
    # topic format: username/feeds/feed-key
    feed = topic.split("/")[-1]
    # Adafruit IO fans each update out on every alias of the feed:
    #   - feed key      e.g. hours24visibility   <- keep (canonical)
    #   - feed name     e.g. Hours24Visibility / om_current
    #   - numeric ID    e.g. 3250058
    # Real feed keys only contain lowercase a-z, 0-9, '-' and '.', so
    # anything else is an alias and is ignored.
    if not is_feed_key(feed):
        return
    if (topic.startswith(aio_username + "/feeds/")
            and feed != AGGREGATOR_FEED_KEY):
        last_feed_message = time.monotonic()
        feed_counts[feed] = feed_counts.get(feed, 0) + 1
    line = f"{get_timestamp()} {feed}"
    if topic == aio_username + "/throttle":
        line = "THROTTLE " + line
    elif topic == aio_username + "/errors":
        line = "ERROR " + line
    else:
        print(line)
        do1_label.text = line
        return
    print(line)
    do2_label.text = line

def get_timestamp():
    """Current RTC time as 'yyyymmddhhmmss'."""
    t = rtc_clock.datetime
    return "{:04d}{:02d}{:02d}{:02d}{:02d}{:02d}".format(
        t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec)

def mqtt_connect():
    try:
        microcontroller.watchdog.feed()
        if not wifi.radio.connected:
            print("Reconnecting WiFi...")
            wifi.radio.connect(ssid, password, timeout=3)
        print("Connecting to Adafruit IO MQTT...")
        mqtt_client.connect()
        return True
    except Exception as e:
        print(f"MQTT connect failed: {e}")
        return False

COLORS = {
    'OFF': (0, 0, 0),
    'RED': (255, 0, 0),
    'GREEN': (0, 255, 0),
    'BLUE': (0, 0, 255),
    'YELLOW': (255, 255, 0),
    'CYAN': (0, 255, 255),
    'MAGENTA': (255, 0, 255),
    'WHITE': (255, 255, 255),
    'ORANGE': (255, 165, 0),
    'PURPLE': (128, 0, 128),
    'VIOLET': (238, 130, 238),
    'GRAY': (32, 32, 32),
    "DARK_GRAY": (4, 8, 6),
    'MAROON': (128, 0, 0),
    'DARK_GREEN': (0, 128, 0),
    'NAVY': (0, 0, 128),
    'OLIVE': (128, 128, 0),
    'TEAL': (0, 128, 128),
}

# -- Set Up the Display --
displayio.release_displays()
mosi = board.GP11
sclck = board.GP10
rst = board.GP14
cs = board.GP9
dc = board.GP7
bl = digitalio.DigitalInOut(board.GP8)
bl.direction = digitalio.Direction.OUTPUT
bl.value = True
spi = busio.SPI(clock=sclck, MOSI=mosi)
spi_bus = FourWire(spi, command=dc, chip_select=cs, reset=rst)
display = ST7789(spi_bus, width=320, height=240, rotation=270, auto_refresh=True)

# -- Set Up NeoPixels --
pixels = neopixel.NeoPixel(board.GP28, 6, brightness=0.3, pixel_order="RGB", auto_write=False)
pixels.fill(COLORS['OFF'])
pixels.show()

# -- Connect WiFi and Set Time --
ssid = getenv("CIRCUITPY_WIFI_SSID")
password = getenv("CIRCUITPY_WIFI_PASSWORD")
print()
print("Connecting to WiFi")

# Connect to Wi-Fi, retrying transient failures before stopping.
for attempt in range(WIFI_CONNECT_RETRIES + 1):
    try:
        wifi.radio.connect(ssid, password)
        break
    except TypeError:
        print("Could not find WiFi info. Check your settings.toml file!")
        raise
    except ConnectionError as e:
        if attempt >= WIFI_CONNECT_RETRIES:
            print("WiFi connection failed after {} attempts: {}".format(
                attempt + 1, e))
            raise
        print("WiFi connection failed (attempt {}/{}): {}".format(
            attempt + 1, WIFI_CONNECT_RETRIES + 1, e))
        print("Retrying in {} seconds...".format(WIFI_RETRY_DELAY_SEC))
        time.sleep(WIFI_RETRY_DELAY_SEC)

print("Connected to WiFi")

pool = adafruit_connection_manager.get_radio_socketpool(wifi.radio)
ssl_context = adafruit_connection_manager.get_radio_ssl_context(wifi.radio)
requests = MonitorSession(pool, ssl_context)
io = None

#  prints MAC address to REPL
print("My MAC addr:", [hex(i) for i in wifi.radio.mac_address])

#  prints IP address to REPL
print(f"My IP address is {wifi.radio.ipv4_address}")

rtc_clock = rtc.RTC()

sync_time_from_adafruit_io()
print(f"RTC time after sync: {rtc_clock.datetime}")

print("-" * 40, "\n")

# -- Set Up Adafruit IO MQTT client --
aio_username = getenv("AIO_USERNAME")
aio_key = getenv("AIO_KEY")
mqtt_client = MQTT.MQTT(
    broker="io.adafruit.com",
    port=8883,
    username=aio_username,
    password=aio_key,
    socket_pool=pool,
    ssl_context=ssl_context,
    is_ssl=True,
    socket_timeout=1,
    recv_timeout=3,
    connect_retries=1,
)
mqtt_client.on_connect = on_connect
mqtt_client.on_disconnect = on_disconnect
mqtt_client.on_message = on_message

# -- Set Up Sensors --
i2c = board.STEMMA_I2C()

board_led = digitalio.DigitalInOut(board.LED)
board_led.direction = digitalio.Direction.OUTPUT
board_led.value = True

vsys = analogio.AnalogIn(board.A1)
print("Battery voltage: {:.2f} V".format(read_volts(vsys)))


# -- Set up RGB LEDs on GP18/GP19/GP20 (common-anode) --
# Common-anode LEDs on GP18/GP19/GP20.
# For common-anode wiring: HIGH = off, LOW = on.
led_red = digitalio.DigitalInOut(board.GP18)
led_red.direction = digitalio.Direction.OUTPUT
led_red.value = True

led_green = digitalio.DigitalInOut(board.GP19)
led_green.direction = digitalio.Direction.OUTPUT
led_green.value = True

led_blue = digitalio.DigitalInOut(board.GP20)
led_blue.direction = digitalio.Direction.OUTPUT
led_blue.value = True

# -- Set up Switches on GP22 and GP21 --
switch_gp22 = keypad.Keys((board.GP22,), value_when_pressed=False, pull=True)
optn = digitalio.DigitalInOut(board.GP21)
optn.direction = digitalio.Direction.INPUT
optn.pull = digitalio.Pull.UP

# -- Set up DS18B20 Temperature Sensor on GP15 --

ow_bus = OneWireBus(board.GP15)

devices = ow_bus.scan()
for device in devices:
    print("ROM = {} \tFamily = 0x{:02x}".format([hex(i) for i in device.rom], device.family_code))

ds18b20 = adafruit_ds18x20.DS18X20(ow_bus, devices[0])

ds18b20.resolution = 9
print('Resolution: {0} bits'.format(ds18b20.resolution))
temperature_c = ds18b20.temperature
temperature_f = c2f(temperature_c)
print('Temperature: {0:0.3f} °C'.format(temperature_c))
print('Temperature: {0:0.3f} °F'.format(temperature_f))


#--- Default Shapes/Text Demo ---
main_group = displayio.Group()
display.root_group = main_group

bg_bitmap = displayio.Bitmap(320, 240, 2)
color_palette = displayio.Palette(2)
color_palette[1] = 0xFFFF00  # Yellow
color_palette[0] = 0x000088  # Navy Blue

bg_sprite = displayio.TileGrid(bg_bitmap, pixel_shader=color_palette, x=0, y=0)
main_group.append(bg_sprite)

# inner_circle = Circle(pixel_shader=color_palette, x=160, y=120, radius=115, color_index=1)
# main_group.append(inner_circle)

font = bitmap_font.load_font("/fonts/Calibri-33.pcf")
font17 = bitmap_font.load_font("/fonts/Calibri-17.pcf")
font12 = bitmap_font.load_font("/fonts/Calibri-12.pcf")
font15 = bitmap_font.load_font("/fonts/Calibri-15.pcf")
font12mono = bitmap_font.load_font("/fonts/ComicSansMS-14.pcf")
font14mono = bitmap_font.load_font("/fonts/FreeMono-14.pcf")
font19 = bitmap_font.load_font("/fonts/ComicSansMS-19.pcf")


time_label = label.Label(font17, text="Line 1", color=0xFFFFFF)
time_label.x = 5
time_label.y = 5
main_group.append(time_label)

temp_label = label.Label(font17, text="Line 2", color=0xFFFF00)
temp_label.x = 100
temp_label.y = 5
main_group.append(temp_label)

volts_label = label.Label(font12, text="9.9 v", color=0x00FF00)
volts_label.anchor_point = (1.0, 0.0)
volts_label.anchored_position = (315, 5)
main_group.append(volts_label)

do1_label = label.Label(font14mono, text="D01", color=0xFF9999)
do1_label.anchor_point = (0.0, 0.0)
do1_label.anchored_position = (5, 72)
main_group.append(do1_label)

do2_label = label.Label(font14mono, text="D02", color=0x99FF99)
do2_label.anchor_point = (0.0, 0.0)
do2_label.anchored_position = (5, 90)
main_group.append(do2_label)


last_temp_read = 0.0
next_time_sync = time.monotonic() + TIME_SYNC_INTERVAL_SEC

#text_to_show = update_display_text(temperature_f)
opt_pos = optn.value
optn_state = get_option_state()
print("Optn switch state:", optn_state)

last_feed_message = time.monotonic()
summary_pending_since = None
print("Monitor reset reason:", microcontroller.cpu.reset_reason)
# Enable after hardware setup; feed only from foreground work, never a timer.
microcontroller.watchdog.timeout = WATCHDOG_SECONDS
microcontroller.watchdog.mode = WatchDogMode.RESET
mqtt_connect()
next_mqtt_retry = time.monotonic() + MQTT_RECONNECT_SEC
next_summary_publish = 0.0
next_feed_summary = time.monotonic() + FEED_SUMMARY_INTERVAL_SEC

board_led.value = False
next_life_flash = time.monotonic() + LIFE_FREQ
while True:
    time.sleep(0)
    now = time.monotonic()
    check_monitor_watchdog(now)

    # -- Service MQTT (non-blocking-ish poll) and auto-reconnect --
    if mqtt_client.is_connected():
        try:
            mqtt_client.loop(timeout=1)  # must be >= socket_timeout
        except Exception as e:
            print(f"MQTT loop error: {e}")
            try:
                mqtt_client.disconnect()
            except Exception as close_error:
                print("MQTT cleanup failed:", close_error)
            next_mqtt_retry = now + MQTT_RECONNECT_SEC
    elif now >= next_mqtt_retry:
        mqtt_connect()
        next_mqtt_retry = now + MQTT_RECONNECT_SEC

    if now >= next_feed_summary:
        summary_fields = ['"{}"'.format(get_timestamp())]
        single_feed_count = 0
        for feed, count in feed_counts.items():
            if count > 1:
                summary_fields.extend(['"{}"'.format(feed), str(count)])
            elif count == 1:
                single_feed_count += 1
        summary_fields.extend(['"single feeds"', str(single_feed_count)])
        summary_line = ", ".join(summary_fields)
        print(summary_line)
        if not pending_summaries:
            summary_pending_since = time.monotonic()
        pending_summaries.append(summary_line)
        feed_counts = {}
        next_feed_summary += FEED_SUMMARY_INTERVAL_SEC

    if (wifi.radio.connected and pending_summaries
            and now >= next_summary_publish):
        try:
            microcontroller.watchdog.feed()
            if io is None:
                io = IO_HTTP(aio_username, aio_key, requests)
            io.send_data(AGGREGATOR_FEED_KEY, pending_summaries[0])
            pending_summaries.pop(0)
            summary_pending_since = time.monotonic() if pending_summaries else None
        except Exception as e:
            print("HTTP summary upload failed: {}".format(e))
            next_summary_publish = time.monotonic() + SUMMARY_UPLOAD_RETRY_SEC

    # if now >= next_time_sync:
    #     if sync_time_from_adafruit_io():
    #         print(f"RTC time after sync: {rtc_clock.datetime}")
    #         next_time_sync = now + TIME_SYNC_INTERVAL_SEC
    #         print(f"Next time sync scheduled in {TIME_SYNC_INTERVAL_SEC} seconds.")
    #     else:
    #         next_time_sync = now + TIME_SYNC_RETRY_SEC

    # if optn.value != opt_pos:
    #     opt_pos = optn.value
    #     optn_state = get_option_state()
    #     print("Optn switch state:", optn_state)
    #     if optn_state == "ON":
    #         bl.value = True
    #     else:
    #         bl.value = False
    # Drain all queued button events every loop pass.
    if now >= next_life_flash:
        #print("Flashing board LED to indicate program is running...", time.monotonic())
        board_led.value = not board_led.value
        next_life_flash = now + LIFE_FREQ

    # switch_event = switch_gp22.events.get()
    # while switch_event:
    #     if switch_event.pressed:
    #         print("Switch GP22: pressed")
    #         #text_to_show = update_display_text()
    #         #print(get_short_time())
    #         #optn_state = get_option_state()
    #         #print("Optn switch state:", optn_state)

    #     elif switch_event.released:
    #         print("Switch GP22: released")
    #     switch_event = switch_gp22.events.get()

    # if now - last_temp_read >= TEMP_REFRESH_SEC:
    #     print("Reading temperature from DS18B20...", time.monotonic())  
    #     temp_label.text = "{0:0.1f} °F".format(read_temperature_f())
    #     #text_to_show = update_display_text()
    #     last_temp_read = now
    #     #print(text_to_show)
    
    check_monitor_watchdog(time.monotonic())
    time.sleep(0.01)
