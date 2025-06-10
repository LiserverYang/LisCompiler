# Copyright 2025, LiserverYang. All rights reserved.

def FormatDuration(elapsed_time):
    total_milliseconds = round(elapsed_time * 1000)

    milliseconds = total_milliseconds % 1000
    total_seconds = total_milliseconds // 1000

    days = total_seconds // (24 * 3600)
    remaining = total_seconds % (24 * 3600)
    hours = remaining // 3600
    remaining %= 3600
    minutes = remaining // 60
    seconds = remaining % 60

    time_units = [
        ('d', days),
        ('h', hours),
        ('m', minutes),
        ('s', seconds),
        ('ms', milliseconds)
    ]
    
    parts = [f"{value}{unit}" for unit, value in time_units if value != 0]

    if not parts:
        return "0ms"
    return ''.join(parts)
