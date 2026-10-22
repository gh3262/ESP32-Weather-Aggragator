    def get_local_time(self, location=None, max_attempts=10):
        """
        Fetch and "set" the local time of this microcontroller to the local time at the location,
        using an internet time API.

        :param str location: Your city and country, e.g. ``"America/New_York"``.
        :param max_attempts: The maximum number of attempts to connect to WiFi before
                             failing or use None to disable. Defaults to 10.

        """
        reply = self.get_strftime(TIME_SERVICE_FORMAT, location=location, max_attempts=max_attempts)
        if reply:
            times = reply.split(" ")
            the_date = times[0]
            the_time = times[1]
            year_day = int(times[2])
            week_day = int(times[3])
            is_dst = None  # no way to know yet
            year, month, mday = (int(x) for x in the_date.split("-"))
            the_time = the_time.split(".")[0]
            hours, minutes, seconds = (int(x) for x in the_time.split(":"))
            now = time.struct_time(
                (year, month, mday, hours, minutes, seconds, week_day, year_day, is_dst)
            )

            if rtc is not None:
                rtc.RTC().datetime = now

        return reply