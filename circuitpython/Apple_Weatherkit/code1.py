import board
import time
import os
import ssl
import wifi
import socketpool
import adafruit_requests
import gc
import digitalio
import neopixel
from adafruit_io.adafruit_io import IO_HTTP, AdafruitIO_RequestError
from jpcolors import Color

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
    print("")

def set_td():
    current_time = io.receive_time(timezone=tz)
    return(str(current_time.tm_year) + "-" + str(current_time.tm_mon) + "-" + str(current_time.tm_mday))

def set_td14():
    current_time = io.receive_time(timezone=tz)
    timestamp = time.mktime(current_time) + (24 * 14 * 60 * 60)
    ft = time.localtime(timestamp)
    ot = (str(ft.tm_year) + "-" + str(ft.tm_mon) + "-" + str(ft.tm_mday))
    return(ot)

def windir(wd):
    if wd > 351:
        return "N"
    elif wd > 328:
        return "NNW"
    elif wd > 306:
        return "NW"
    elif wd > 283:
        return "WNW"
    elif wd > 261:
        return "W"
    elif wd > 238:
        return "WSW"
    elif wd > 216:
        return "SW"
    elif wd > 193:
        return "SSW"
    elif wd > 171:
        return "S"
    elif wd > 148:
        return "SSE"
    elif wd > 126:
        return "SE"
    elif wd > 103:
        return "ESE"
    elif wd > 81:
        return "E"
    elif wd > 58:
        return "ENE"
    elif wd > 36:
        return "NE"
    elif wd > 13:
        return "NNE"
    else:
        return "N"
    

np = neopixel.NeoPixel(board.NEOPIXEL, 1)
np[0] = Color.red

pool = socketpool.SocketPool(wifi.radio)
https = ssl.create_default_context()
#print(f"Connecting to {os.getenv('CIRCUITPY_WIFI_SSID')}")
wifi.radio.connect(
    os.getenv("CIRCUITPY_WIFI_SSID"), os.getenv("CIRCUITPY_WIFI_PASSWORD")
)
print(f"Connected to {os.getenv('CIRCUITPY_WIFI_SSID')}")
np[0] = Color.green
#print("My MAC addr:", [hex(i) for i in wifi.radio.mac_address])
#print("My IP address is", wifi.radio.ipv4_address)
# mem()


#2024-12-05/2024-12-07

aio_username = os.getenv('aio_username')
aio_key = os.getenv('aio_key')
tz = os.getenv('TIMEZONE')
vck = os.getenv('VISUAL_CROSSING_KEY')
requests = adafruit_requests.Session(pool, ssl.create_default_context())

io = IO_HTTP(aio_username, aio_key, requests)

jwu_a = "https://weather.visualcrossing.com/VisualCrossingWebServices/rest/services/timeline/35.154985%2C%20-89.725161/"
jwu_c = "?unitGroup=us&key="
jwu_d = "&contentType=json"
jwu_e = "?unitGroup=us&include=days%2Calerts%2Ccurrent&key="
JSON_WEATHER_URL_TODAY = jwu_a + set_td() + jwu_c + vck + jwu_d
JSON_WEATHER_URL_14 = jwu_a + set_td() + "/" + set_td14() + jwu_c + vck + jwu_d
JSON_WEATHER_URL_14_DAYS = jwu_a + set_td() + "/" + set_td14() + jwu_e + vck + jwu_d
#print("")
#print(JSON_WEATHER_URL_TODAY)
#print("")
#print(JSON_WEATHER_URL_14)
#print("")
#print(JSON_WEATHER_URL_14_DAYS)
x = 0
while True:
    np[0] = Color.yellow
    gc.collect()
    #mem()
    response = requests.get(JSON_WEATHER_URL_14)
    #print(response.json()['days'][0]['temp'])
    dys = -1
    hrs = 0
    np[0] = Color.white
    print("AS OF: ", response.json()['currentConditions']['datetime'])
    time.sleep(10)
    np[0] = Color.cyan
    while dys < 14:
        dys = dys + 1
        if np[0] == Color.cyan:
            np[0] = Color.violet
        else:
            np[0] = Color.cyan
        print(response.json()['days'][dys]['datetime'], response.json()['days'][dys]['description'])
        while hrs < 24:
            hHour = response.json()['days'][dys]['hours'][hrs]['datetime']
            hTemp = response.json()['days'][dys]['hours'][hrs]['temp']
            hFeels = response.json()['days'][dys]['hours'][hrs]['feelslike']
            hHumid = response.json()['days'][dys]['hours'][hrs]['humidity']
            hDew = response.json()['days'][dys]['hours'][hrs]['dew']
            hPrec = response.json()['days'][dys]['hours'][hrs]['precip']
            hPreProb = response.json()['days'][dys]['hours'][hrs]['precipprob']
            hPreType = response.json()['days'][dys]['hours'][hrs]['preciptype']
            hWindG = response.json()['days'][dys]['hours'][hrs]['windgust']
            hWindS = response.json()['days'][dys]['hours'][hrs]['windspeed']
            hWindD = response.json()['days'][dys]['hours'][hrs]['winddir']
            hWindDtxt = windir(hWindD)
            hPressure = response.json()['days'][dys]['hours'][hrs]['pressure']
            hCloudC = response.json()['days'][dys]['hours'][hrs]['cloudcover']
            hSev = response.json()['days'][dys]['hours'][hrs]['severerisk']
            hCond = response.json()['days'][dys]['hours'][hrs]['conditions']
            l1 = "At " + hHour + ": " + str(hTemp) + "F (feels like: " + str(hFeels) + ") dewpt: " + str(hDew) + " and " +str(hHumid) + "% Humidity."
            l2 = " - Winds blowing " + str(hWindS) + "mph gusting to " + str(hWindG) + " from " + str(hWindD) + " (" + hWindDtxt + ")"
            l3 = " - " + hCond + " with clouds " + str(hCloudC) + "pct. Barometer: " + str(hPressure)
            #l4 = "  - Precipitation: " + hPreType + " with rate of " #+ str(hPrec) + " probability " + str(hPreProb)

            
            
            print(l1)
            print(l2)
            print(l3)
            #print(l4)

            print("")
  
            
            
            
            #print(response.json()['days'][dys]['hours'][hrs]['datetime'], response.json()['days'][dys]['hours'][hrs]['temp'],response.json()['days'][dys]['hours'][hrs]['conditions'])
            #print("  ->  Precip:",  str(response.json()['days'][dys]['hours'][hrs]['precip']), "Wind:", str(response.json()['days'][dys]['hours'][hrs]['windspeed']), "Pressure:", str(response.json()['days'][dys]['hours'][hrs]['pressure']))
            hrs = hrs + 1
            time.sleep(.5)
        print("*"*20)
        print("")
        hrs = 0
        time.sleep(10)

    #mem()
    np[0] = Color.blue
    time.sleep(3600)
    print(x)
    np[0] = Color.purple

np[0] = Color.amber
time.sleep(3600)

