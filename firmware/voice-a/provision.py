"""Set the TiltBox's Wi-Fi and cloud settings over USB without shell-history secrets.

The device only needs the URL of your TiltBox cloud Worker and its DEVICE_TOKEN.
The OpenRouter key stays in the Worker; it is never sent to the device."""
import argparse,getpass,json,time
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('port',help='Exact USB serial device, e.g. /dev/cu.usbmodem101')
    parser.add_argument('--url',help='Worker base URL, e.g. https://tiltbox-cloud.<you>.workers.dev (or http://<mac-ip>:8787 for wrangler dev)')
    parser.add_argument('--cloud-only',action='store_true',help='Only update the Worker URL and device token; keep the stored Wi-Fi settings')
    args=parser.parse_args()
    try:import serial
    except ImportError:raise SystemExit('Install pyserial first: python3 -m pip install pyserial')
    ssid=password=''
    if not args.cloud_only:ssid=input('Wi-Fi name: ');password=getpass.getpass('Wi-Fi password: ')
    url=(args.url or input('Cloud Worker URL: ')).strip().rstrip('/');key=getpass.getpass('Device token (DEVICE_TOKEN of the Worker): ')
    if (not args.cloud_only and (not 1<=len(ssid.encode())<=32 or len(password.encode())>64)) or not 8<len(url)<190 or not url.startswith(('https://','http://')) or not 7<len(key)<190:raise SystemExit('Invalid field length or URL scheme; no data sent.')
    payload=('CLOUD '+json.dumps({'url':url,'key':key}) if args.cloud_only else 'CONFIG '+json.dumps({'ssid':ssid,'password':password,'url':url,'key':key},ensure_ascii=False))+'\n'
    if len(payload.encode())>=760:raise SystemExit('Encoded configuration exceeds the USB command limit; no data sent.')
    with serial.Serial(args.port,115200,timeout=.2) as port:
        port.dtr=False;port.rts=False;time.sleep(2);port.reset_input_buffer();port.write(payload.encode());port.flush()
        end=time.monotonic()+12
        while time.monotonic()<end:
            line=port.readline().decode(errors='replace')
            if 'Configuration saved.' in line or 'Cloud settings saved.' in line:print('Saved. Hold the button with the LEDs facing down, speak, release.');return
            if 'Invalid c' in line or 'Configuration save failed' in line:raise SystemExit('The device rejected the configuration: '+line.strip())
    raise SystemExit('No confirmation received. Check the port, firmware and USB connection. No credentials were printed.')
if __name__=='__main__':main()
