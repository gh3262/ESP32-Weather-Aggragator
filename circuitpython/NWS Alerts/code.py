import board
import time
import gc
import os
import rtc
import supervisor
import wifi
import adafruit_connection_manager
import adafruit_requests
import digitalio
from adafruit_io.adafruit_io import IO_HTTP, AdafruitIO_RequestError


# Constants
SLEEP_INTERVALS = {
    'error_retry': 30,
    'startup_delay': 2,
    'restart_delay': 10,
    'clock_error_delay': 15,
    'led_no_alert': 8,
    'led_alert': 1,
    'fetch_interval': 300,   # 5 minutes
    'publish_interval': 15   # 15 seconds per alert cycle
}

LED_PINS = {
    'red': board.GP11,
    'green': board.GP12,
    'blue': board.GP13
}

FEED_NAMES = {
    'weather_alerts': 'weather-alerts',
    'headline_alert': 'headline-alert', 
    'big_alert': 'big-alert',
    'nbr_alerts': 'nbr-alerts',
    'rt': 'rt'
}

class LEDController:
    """Handle LED operations with cleaner interface"""
    def __init__(self):
        self.leds = {}
        for color, pin in LED_PINS.items():
            led = digitalio.DigitalInOut(pin)
            led.direction = digitalio.Direction.OUTPUT
            self.leds[color] = led
    
    def set_color(self, red=False, green=False, blue=False):
        """Set LED states in one call"""
        self.leds['red'].value = red
        self.leds['green'].value = green
        self.leds['blue'].value = blue
    
    def animate_no_alert(self):
        """LED animation for no alerts"""
        # Simple heartbeat to show we are alive but idle
        self.set_color(green=True)
        time.sleep(2.0)
        self.set_color(green=False)
    
    def animate_alert(self):
        """LED animation for active alerts"""
        # Flash Red/Blue to indicate urgency
        self.set_color(red=True)
        time.sleep(0.5)
        self.set_color(blue=True)
        time.sleep(0.5)
        self.set_color(red=False, blue=False)

