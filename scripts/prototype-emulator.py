#!/usr/bin/env python3
"""Run the production native PBW in isolated Emery QEMU with a fixture phone.

Use the Python bundled with the existing Pebble toolchain. The companion itself
runs unchanged in Node's vm; only its phone APIs use synthetic API responses.
No private-key module, existing emulator persistence, or network API is read.
"""
import argparse
import bz2
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import tempfile
import time
import uuid

from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.qemu import QemuTransport, MessageTargetQemu
from libpebble2.communication.transports.qemu.protocol import QemuButton, QemuBluetoothConnection, QemuTimeFormat
from libpebble2.protocol.system import TimeMessage, SetUTC
from libpebble2.protocol.appmessage import AppMessage, AppMessageACK, AppMessageNACK
from libpebble2.services.appmessage import AppMessageService, CString, Int32
from libpebble2.services.install import AppInstaller
from libpebble2.services.screenshot import Screenshot
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
NOW = 1792489260000  # 2026-10-20 09:41 UTC, deterministic fixture clock.
MANIFEST = json.loads((ROOT / 'package.json').read_text())
KEYS = MANIFEST['pebble']['messageKeys']
REVERSE_KEYS = {v: k for k, v in KEYS.items()}
APP_UUID = uuid.UUID(MANIFEST['pebble']['uuid'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path, default=Path.home() / 'Library/Application Support/Pebble SDK/SDKs/current')
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/prototype/screenshots')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    sdk = args.sdk_root.resolve()
    node = ROOT / '.tools/node/bin/node'
    if not node.exists():
        node = 'node'
    with tempfile.TemporaryDirectory(prefix='transit-emery-') as temp:
        temp = Path(temp)
        flash = temp / 'spi.bin'
        flash.write_bytes(bz2.decompress((sdk / 'sdk-core/pebble/emery/qemu/qemu_spi_flash.bin.bz2').read_bytes()))
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        env = dict(os.environ)
        env['DYLD_LIBRARY_PATH'] = str((ROOT / '.tools/libpng/lib').resolve())
        log = (temp / 'qemu.log').open('w')
        command = [str(sdk / 'toolchain/bin/qemu-pebble'), '-rtc', 'base=localtime', '-serial', 'null',
            '-serial', 'tcp:127.0.0.1:%d,server=on,wait=off' % port, '-serial', 'null',
            '-kernel', str(sdk / 'sdk-core/pebble/emery/qemu/qemu_micro_flash.bin'),
            '-machine', 'pebble-emery', '-cpu', 'cortex-m33',
            '-drive', 'if=mtd,format=raw,file=' + str(flash), '-display', 'none', '-audio', 'driver=none,id=audio0']
        qemu = subprocess.Popen(command, env=env, stdout=log, stderr=log)
        bridge = subprocess.Popen([str(node), str(ROOT / 'test/support/emulator-bridge.js')],
            cwd=ROOT, text=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        try:
            pebble = PebbleConnection(QemuTransport('127.0.0.1', port))
            for attempt in range(50):
                try:
                    pebble.connect()
                    break
                except Exception:
                    if qemu.poll() is not None:
                        raise RuntimeError((temp / 'qemu.log').read_text())
                    time.sleep(0.1)
            else:
                raise RuntimeError('QEMU did not open its protocol port')
            pebble.run_async()
            print('Connected to isolated Emery firmware', pebble.firmware_version, flush=True)
            pebble.transport.send_packet(QemuBluetoothConnection(connected=True), target=MessageTargetQemu())
            pebble.transport.send_packet(QemuTimeFormat(is_24_hour=False), target=MessageTargetQemu())
            pebble.send_packet(TimeMessage(message=SetUTC(unix_time=NOW // 1000, utc_offset=0, tz_name='UTC')))
            # Avoid overflowing the emulator's protocol buffer during installation.
            original_send = pebble.send_packet
            no_phone = False
            def throttled(packet):
                time.sleep(0.003)
                if no_phone and isinstance(packet, AppMessage) and isinstance(packet.data, AppMessageACK):
                    packet = AppMessage(transaction_id=packet.transaction_id, data=AppMessageNACK())
                return original_send(packet)
            pebble.send_packet = throttled
            AppInstaller(pebble, str(ROOT / 'build/pebble-transit.pbw')).install()
            print('Installed production native PBW', flush=True)
            time.sleep(1)  # Firmware starts the app after its resource transfer completes.
            service = AppMessageService(pebble)
            incoming = queue.Queue()
            service.register_handler('appmessage', lambda tid, app_uuid, payload: incoming.put(payload))
            acks = queue.Queue()
            service.register_handler('ack', lambda tid, app_uuid: acks.put(True))
            service.register_handler('nack', lambda tid, app_uuid: acks.put(False))

            def phone(input):
                bridge.stdin.write(json.dumps(input) + '\n')
                bridge.stdin.flush()
                response = json.loads(bridge.stdout.readline())
                if 'error' in response:
                    raise RuntimeError(response['error'])
                return response

            def deliver(payload):
                while not acks.empty():
                    acks.get_nowait()
                service.send_message(APP_UUID, {KEYS[k]: CString(v) if isinstance(v, str) else Int32(v)
                    for k, v in payload.items()})
                if not acks.get(timeout=5):
                    raise AssertionError('Watch rejected fixture AppMessage')

            def drain():
                latest = None
                while True:
                    try:
                        message = incoming.get(timeout=0.25)
                    except queue.Empty:
                        break
                    symbolic = {REVERSE_KEYS[k]: v for k, v in message.items()}
                    response = phone({'message': symbolic, 'advance': 700})
                    deliver(response['payload'])
                    latest = response
                return latest

            def reset(scenario='normal'):
                pebble.send_packet(TimeMessage(message=SetUTC(unix_time=NOW // 1000, utc_offset=0, tz_name='UTC')))
                # A zero-result payload resets the watch's local navigation to Nearby.
                deliver({'HOME_COUNT': 0, 'LINE_COUNT': 0, 'IS_LOADING': 1, 'DATA_STATE': 0, 'ERROR_MESSAGE': ''})
                response = phone({'reset': True, 'scenario': scenario, 'now': NOW})
                if response.get('payload'):
                    deliver(response['payload'])
                drain()
                return response

            def button(name, duration=100):
                buttons = {'up': 2, 'select': 4, 'down': 8, 'back': 1}
                pebble.transport.send_packet(QemuButton(state=buttons[name]), target=MessageTargetQemu())
                time.sleep(duration / 1000)
                pebble.transport.send_packet(QemuButton(state=0), target=MessageTargetQemu())
                return drain()

            def capture(name):
                time.sleep(0.6)  # Let the finite list transition finish.
                rows = Screenshot(pebble).grab_image()
                image = Image.frombytes('RGB', (len(rows[0]) // 3, len(rows)), b''.join(rows))
                assert image.size == (200, 228), image.size
                image.save(args.output / (name + '.png'))
                print('Captured', name, flush=True)

            reset()
            capture('01-nearby')
            before = phone({})['requests']
            for _ in range(3):
                response = button('down')
            assert response['payload']['HOME_START'] == 2
            assert len(response['requests']) == len(before)
            capture('02-scroll')
            reset()
            response = button('select')
            assert response['payload']['DETAIL_ACTIVE'] == 1
            capture('03-route-detail')
            response = button('up')
            assert response['payload']['DEPARTURE_INDEX'] == 1
            capture('04-upcoming-departure')
            response = button('select')
            assert response['payload']['HEADSIGN'] == 'East LA / Atlantic'
            capture('05-direction')
            button('select')
            response = button('down')
            assert response['payload']['STOP_COUNT'] == 5
            capture('06-stop-timeline')
            button('down'); button('down')
            capture('07-stop-selection')
            response = button('select')
            assert response['payload']['DEPARTURE_INDEX'] == 1
            button('back'); response = button('back')
            assert response['payload']['DETAIL_ACTIVE'] == 0
            response = button('select', 800)
            assert response['payload']['DIRECTION_INDEX'] == 1
            response = button('up', 800)
            assert response['payload']['IS_FAVORITE'] == 0
            reset('long'); capture('08-long-nearby'); button('select'); capture('09-long-detail')
            button('down'); capture('10-long-stops')
            reset('no-departures'); button('select'); capture('11-no-departures')
            for scenario in ['loading', 'empty', 'key', 'location', 'network']:
                reset(scenario); capture('state-' + scenario)
            reset()
            saved = phone({})['payload']
            saved['DATA_CACHED'] = 1; saved['DATA_UPDATED_AT'] -= 600
            deliver(saved); capture('state-saved')
            pebble.transport.send_packet(QemuBluetoothConnection(connected=False), target=MessageTargetQemu())
            # QEMU keeps its developer serial link alive after the Bluetooth flag
            # changes. Reject a watch request to exercise the native outbox-failure
            # path as well; do not deliver a simulated phone reply afterward.
            no_phone = True
            pebble.transport.send_packet(QemuButton(state=8), target=MessageTargetQemu())
            time.sleep(0.1)
            pebble.transport.send_packet(QemuButton(state=0), target=MessageTargetQemu())
            capture('state-disconnected')
            print('PASS: native buttons, viewport, board, direction, timeline, departure cycle, back, pin, states', flush=True)
            service.shutdown()
        finally:
            bridge.terminate(); qemu.terminate()
            bridge.wait(timeout=5); qemu.wait(timeout=5)
            log.close()


if __name__ == '__main__':
    main()
