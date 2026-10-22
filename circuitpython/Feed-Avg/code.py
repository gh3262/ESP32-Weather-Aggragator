import time
import board
import digitalio
import rtc
import ssl
import wifi
import socketpool
import os
import gc
import supervisor
from adafruit_datetime import datetime
import adafruit_requests
from adafruit_io.adafruit_io import IO_HTTP, AdafruitIO_RequestError

def set_clock():
    """Set the RTC from Adafruit IO time service with error handling."""
    try:
        print("Setting clock from Adafruit IO...")
        gc.collect()  # Clean up memory before network operation
        
        now = time.struct_time(io.receive_time(os.getenv("TIMEZONE", "America/Chicago")))
        print(f"Received time: {now}")
        
        r = rtc.RTC()
        r.datetime = time.struct_time((
            now.tm_year, now.tm_mon, now.tm_mday,
            now.tm_hour, now.tm_min, now.tm_sec, 
            0, -1, 0
        ))
        print("RTC set successfully")
        
    except Exception as e:
        print(f"Error setting RTC: {e}")
        print("Restarting in 30 seconds...")
        time.sleep(30)
        supervisor.reload()
    
def get_time():
    """Get formatted time string from RTC."""
    r = rtc.RTC()
    now = r.datetime
    
    # Convert to 12-hour format
    hour_12 = now.tm_hour % 12
    if hour_12 == 0:
        hour_12 = 12
    period = "am" if now.tm_hour < 12 else "pm"
    
    return f"{now.tm_mon}/{now.tm_mday}/{now.tm_year} {hour_12}:{now.tm_min:02d} {period}"

def debug_memory(label=""):
    """Print memory usage with optional label."""
    gc.collect()
    free_mem = gc.mem_free()
    print(f"Memory {label}: {free_mem} bytes free")
    return free_mem

def parse_iso_datetime(iso_str):
    """Parse ISO datetime string and return struct_time."""
    # Remove the 'Z' if present
    if iso_str.endswith("Z"):
        iso_str = iso_str[:-1]
    
    # Parse using datetime and convert to struct_time
    dt = datetime.fromisoformat(iso_str)
    return time.struct_time((
        dt.year, dt.month, dt.day,
        dt.hour, dt.minute, dt.second,
        -1, -1, -1
    ))

def format_central_time(utc_struct):
    """Convert UTC struct_time to Central Time formatted string."""
    # Convert to timestamp and adjust for Central Time (-6 hours)
    utc_timestamp = time.mktime(utc_struct)
    central_timestamp = utc_timestamp - (6 * 3600)  # 6 hours in seconds
    central_time = time.localtime(central_timestamp)
    
    return f"{central_time.tm_hour:02d}:{central_time.tm_min:02d}  {central_time.tm_year}-{central_time.tm_mon:02d}-{central_time.tm_mday:02d}"

def calculate_age_seconds(iso_timestamp):
    """Calculate how many seconds ago the timestamp was."""
    timestamp_struct = parse_iso_datetime(iso_timestamp)
    current_time = rtc.RTC().datetime
    
    # Convert both to seconds since epoch
    current_seconds = time.mktime(current_time)
    timestamp_seconds = time.mktime(timestamp_struct)
    
    # Return age in seconds (adjusted for timezone)
    return current_seconds - timestamp_seconds + (5 * 3600)

def blink_led(count, led_pin):
    """Blink LED a specified number of times."""
    for i in range(count):
        print(f"Blink {i + 1}")
        led_pin.value = True
        time.sleep(0.5)
        led_pin.value = False
        time.sleep(0.5)

def status_sleep(blink_count, led_pin, cycles=100):
    """Sleep with periodic status blinking."""
    for cycle in range(cycles):
        blink_led(blink_count, led_pin)
        time.sleep(2)
        print(f"Sleep cycle {cycle + 1}/{cycles}")

# Configuration constants
#TEMP_FEEDS = ['om-temp', 'currenttemperature', 'openweather.owtemp', 'wapi.temperature', 'gtemp']
#FEED_NAMES = ['OM', 'AW', 'OW', 'WA', 'GA']
TEMP_FEEDS = ['om-temp', 'currenttemperature', 'openweather.owtemp', 'wapi.temperature']
FEED_NAMES = ['OM', 'AW', 'OW', 'WA']
MAX_DATA_AGE_MINUTES = 30
SLEEP_CYCLES = 100
MAX_ITERATIONS = 200

