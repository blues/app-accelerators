from argparse import ArgumentParser
from pathlib import Path
import notecard
from periphery import I2C
from time import sleep
import subprocess
from datetime import datetime
import shutil
import base64
import zipfile
import shlex
import socket


def configure_i2c():
    port = I2C('/dev/i2c-1')
    return notecard.OpenI2C(port, 0, 0)


def configure_notecard(ncard, product):
    print('Configuring Notecard.')
    cmd = {'cmd': 'card.io', 'rate': 1}  # set modem speed to maximum
    ncard.Command(cmd)

    cmd = {
        'cmd': 'hub.set',
        'product': product,
        'mode': 'continuous',
        'sync': True,
        'unsecure': True
    }
    ncard.Command(cmd)


def set_time(ncard):
    print('Waiting for Notecard to acquire time-of-day')
    timeout = 10
    iterations = 0
    got_time = False
    while iterations != timeout:
        req = {'req': 'card.time'}
        rsp = ncard.Transaction(req)
        time = rsp['time']
        zone = rsp['zone'].split(',')[1]
        if zone != 'Unknown':
            got_time = True
            break

        iterations += 1
        sleep(1)

    if not got_time:
        raise Exception('Failed to set local time-of-day via card.time.')

    print('Setting local time-of-day')

    bash_cmd = f"sudo date -s '@'{time}"
    subprocess.run(bash_cmd, shell=True, check=True)

    bash_cmd = f'sudo timedatectl set-timezone {zone}'
    subprocess.run(bash_cmd, shell=True, check=True)


def download(data_dir, ncard, route, content, content_version, active_dir):
    print(f'Downloading {content}')

    download_dir = Path(data_dir, 'download')
    if download_dir.exists():
        shutil.rmtree(download_dir)
    download_dir.mkdir(parents=True)

    zip_path = Path(download_dir, 'download.zip')
    chunk_len = 8192
    offset = 0
    while True:
        req = {
            'req': 'web.get',
            'route': route,
            'name': content,
            'content': 'application/zip',
            'offset': offset,
            'max': chunk_len
        }
        rsp = ncard.Transaction(req)
        total = rsp['total']
        if total == 'null':
            print('retrying...')
        else:
            with open(zip_path, 'ab') as f:
                f.write(base64.b64decode(rsp['payload']))

            offset += chunk_len
            if offset >= total:
                break

            print(f'{offset}/{total} downloaded')

    with zipfile.ZipFile(zip_path, 'r') as z:
        z.extractall(download_dir)

    vars_dir = Path(download_dir, 'vars')
    vars_dir.mkdir(parents=True, exist_ok=True)
    vars_content_path = Path(vars_dir, 'CONTENT')
    with open(vars_content_path, 'w+') as f:
        f.write(content)
    vars_content_version_path = Path(vars_dir, 'CONTENT_VERSION')
    with open(vars_content_version_path, 'w+') as f:
        f.write(content_version)

    active_dir.mkdir(parents=True, exist_ok=True)
    shutil.copytree(download_dir, active_dir, dirs_exist_ok=True)


def should_download(env_dl_time, env_content, active_content,
                    env_content_version, active_content_version):
    download = False
    # If time matches env var kiosk_download_time, ok to download.
    if str(datetime.now().hour) == env_dl_time:
        download = True

    # If kiosk_download_time is not a number, ok to download.
    if not env_dl_time.isdigit():
        download = True

    # If the content to download isn't specified, nothing to download.
    if env_content == '':
        download = False
    else:
        # If the content to download hasn't changed from what's already active
        # (content name and version are the same), don't download.
        if active_content == env_content and \
           (env_content_version == '' or \
            active_content_version == env_content_version):
            download = False

    return download


# From https://stackoverflow.com/a/33117579.
def connected_to_internet(host="8.8.8.8", port=53, timeout=3):
    """
    Host: 8.8.8.8 (google-public-dns-a.google.com)
    OpenPort: 53/tcp
    Service: domain (DNS/TCP)
    """
    try:
        socket.setdefaulttimeout(timeout)
        socket.socket(socket.AF_INET, socket.SOCK_STREAM).connect((host, port))
        return True
    except socket.error as ex:
        print(ex)
        return False


def main(args):
    ncard = configure_i2c()
    configure_notecard(ncard, args.product)

    # Use the Notecard to get the time if we're not connected to the Internet.
    if not connected_to_internet():
        set_time(ncard)

    browser_launched = False
    current_kiosk_data = ''

    while True:
        req = {'req': 'env.get'}
        rsp_body = ncard.Transaction(req)['body']
        env_data = rsp_body['kiosk_data']
        env_content = rsp_body['kiosk_content']
        env_content_version = rsp_body['kiosk_content_version']
        env_dl_time = rsp_body['kiosk_download_time']

        active_dir = Path(args.data_dir, 'active')
        active_content_path = Path(active_dir, 'vars', 'CONTENT')
        active_content = ''
        if active_content_path.exists():
            active_content = active_content_path.read_text()

        active_content_version_path = Path(active_dir, 'vars',
                                           'CONTENT_VERSION')
        active_content_version = ''
        if active_content_version_path.exists():
            active_content_version = active_content_version_path.read_text()

        if should_download(env_dl_time, env_content, active_content,
                           env_content_version, active_content_version):
            download(args.data_dir, ncard, args.route, env_content,
                     env_content_version, active_dir)
            # Force the kiosk data to be rewritten
            current_kiosk_data = ''

        if env_data != '' and env_data != current_kiosk_data:
            print(f'Writing new data: {env_data}')
            active_data_path = Path(active_dir, 'resources', 'data.js')
            with open(active_data_path, 'w') as f:
                f.write(f'var data = {env_data}')
            current_kiosk_data = env_data

        if not browser_launched:
            browser_launched = True
            active_html_path = Path(active_dir, 'resources', 'index.htm')
            subprocess.Popen(
                shlex.split(f'chromium-browser file://{active_html_path}'))

        sleep(5)


if __name__ == '__main__':
    default_data_dir = str(Path(Path.home(), 'kiosk-data'))

    parser = ArgumentParser(description='Run the Notecard-based kiosk app.')
    parser.add_argument(
        '--data-dir',
        help=
        'Root directory for downloaded content and other files created by this script.',
        default=default_data_dir)
    parser.add_argument('--product',
                        help='ProductUID for the Notehub project.',
                        required=True)
    parser.add_argument(
        '--route',
        help=
        'Alias for a Proxy Route in Notehub that will be used to download content.',
        required=True)
    args = parser.parse_args()

    main(args)