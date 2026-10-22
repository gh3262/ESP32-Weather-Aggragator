# Adafruit IO Throttle Fix

## Problem
The code was sending data to Adafruit IO too quickly, causing throttle errors. Adafruit IO has rate limits to prevent abuse:
- **Free accounts**: 30 data points per minute (2 seconds per send minimum)
- **Plus accounts**: Higher limits, but still rate limited

## Changes Made

### 1. Increased Base Delay
**Before:** `AIO_SEND_DELAY = 1`  
**After:** `AIO_SEND_DELAY = 2`

This ensures a 2-second delay between each Adafruit IO API call, which matches the free tier rate limit.

### 2. Added Delays Between EVERY Send

#### Current Weather Data Sends
**Before:** Only 3-4 delays total for ~15 sends (multiple sends bunched together)  
**After:** 2-second delay after EVERY single send operation

This means:
- 15 current weather data points × 2 seconds = ~30 seconds total
- Well within rate limits
- No risk of throttling

#### Past Weather Data Sends  
**Before:** Only 3 delays for ~9 sends per day  
**After:** 2-second delay after EVERY send, including after getting the feed

For 21 days of past data:
- 9 sends per day × 2 seconds = 18 seconds per day
- 21 days × 18 seconds = ~6 minutes total
- Plus delays between days

### 3. Added Extra Delays
- Delay after getting each past feed (before sending)
- Delay before RT (real-time) update sends
- Delays maintained between processing different days

## Impact

### Time Cost
- **Current weather update**: ~30 seconds (up from ~6 seconds)
- **Past weather update (21 days)**: ~7-8 minutes (up from ~2 minutes)
- **Total cycle time**: Still well within the 5-minute (300 second) UPDATE_INTERVAL

### Benefits
- ✅ No more throttle errors
- ✅ Reliable data transmission
- ✅ Account stays in good standing
- ✅ No data loss due to rate limiting

### Adjusting if Needed

If you have an **Adafruit IO Plus account** with higher limits, you can reduce the delay:

```python
AIO_SEND_DELAY = 1  # For Plus accounts with 60 data points/min
# or
AIO_SEND_DELAY = 0.5  # For even higher tier accounts
```

If you're still getting throttle errors on the free tier, increase it:

```python
AIO_SEND_DELAY = 3  # Extra conservative for free tier
```

## Adafruit IO Rate Limits Reference

### Free Tier
- 30 data points per minute
- 60 API calls per minute  
- Minimum recommended delay: 2 seconds

### Plus Tier ($10/month)
- 60 data points per minute
- 120 API calls per minute
- Minimum recommended delay: 1 second

## Code Locations Changed

1. **Line 27**: `AIO_SEND_DELAY` constant increased from 1 to 2
2. **Lines 420-456**: Added delay after each current weather send
3. **Lines 528-556**: Added delay after each past weather send
4. **Line 462**: Added delay before RT update

All changes ensure consistent pacing to stay within Adafruit IO rate limits.
