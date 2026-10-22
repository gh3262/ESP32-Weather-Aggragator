"""
Weather Helpers Library
Common functions and constants for CircuitPython weather forecast files.
Centralizes shared code to reduce duplication and improve maintainability.
"""
import os
import time
import ssl
import wifi
import socketpool
import board
import neopixel
import adafruit_requests
from adafruit_io.adafruit_io import IO_HTTP

# =============================================================================
# CONSTANTS
# =============================================================================

LOCATION_ID = 2738
THROTTLE_THRESHOLD = 20
THROTTLE_SLEEP = 15
SLEEP_DURATION_SHORT = 10  # Used by f1-f8
SLEEP_DURATION_LONG = 300  # Used by f6a, f7a, f8a

# =============================================================================
# NEOPIXEL SETUP
# =============================================================================

pixel = neopixel.NeoPixel(board.NEOPIXEL, 1)
pixel.brightness = 0.3
pixel.fill(0x222222)

# =============================================================================
# INITIALIZATION FUNCTIONS
# =============================================================================

def init_wifi_and_io():
    """
    Initialize WiFi connection and Adafruit IO client.
    
    Returns:
        IO_HTTP: Configured Adafruit IO client instance
    """
    wifi.radio.connect(os.getenv('CIRCUITPY_WIFI_SSID'), 
                      os.getenv('CIRCUITPY_WIFI_PASSWORD'))
    print("Connected to WiFi")
    print(f"Connected to {os.getenv('CIRCUITPY_WIFI_SSID')}")
    print(f"My IP address: {wifi.radio.ipv4_address}")

    pool = socketpool.SocketPool(wifi.radio)
    requests = adafruit_requests.Session(pool, ssl.create_default_context())
    return IO_HTTP(os.getenv('aio_username'), os.getenv('aio_key'), requests)

# =============================================================================
# CONVERSION FUNCTIONS
# =============================================================================

def pct(x):
    """
    Convert decimal to percentage.
    
    Args:
        x (float): Decimal value (e.g., 0.75)
    
    Returns:
        int: Percentage value (e.g., 75)
    """
    return int(x * 100)

def tof(c):
    """
    Convert Celsius to Fahrenheit with one decimal place.
    
    Args:
        c (float): Temperature in Celsius
    
    Returns:
        float: Temperature in Fahrenheit, rounded to 1 decimal
    """
    return round((float(c) * 9 / 5) + 32, 1)

def tomph(k):
    """
    Convert km/h to mph.
    
    Args:
        k (float): Speed in kilometers per hour
    
    Returns:
        int: Speed in miles per hour
    """
    return int(k * 0.62137119)

def mtom(meters):
    """
    Convert meters to miles.
    
    Args:
        meters (float): Distance in meters
    
    Returns:
        int: Distance in miles
    """
    return int(meters * 0.00062137)

def windir(wd):
    """
    Convert wind direction in degrees to compass direction.
    
    Uses 16-point compass rose with mathematically calculated sectors.
    Each sector is 22.5 degrees wide.
    
    Args:
        wd (float): Wind direction in degrees (0-360)
    
    Returns:
        str: Compass direction (N, NNE, NE, ENE, E, ESE, SE, SSE,
             S, SSW, SW, WSW, W, WNW, NW, NNW)
    """
    directions = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 
                  'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW']
    index = int((wd + 11.25) / 22.5) % 16
    return directions[index]

# =============================================================================
# API MANAGEMENT FUNCTIONS
# =============================================================================

def sp(io):
    """
    Check and wait for API throttle limit to recover.
    
    Monitors Adafruit IO API throttle limit and pauses execution
    if remaining calls fall below threshold. Provides visual feedback
    via NeoPixel during waiting period.
    
    Args:
        io (IO_HTTP): Adafruit IO client instance
    """
    rrl = io.get_remaining_throttle_limit()
    while rrl < THROTTLE_THRESHOLD:
        pixel[0] = 0xFF0000  # Red during throttle wait
        print(rrl, f"pause {THROTTLE_SLEEP} sec")
        time.sleep(THROTTLE_SLEEP)
        rrl = io.get_remaining_throttle_limit()
    
    # Brief status indication
    pixel.fill(0xFF8888)
    print(time.monotonic())
    time.sleep(10)
    pixel.fill(0x00FFFF)

