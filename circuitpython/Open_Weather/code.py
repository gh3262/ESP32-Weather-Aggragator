import time
import board
import ssl
import wifi
import socketpool
import os
import gc
import digitalio
import supervisor
import adafruit_requests
from adafruit_io.adafruit_io import IO_HTTP, AdafruitIO_RequestError
import adafruit_connection_manager
from adafruit_max7219 import bcddigits
import busio

def m(v):
    before = gc.mem_free()
    gc.collect()
    print(v, before, gc.mem_free())

def slow(v):
    flick()
    before = gc.mem_free()
    gc.collect()
    print(v, before, gc.mem_free())
    time.sleep(1)
    flick()
    time.sleep(1)
    flick()
    time.sleep(1)
    flick()


def mtom(m):
    ml = int(m * 0.00062137)
    return ml

def unix_to_local(unix_time, timezone_offset):
    # Convert the Unix timestamp to UTC time struct
    utc_time = time.localtime(unix_time)
    # Apply timezone offset (convert hours to seconds)
    local_time = time.localtime(unix_time + timezone_offset)
    # Format time as needed; for example, as "YYYY-MM-DD HH:MM"
    formatted_time = "{:04}-{:02}-{:02} {:02}:{:02}".format(
        local_time.tm_year, local_time.tm_mon, local_time.tm_mday,
        local_time.tm_hour, local_time.tm_min
        )
    return formatted_time


def ctof(c):
    f = (c * 9 / 5) + 32
    return f


def windir(d):
    if d is None:
        return "--"
    else:
        return(["North", "NE", "East", "SE", "South", "SW", "West", "NW"][int(((d + 22.5) % 360) / 45)])

def windir2(wd):
    if wd > 351:
        return "North"
    elif wd > 328:
        return "NNW"
    elif wd > 306:
        return "NW"
    elif wd > 283:
        return "WNW"
    elif wd > 261:
        return "West"
    elif wd > 238:
        return "WSW"
    elif wd > 216:
        return "SW"
    elif wd > 193:
        return "SSW"
    elif wd > 171:
        return "South"
    elif wd > 148:
        return "SSE"
    elif wd > 126:
        return "SE"
    elif wd > 103:
        return "ESE"
    elif wd > 81:
        return "East"
    elif wd > 58:
        return "ENE"
    elif wd > 36:
        return "NE"
    elif wd > 13:
        return "NNE"
    else:
        return "North"

def tomph(k):
    m = k * 2.23693629
    return m

def unix_to_time(unix_time, timezone_offset):
    # Convert the Unix timestamp to UTC time struct
    utc_time = time.localtime(unix_time)
    # Apply timezone offset (convert hours to seconds)
    local_time = time.localtime(unix_time + timezone_offset)
    # Format time as needed; for example, as "HH:MM"
    formatted_time = "{:02}:{:02}".format(
        local_time.tm_hour, local_time.tm_min
        )
    return formatted_time

def press_trend(avg, cp):
    if cp > avg:
        return "Rising"
    elif avg > cp:
        return "Falling"
    else:
        return "Steady"

def ptrend_val(pt):
    if pt == "Rising":
        return 1
    elif pt == "Falling":
        return -1
    else:
        return 0

def ao():
    r.value = False
    g.value = False
    b.value = False
    r1.value = False
    g1.value = False

def cycle(n):
    if n < 1:
        n = 1
    c = 0
    while c < n:
        c = c + 1
        r.value = True
        time.sleep(.5)
        ao()
        g.value = True
        time.sleep(.5)
        ao()
        b.value = True
        time.sleep(.5)
        ao()
        r1.value = True
        time.sleep(.5)
        ao()
        g1.value = True
        time.sleep(.5)
        ao()
        r.value = True
        b.value = True
        time.sleep(.5)
        ao()
        r.value = True
        g.value = True
        time.sleep(.5)
        ao()
        r1.value = True
        g1.value = True
        time.sleep(.5)
        ao()
        r1.value = True
        g1.value = True
        r.value = True
        g.value = True
        b.value = True
        time.sleep(.5)
        ao()

def sg():
    ao()
    g1.value = True

def sr():
    ao()
    r1.value = True

def sy():
    ao()
    r1.value = True
    g1.value = True


def flick():
    r.value = True
    time.sleep(.25)
    ao()
    g.value = True
    time.sleep(.25)
    ao()
    b.value = True
    time.sleep(.25)
    ao()
    r.value = True
    b.value = True
    g.value = True
    time.sleep(.5)
    ao()
    sy()

