"""Simple ESP32 probe script.
Usage:
  python test_esp32_probe.py --url http://192.168.1.50
Or set environment variable `ESP32_ALERT_URL` and run without args.

The script will call /status and /vehicle/stop (if available) and print results.
"""
import argparse
import os
import urllib.request
import urllib.error
import sys

def call_url(url, timeout=3):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            data = resp.read().decode('utf-8', errors='replace')
            return True, data
    except urllib.error.HTTPError as he:
        return False, f'HTTPError {he.code}: {he.reason}'
    except Exception as e:
        return False, str(e)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', '-u', help='Base URL of ESP32 (e.g. http://192.168.1.50)')
    args = parser.parse_args()

    base = args.url or os.environ.get('ESP32_ALERT_URL')
    if not base:
        print('No ESP32 URL provided. Set --url or ESP32_ALERT_URL environment variable.')
        sys.exit(2)

    base = base.rstrip('/')
    status_url = base + '/status'
    stop_url = base + '/vehicle/stop'

    print(f'Probing ESP32 at {base}')
    ok, data = call_url(status_url)
    if ok:
        print('[status] OK')
        print(data)
    else:
        print('[status] FAILED:', data)

    ok2, data2 = call_url(stop_url)
    if ok2:
        print('[vehicle/stop] OK')
        print(data2)
    else:
        print('[vehicle/stop] FAILED:', data2)

if __name__ == '__main__':
    main()