class WeatherMonitor:
    """Main weather monitoring class"""
    def __init__(self):
        self.leds = LEDController()
        self.requests = None
        self.io = None
        self.feeds_cache = {}
        self.aio_username = os.getenv('AIO_USERNAME')
        self.aio_key = os.getenv('AIO_KEY')
        self.alerts_url = self._build_alerts_url()
        
        self.connection_failures = 0
        self.alerts = []
        self.current_alert_index = 0
        
        # State tracking to avoid redundant sends
        self.last_sent_data = {} 
        self.last_fetch_time = -SLEEP_INTERVALS['fetch_interval'] # Force immediate fetch
        self.last_publish_time = 0
        
    def _build_alerts_url(self):
        """Build the NWS alerts URL"""
        lat = os.getenv('LATITUDE')
        lon = os.getenv('LONGITUDE')
        return f"https://api.weather.gov/alerts/active?point={lat},{lon}"
    
    def _log_memory(self, checkpoint):
        """Simplified memory logging"""
        before = gc.mem_free()
        gc.collect()
        # print(f"MEM {checkpoint}: {before} -> {gc.mem_free()}")
    
    def _get_current_time(self):
        """Get formatted current time"""
        rtc_obj = rtc.RTC()
        now = rtc_obj.datetime
        hour = now.tm_hour % 12 or 12
        period = "am" if now.tm_hour < 12 else "pm"
        return f"{now.tm_mon}/{now.tm_mday}/{now.tm_year} {hour}:{now.tm_min:02d} {period}"
    
    def _get_feed_time(self):
        """Get time in feed format"""
        rtc_obj = rtc.RTC()
        now = rtc_obj.datetime
        return f"{now.tm_year}-{now.tm_mon}-{now.tm_mday}T{now.tm_hour}:{now.tm_min}"
    
    def _set_clock(self):
        """Set system clock from network time"""
        try:
            now = time.struct_time(self.io.receive_time(os.getenv("TIMEZONE")))
            rtc_obj = rtc.RTC()
            rtc_obj.datetime = time.struct_time((
                now.tm_year, now.tm_mon, now.tm_mday,
                now.tm_hour, now.tm_min, now.tm_sec, 0, -1, -1
            ))
            print(f"Clock set: {self._get_current_time()}")
        except Exception as e:
            print(f"Error setting clock: {e}")
            # Non-fatal, continue with internal clock or retry later logic could be added
    
    def _initialize_connection(self):
        """Initialize WiFi and IO connections"""
        try:
            wifi.radio.connect(
                os.getenv("CIRCUITPY_WIFI_SSID"), 
                os.getenv("CIRCUITPY_WIFI_PASSWORD")
            )
            print(f"Connected to {os.getenv('CIRCUITPY_WIFI_SSID')}")
            
            pool = adafruit_connection_manager.get_radio_socketpool(wifi.radio)
            ssl_context = adafruit_connection_manager.get_radio_ssl_context(wifi.radio)
            self.requests = adafruit_requests.Session(pool, ssl_context)
            self.io = IO_HTTP(self.aio_username, self.aio_key, self.requests)
            
        except Exception as e:
            print(f"Connection error: {e}")
            self.leds.set_color(red=True)
            time.sleep(SLEEP_INTERVALS['restart_delay'])
            supervisor.reload()

    def _get_feed_key(self, feed_name):
        """Get feed key with caching"""
        if feed_name not in self.feeds_cache:
            try:
                feed = self.io.get_feed(FEED_NAMES[feed_name])
                self.feeds_cache[feed_name] = feed['key']
            except Exception as e:
                print(f"Error getting feed {feed_name}: {e}")
                return None
        return self.feeds_cache[feed_name]

    def _send_data_safely(self, feed_name, data, metadata=None):
        """Send data with error handling and deduplication"""
        # Check deduplication
        identifier = f"{feed_name}_{str(data)}"
        if self.last_sent_data.get(feed_name) == identifier:
            # print(f"Skipping duplicate send for {feed_name}")
            return True

        feed_key = self._get_feed_key(feed_name)
        if feed_key:
            try:
                self.io.send_data(feed_key, data, metadata)
                self.last_sent_data[feed_name] = identifier
                print(f"Sent to {feed_name}: {str(data)[:30]}...")
                return True
            except Exception as e:
                print(f"Error sending to {feed_name}: {e}")
                self.connection_failures += 1
        return False
    
    def _extract_alert_data(self, alert_feature):
        """Extract all alert data in one pass"""
        props = alert_feature['properties']
        params = props.get('parameters', {})
        
        return {
            'area_desc': props.get('areaDesc', ''),
            'sent': props.get('sent', ''),
            'status': props.get('status', ''),
            'severity': props.get('severity', ''),
            'urgency': props.get('urgency', ''),
            'event': props.get('event', ''),
            'sender_name': props.get('senderName', ''),
            'headline': props.get('headline', ''),
            'description': props.get('description', ''),
            'nws_headline': params.get('NWSheadline', [''])[0] if 'NWSheadline' in params else '',
        }
    
    def _format_alert_content(self, alert_data, alert_index, total_alerts):
        """Format alert content efficiently"""
        base_info = f"From {alert_data['sender_name']} sent {alert_data['sent']}\n"
        priority_info = f">> {alert_data['severity']} * {alert_data['urgency']} <<\n"
        event_line = f"{alert_data['event']}: {alert_data['nws_headline']}\n"
        
        # Short content
        alert_header = f"** ALERT {alert_index + 1} of {total_alerts} ** "
        short_parts = [
            alert_header, base_info, priority_info, event_line,
            f"{alert_data['headline']}\n"
        ]
        short_content = ''.join(short_parts)
        
        # Big content (full description)
        big_content = short_content + f"\n{alert_data['description']}\n"
        
        # Headline content
        headline_content = f"{alert_data['nws_headline']} - {alert_data['headline']}"
        
        return big_content, short_content, headline_content

    def fetch_alerts(self):
        """Fetch alerts from NWS"""
        print(f"Fetching: {self.alerts_url}")
        self.leds.set_color(green=True)
        self._log_memory("fetch_start")
        
        try:
            response = self.requests.get(self.alerts_url)
            if response.status_code == 200:
                data = response.json()
                features = data.get('features', [])[:5] # Limit here instead
                
                # Parse alerts into list
                parsed_alerts = []
                for i, feature in enumerate(features):
                    parsed_alerts.append(self._extract_alert_data(feature))
                
                self.alerts = parsed_alerts
                print(f"Fetched {len(self.alerts)} alerts")
                
                # Update alert count immediately
                metadata = {
                    "lat": os.getenv('LATITUDE'), 
                    "lon": os.getenv('LONGITUDE'), 
                    "created_at": self._get_feed_time()
                }
                self._send_data_safely('nbr_alerts', len(self.alerts), metadata)
                
                # Update status feed
                status_msg = f"Fetched {len(self.alerts)} alerts at {self._get_current_time()}"
                self._send_data_safely('rt', status_msg)
                
            else:
                print(f"Fetch failed: {response.status_code}")
                # Don't clear old alerts on transient failure? 
                # Better to keep old ones than show nothing if network blip.
            
            response.close()
            # Reset index to show first alert next cycle
            self.current_alert_index = 0
            
        except Exception as e:
            print(f"Fetch error: {e}")
            self.leds.set_color(red=True)
            time.sleep(SLEEP_INTERVALS['led_alert']) # Short flash
            
        self._log_memory("fetch_end")
        self.leds.set_color(blue=True) 

    def publish_current_alert(self):
        """Publish the current alert in the cycle"""
        if not self.alerts:
            # Handle no alerts
            metadata = {
                "lat": os.getenv('LATITUDE'), 
                "lon": os.getenv('LONGITUDE'), 
                "created_at": self._get_feed_time()
            }
            content = "No Current Alerts"
            self._send_data_safely('weather_alerts', content, metadata)
            self._send_data_safely('headline_alert', content, metadata)
            self._send_data_safely('big_alert', content, metadata)
            print("Published: No Alerts")
            return

        # Handle active alerts
        alert_data = self.alerts[self.current_alert_index]
        big, short, headline = self._format_alert_content(
            alert_data, self.current_alert_index, len(self.alerts)
        )
        
        metadata = {
            "lat": os.getenv('LATITUDE'), 
            "lon": os.getenv('LONGITUDE'), 
            "created_at": self._get_feed_time()
        }
        
        self._send_data_safely('weather_alerts', short, metadata)
        self._send_data_safely('headline_alert', headline, metadata)
        self._send_data_safely('big_alert', big, metadata)
        
        print(f"Published Alert {self.current_alert_index + 1}/{len(self.alerts)}")
        
        # Cycle index
        self.current_alert_index = (self.current_alert_index + 1) % len(self.alerts)

    def run(self):
        """Main execution loop"""
        print("> Weather Monitor Starting (Optimized)...")
        self.leds.set_color(red=True)
        time.sleep(SLEEP_INTERVALS['startup_delay'])
        
        self._initialize_connection()
        self._set_clock()
        self.leds.set_color(blue=True)
        
        while True:
            try:
                now = time.monotonic()
                
                # Fetch Check
                if now - self.last_fetch_time > SLEEP_INTERVALS['fetch_interval']:
                    self.fetch_alerts()
                    self.last_fetch_time = now
                    # Force immediate publish after new fetch
                    self.last_publish_time = 0 
                
                # Publish Check
                if now - self.last_publish_time > SLEEP_INTERVALS['publish_interval']:
                    self.publish_current_alert()
                    self.last_publish_time = now
                
                # Animation / Idling
                if self.alerts:
                    self.leds.animate_alert()
                else:
                    self.leds.animate_no_alert()
                    time.sleep(SLEEP_INTERVALS['led_no_alert']) # Extra sleep when idle
                
                # Small sleep to prevent tight loop
                time.sleep(0.1)
                
            except Exception as e:
                print(f"Main loop error: {e}")
                self.leds.set_color(red=True)
                time.sleep(SLEEP_INTERVALS['restart_delay'])
                supervisor.reload()

# Entry point
if __name__ == "__main__":
    monitor = WeatherMonitor()
    monitor.run()