def er():
    c = 0
    while c < 5:
        c = c + 1
        r.value = True
        time.sleep(.1)
        ao()
        r1.value = True
        time.sleep(.1)
        ao()
        time.sleep(.1)
    time.sleep(2)
    #sr()
def rotat(ct, fl, baro, maxt, mint, ws, wg):
    print("Rotating")
    pt = 5
    leds.brightness(10)
    leds.clear_all()
    leds.show_str(0, ct)
    leds.show_str(5, fl)
    leds.show()
    time.sleep(pt)
    leds.clear_all()
    leds.show_str(2, baro)
    leds.show()
    time.sleep(pt)
    leds.clear_all()
    leds.show_str(0, maxt)
    leds.show_str(5, mint)
    leds.show()
    time.sleep(pt)
    leds.clear_all()
    leds.show_str(0, ws)
    leds.show_str(5, wg)
    leds.show()
    time.sleep(pt)
    leds.brightness(1)
    leds.clear_all()
    leds.show_str(0, ct)
    leds.show_str(5, fl)
    leds.show()

try:
    print("Starting")

    r = digitalio.DigitalInOut(board.GP9)
    r.direction = digitalio.Direction.OUTPUT

    g = digitalio.DigitalInOut(board.GP10)
    g.direction = digitalio.Direction.OUTPUT

    b = digitalio.DigitalInOut(board.GP11)
    b.direction = digitalio.Direction.OUTPUT

    r1 = digitalio.DigitalInOut(board.GP12)
    r1.direction = digitalio.Direction.OUTPUT

    g1 = digitalio.DigitalInOut(board.GP13)
    g1.direction = digitalio.Direction.OUTPUT

    spi = busio.SPI(board.GP18, board.GP19) #clk/data
    cs = digitalio.DigitalInOut(board.GP17)

    leds = bcddigits.BCDDigits(spi, cs, nDigits=8)

    leds.brightness(10)
    leds.clear_all()
    '''
    # place 8-digit number on display
    value = 0005678
    leds.show_str(0, "{:8}".format(value))
    leds.show()

    for i in range(16):
        leds.brightness(i)
        time.sleep(0.5)

    leds.brightness(3)

    leds.show_str(6, "90")  # show 90 starting at position 6
    leds.set_digit(0, 10)  # show - at position 0
    leds.set_digit(1, 12)  # show H at position 1
    leds.set_digit(2, 11)  # show E at position 2
    leds.set_digit(3, 13)  # show L at position 3
    leds.set_digit(4, 14)  # show P at position 4
    leds.set_digit(5, 10)  # show - at position 5

    leds.show()
    time.sleep(5.0)

    leds.clear_all()
    leds.brightness(5)

    # set the two dots and two 4-digit numbers
    leds.show_dot(2, 1)
    leds.show_dot(6, 1)
    leds.show_str(0, " 72.5")
    leds.show_str(4, "-10.8")

    leds.show()
    time.sleep(5.0)

    leds.brightness(10)
    leds.clear_all()
    # show a 4 character numeric string
    leds.show_str(0, "   0")
    leds.show()
    time.sleep(5.0)

    leds.clear_all()
    # show 0->8
    for digit in range(8):
        leds.set_digit(digit, digit)

    leds.show()
    time.sleep(5.0)

    leds.clear_all()
    '''
    sr()

    aio_username = os.getenv('aio_username')
    aio_key = os.getenv('aio_key')
    UNITS = "metric"
    LOCATION = os.getenv("LOCATION")
    LONGITUDE = os.getenv("LONGITUDE")
    LATITUDE = os.getenv("LATITUDE")
