import sys
import subprocess
import os

if len(sys.argv) == 1:
    print('You must provide the serial port to connect to (e.g. /dev/ttyACM0).')
    sys.exit(1)

serial_port = sys.argv[1]

# Clone note-python.
subprocess.run(['git', 'clone', '--depth=1', 'https://github.com/blues/note-python.git'])

# Set up the directories to hold the note-python files on the MCU.
subprocess.run(['python', 'pyboard.py', '-d', serial_port, '--no-soft-reset', '-f', 'mkdir', '/lib'])
subprocess.run(['python', 'pyboard.py', '-d', serial_port, '--no-soft-reset', '-f', 'mkdir', '/lib/notecard'])

# Copy the note-python files to the MCU.
src_dir = './note-python/notecard'
for filename in os.listdir(src_dir):
    if filename.endswith('.py'):
        file_path = os.path.join(src_dir, filename)
        subprocess.run(['python', 'pyboard.py', '-d', serial_port, '--no-soft-reset', '-f', 'cp', file_path, f':/lib/notecard/{filename}'])

# Verify that the files were copied to /lib/notecard.
subprocess.run(['python', 'pyboard.py', '-d', serial_port, '--no-soft-reset', '-f', 'ls', '/lib/notecard'])