def get_feeds_batch(io, feed_names):
    """
    Retrieve multiple Adafruit IO feeds in a single batch operation.
    
    This is a generic batch retrieval function that can be used by any file.
    Reduces API calls from N individual calls to 1 batch operation.
    
    Args:
        io (IO_HTTP): Adafruit IO client instance
        feed_names (list): List of feed name strings to retrieve
    
    Returns:
        dict: Dictionary mapping feed names to feed objects
              Format: {feed_name: feed_object, ...}
    """
    return {name: io.get_feed(name) for name in feed_names}

# =============================================================================
# VISUAL FEEDBACK FUNCTIONS
# =============================================================================

def set_stage_color(stage):
    """
    Set NeoPixel color based on execution stage.
    
    Provides visual feedback about script progress using predefined colors.
    
    Args:
        stage (str): Stage identifier
            'init' - Gray (initial)
            'weather' - Blue (fetching weather)
            'feeds' - Yellow (fetching feeds)
            'send' - Pink (sending data)
            'complete' - Purple (finished)
    """
    colors = {
        'init': 0x222222,      # Gray
        'weather': 0x6699FF,   # Blue
        'feeds': 0xFFFF00,     # Yellow
        'send': 0x44FF44,      # Lime
        'complete': 0x9999FF   # Purple
    }
    pixel.fill(colors.get(stage, 0xFFFFFF))

# =============================================================================
# DATA VALIDATION FUNCTIONS
# =============================================================================

def safe_get(data, key, default=None, missing=None, label=None, *_ignored):
    """Safely retrieve value from dictionary with optional default and logging.

    Accepts additional positional arguments for compatibility with older call sites
    or stale bytecode versions to avoid TypeError on invocation.

    Args:
        data (dict): Dictionary to retrieve from
        key (str): Key to look up
        default: Default value if key doesn't exist
        missing (set|list|None): Optional collection to record missing keys
        label (str|None): Friendly label to log when missing
    
    Returns:
        Value from dictionary or default
    """
    try:
        return data[key]
    except (KeyError, TypeError):
        if missing is not None and label is not None:
            missing.add(label) if hasattr(missing, "add") else missing.append(label)
        if label:
            print(f"safe_get error for key {label}")
        return default


def make_send_with_fallback(io, missing_labels):
    """Create a sender that emits "0" for labels marked missing."""
    def _send(feed_key, value, label):
        if label in missing_labels:
            io.send_data(feed_key, "0")
        else:
            io.send_data(feed_key, value)
    return _send


def log_missing_labels(io, rt_feed_key, missing_labels, file_name):
    """Log missing keys to the rt feed with a consistent format."""
    for label in missing_labels:
        msg = f"safe_get error for key {label} in {file_name}"
        print(msg)
        io.send_data(rt_feed_key, msg)

# =============================================================================
# USAGE EXAMPLES
# =============================================================================

"""
Example usage in forecast files:

# Import the helpers
from weather_helpers import (
    init_wifi_and_io, pct, tof, tomph, windir, sp, get_feeds_batch,
    set_stage_color, LOCATION_ID, SLEEP_DURATION_SHORT, pixel
)

# Initialize
io = init_wifi_and_io()

# Fetch weather
set_stage_color('weather')
forecast = io.receive_weather(LOCATION_ID)

# Get feeds in batch
feed_names = ['temp', 'humidity', 'pressure', 'rt']
feeds = get_feeds_batch(io, feed_names)
sp(io)

# Send data
set_stage_color('send')
io.send_data(feeds['temp']['key'], tof(temperature))
io.send_data(feeds['humidity']['key'], pct(humidity))

# Cleanup
set_stage_color('complete')
time.sleep(SLEEP_DURATION_SHORT)
"""