# Example API URL omitted; credentials belong in local settings.

    DATA_SOURCE = ("http://api.openweathermap.org/data/2.5/weather?"
                    + "lat="+LATITUDE
                    + "&lon="+LONGITUDE
                    + "&units="
                    + UNITS
                    + "&mode=json"
                    + "&appid="
                    + os.getenv("OPENWEATHER_KEY")
                    )

    print("-"*60, "\n", DATA_SOURCE, "\n", "-"*60, "\n")
    m(1)
    try:
        wifi.radio.connect(os.getenv('CIRCUITPY_WIFI_SSID'), os.getenv('CIRCUITPY_WIFI_PASSWORD'))
        print("\nConnected to WiFi")
        print(wifi.radio.ipv4_address)
        pool = socketpool.SocketPool(wifi.radio)
        requests = adafruit_requests.Session(pool, ssl.create_default_context())
        sg()
    except Exception as e:
        print("Error Connecting: ",e)
        er()
        sr()
        time.sleep(10)
        supervisor.reload()
    sy()
    x = 0
    m(2)
    while x < 1:
        print("\nFetching json from", DATA_SOURCE)
        try:
            response = requests.get(DATA_SOURCE)
        except Exception as e:
            er()
            sr()
            print("Error Fetching: ", e)
            time.sleep(5)
            print("go on")
        leds.clear_all()
        tz = response.json()["timezone"]
        current_temp = response.json()["main"]["temp"]
        try:
            max_temp = response.json()["main"]["temp_max"]
        except:
            er()
            max_temp = 0
        try:
            min_temp = response.json()["main"]["temp_min"]
        except:
            er()
            min_temp = 0
        pressure = response.json()["main"]["pressure"]
        feels_like = response.json()["main"]["feels_like"]
        humidity = response.json()["main"]["humidity"]
        baro_sea_level = response.json()["main"]["sea_level"]
        baro_ground_level = response.json()["main"]["grnd_level"]
        sr = response.json()["sys"]["sunrise"]
        ss = response.json()["sys"]["sunset"]
        cntry = response.json()["sys"]["country"]
        visibility = response.json()["visibility"]
        dt = response.json()["dt"]
        try:
           wind_gust = response.json()["wind"]["gust"]
        except:
            wind_gust = 0
            er()
            sy()
        wind_speed = response.json()["wind"]["speed"]
        wind_dir = response.json()["wind"]["deg"]
        clouds = response.json()["clouds"]["all"]
        try:
            rain = response.json()["rain"]["1h"]
        except:
            er()
            rain = 0
            sy()
        lon = response.json()["coord"]["lon"]
        lat = response.json()["coord"]["lat"]
        city = response.json()["name"]
        current_weather_condition = response.json()["weather"][0]["main"]
        current_weather_description = response.json()["weather"][0]["description"]
        current_weather_id = response.json()["weather"][0]["id"]
        icon = response.json()["weather"][0]["icon"]
        m(3)
        print(current_weather_condition)
        print(current_weather_description)
        print("Weather id/icon: ", current_weather_id, icon)
        print("Visibility: ", visibility, mtom(visibility))
        print("As Of: ", dt, unix_to_local(dt, tz))
        print(tz)
        print("Clouds: ", clouds)
        print("Pressure: ", pressure)
        print("Pressure Sea Level: ", baro_sea_level)
        leds_baro = str(int(baro_sea_level))
        print("Pressure Ground Level: ", baro_ground_level)
        print("Current Temp: ", current_temp, ctof(current_temp))
        print("Current temperature: {:.1f} °F".format(ctof(current_temp)))
        print("Feels Like: ", feels_like, ctof(feels_like))

        leds_ct = str(int((10*ctof(current_temp)))/10)
        leds_fl = str(int((10*ctof(feels_like)))/10)
        print(leds_ct, leds_fl)
        leds.show_str(0, leds_ct)
        leds.show_str(5, leds_fl)
        leds.show()



        print("Temp Max: ", max_temp, ctof(max_temp))
        leds_mxt = str(int((10*ctof(max_temp)))/10)
        print("Temp Min: ", min_temp, ctof(min_temp))
        leds_mnt = str(int((10*ctof(min_temp)))/10)
        print("Wind Direction: ", wind_dir, windir(wind_dir), windir2(wind_dir))
        print("Wind Speed: ", wind_speed, int(tomph(wind_speed)))
        leds_ws = str(int(tomph(wind_speed)))
        print("Wind Gust: ", wind_gust, int(tomph(wind_gust)))
        leds_wg = str(int(tomph(wind_gust)))
        print(int(tomph(wind_speed)), leds_ws)
        print("Rain: ", rain)
        print("Humidity: ", humidity)
        print("Sunrise: ", sr, unix_to_time(sr, tz))
        print("Sunset: ", ss, unix_to_time(ss, tz))
        print("Location: ", city + ", " + cntry)
        print("Long & Lat: ", lon, lat)
        time.sleep(2)
        x = x + 1
        m(4)
        sg()

    io = IO_HTTP(aio_username, aio_key, requests)
    m(5)
    print("\nGETTING FEEDS")
    sy()
    try:
        owMainText = io.get_feed('openweather.owmaintext')
        owPressure = io.get_feed('openweather.owpressure')
        owDescription = io.get_feed('openweather.owdescription')
        owWeatherid = io.get_feed('openweather.owweatherid')
        owIcon = io.get_feed('openweather.owicon')
        owVisibility = io.get_feed('openweather.owvisibility')
        owAsOf = io.get_feed('openweather.owasof')
        owClouds = io.get_feed('openweather.owclouds')
        owTemp = io.get_feed('openweather.owtemp')
        owFeelsLike = io.get_feed('openweather.owfeelslike')
        owMaxTemp = io.get_feed('openweather.owmaxtemp')
        owMinTemp = io.get_feed('openweather.owmintemp')
        owWindDirection = io.get_feed('openweather.owwinddirection')
        owWindSpeed = io.get_feed('openweather.owwindspeed')
        owWindGust = io.get_feed('openweather.owwindgust')
        owRain = io.get_feed('openweather.owrain')
        owHumidity = io.get_feed('openweather.owhumidity')
        owSunrise = io.get_feed('openweather.owsunrise')
        owSunset = io.get_feed('openweather.owsunset')
        owCity = io.get_feed('openweather.owcity')
        owTimeZone = io.get_feed('openweather.owtimezone')
        owPTVal = io.get_feed('openweather.owptval')
        owPressTrend = io.get_feed('openweather.owpresstrend')
    except Exception as e:
        print("Getting feeds failed: ", e)
        er()
        time.sleep(3)
        supervisor.reload()

    m(10)
    print("\nGOT")

    print("\nGETTING HISTORY")
    nbr_read = 6
    hist = io.receive_n_data(owPressure['key'], nbr_read)
    m(11)
    i = 0
    sm = 0
    m(12)
    while i < nbr_read:
        t = hist[i]['value']
        sm = sm + float(t)
        # print(t)
        i = i + 1
    avrg = sm / nbr_read
    print("\nPressure: "+str(pressure)+" Average: ", avrg, sm)
    pressure_trend = press_trend(avrg, pressure)
    print(pressure_trend)
    ptval = ptrend_val(pressure_trend)
    print(ptval)
    m(13)
    try:
        io.send_data(owMainText['key'],current_weather_condition)
        io.send_data(owDescription['key'],current_weather_description)
        io.send_data(owWeatherid['key'],current_weather_id)
    except Exception as e:
        print("Failed batch 1: ", e)
        er()
        time.sleep(10)
        sy()
    slow(30)
    try:
        io.send_data(owIcon['key'],icon)
        io.send_data(owVisibility['key'],mtom(visibility))
        io.send_data(owAsOf['key'],unix_to_local(dt, tz))
        io.send_data(owClouds['key'],clouds)
    except Exception as e:
        print("Failed batch 2: ", e)
        er()
        time.sleep(10)
        sy()
    slow(31)
    try:
        io.send_data(owPressure['key'],pressure)
        io.send_data(owTemp['key'],ctof(current_temp))
        io.send_data(owFeelsLike['key'],ctof(feels_like))
        io.send_data(owMaxTemp['key'],ctof(max_temp))
        io.send_data(owMinTemp['key'],ctof(min_temp))
    except Exception as e:
        print("Failed batch 3: ", e)
        er()
        time.sleep(10)
        sy()
    slow(32)
    try:
        io.send_data(owWindDirection['key'],windir2(wind_dir))
        io.send_data(owWindSpeed['key'],int(tomph(wind_speed)))
        io.send_data(owWindGust['key'],int(tomph(wind_gust)))
        io.send_data(owRain['key'],rain)
        io.send_data(owHumidity['key'],humidity)
    except Exception as e:
        print("Failed batch 4: ", e)
        er()
        time.sleep(10)
        sy()
    slow(33)
    try:
        io.send_data(owSunrise['key'],unix_to_time(sr, tz))
        io.send_data(owSunset['key'],unix_to_time(ss, tz))
        io.send_data(owCity['key'],city + ", " + cntry)
        io.send_data(owTimeZone['key'], tz / 3600 )
        io.send_data(owPressTrend['key'],pressure_trend)
        io.send_data(owPTVal['key'],ptval)
    except Exception as e:
        print("Failed batch 5: ", e)
        er()
        time.sleep(10)
        sy()
    slow(34)
    try:
        rt = io.get_feed('rt')
        m(35)
        io.send_data(rt['key'], "OpenWeather" )
        m(36)
    except Exception as e:
        print("Failed to get or sent RT: ", e)
        er()
        sy()

    print("\nDone Sending Data")
    sg()
    print("\nSleeping")
    i = 0
    while i < 30:
        cycle(1)
        rotat(leds_ct, leds_fl, leds_baro, leds_mxt, leds_mnt, leds_ws, leds_wg)
        time.sleep(30)
        sg()
        print(i, " --*-- ")
        cycle(1)
        i = i + 1

    print("\nRestarting")
    supervisor.reload()
except:
    time.sleep(15)
    supervisor.reload()