print("STARTING Temperature Monitor")
led = digitalio.DigitalInOut(board.D2)
led.direction = digitalio.Direction.OUTPUT

try:
    debug_memory("startup")
    
    # Get environment variables
    WIFI_SSID = os.getenv('CIRCUITPY_WIFI_SSID')
    WIFI_PW = os.getenv('CIRCUITPY_WIFI_PASSWORD')
    aio_username = os.getenv('aio_username')
    aio_key = os.getenv('aio_key')
    
    debug_memory("after env vars")
    
    # Connect to WiFi
    try:
        wifi.radio.connect(WIFI_SSID, WIFI_PW)
        print(f"Connected to {WIFI_SSID}")
        print(f"IP address: {wifi.radio.ipv4_address}")
    except Exception as e:
        print(f"Failed to connect to WiFi: {e}")
        time.sleep(15)
        supervisor.reload()
    
    debug_memory("after WiFi")
    
    # Set up networking
    pool = socketpool.SocketPool(wifi.radio)
    requests = adafruit_requests.Session(pool, ssl.create_default_context())
    
    debug_memory("after network setup")
    
    # Initialize Adafruit IO
    io = IO_HTTP(aio_username, aio_key, requests)
    debug_memory("after IO init")
    
    # Set clock
    try:
        set_clock()
    except Exception as e:
        print(f"Error setting clock: {e}")
    
    debug_memory("after clock set")
    print(f"Current time: {get_time()}")
    print("-" * 60)
    
    # Get feed objects
    try:
        feeds = {}
        for feed_name in TEMP_FEEDS:
            feeds[feed_name] = io.get_feed(feed_name)
            debug_memory(f"got feed {feed_name}")
        
        feeds['feed-avg'] = io.get_feed('feed-avg')
        debug_memory("got average feed")
        
    except Exception as e:
        print(f"Couldn't get feeds: {e}")
        time.sleep(300)
        supervisor.reload()
    
    # Main monitoring loop
    max_age_seconds = MAX_DATA_AGE_MINUTES * 60

    start = time.monotonic()
    restart = start + 3600
    
    for iteration in range(MAX_ITERATIONS):
        if time.monotonic() > restart:
            supervisor.reload()
            
        temp_sum = 0
        valid_count = 0
        
        print(f"\nIteration {iteration + 1}, Time: {get_time()}")
        debug_memory("loop start")
        
        # Process each temperature feed
        for i, (feed_key, feed_name) in enumerate(zip(TEMP_FEEDS, FEED_NAMES)):
            try:
                data = io.receive_data(feeds[feed_key]["key"])
                temp_value = float(data["value"])
                age_seconds = calculate_age_seconds(data['created_at'])
                formatted_time = format_central_time(parse_iso_datetime(data['created_at']))
                
                print(f"{feed_name} temp: {temp_value}°F, Time: {formatted_time}, Age: {age_seconds}s")
                
                if age_seconds < max_age_seconds:
                    temp_sum += temp_value
                    valid_count += 1
                    
            except Exception as e:
                print(f"Failed to get {feed_name} data: {e}")
        
        # Calculate and send average if we have valid data
        if valid_count > 0:
            average_temp = temp_sum / valid_count
            print(f"\nValid data points: {valid_count}, Average: {average_temp:.1f}°F")
            
            blink_led(valid_count, led)
            debug_memory("after blinking")
            
            try:
                metadata = {"ele": valid_count}
                io.send_data(feeds['feed-avg']['key'], average_temp, metadata)
                print("Average temperature sent to Adafruit IO")
            except Exception as e:
                print(f"Could not send average: {e}")
                time.sleep(15)
                supervisor.reload()
        else:
            print("No timely data available")
            blink_led(1, led)  # Single blink for no data
        
        print("-" * 60)
        debug_memory("before sleep")
        
        # Sleep with status indication
        status_sleep(valid_count if valid_count > 0 else 1, led, SLEEP_CYCLES)
        
        print(f"Completed sleep, Time: {get_time()}")

except Exception as e:
    print(f"Main loop error: {e}")
    time.sleep(60)
    supervisor.reload()