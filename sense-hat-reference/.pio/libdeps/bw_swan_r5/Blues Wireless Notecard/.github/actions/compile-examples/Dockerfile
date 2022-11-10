# Copyright 2022 Blues Inc.  All rights reserved.
# Use of this source code is governed by licenses granted by the
# copyright holder including that found in the LICENSE file.

# Build development environment
# docker build --file Dockerfile.arduino-cli --tag arduino-cli .

# Launch development environment
# docker run --entrypoint bash --interactive --rm --tty --volume "$(pwd)":/host-volume/ --workdir /host-volume/ arduino-cli

# Define global arguments
ARG ARDUINO_CLI_VERSION=0.23.0
ARG DEBIAN_FRONTEND="noninteractive"
ARG UID=1000
ARG USER="blues"

# POSIX compatible (Linux/Unix) base image
FROM debian:stable-slim

# Import global arguments
ARG ARDUINO_CLI_VERSION
ARG DEBIAN_FRONTEND
ARG UID
ARG USER

# Define local arguments
ARG BINDIR=/usr/local/bin
ARG ECHO_BC_FILE='$bcfile'

# Define environment variables
ENV ARDUINO_UPDATER_ENABLE_NOTIFICATION=false

# Create Non-Root User
RUN ["dash", "-c", "\
    addgroup \
     --gid ${UID} \
     \"${USER}\" \
 && adduser \
     --disabled-password \
     --gecos \"\" \
     --ingroup \"${USER}\" \
     --uid ${UID} \
     \"${USER}\" \
 && usermod \
     --append \
     --groups \"dialout,plugdev\" \
     \"${USER}\" \
"]

# Establish development environment
RUN ["dash", "-c", "\
    apt-get update --quiet \
 && apt-get install --assume-yes --no-install-recommends --quiet \
     bash \
     bash-completion \
     ca-certificates \
     curl \
     python-is-python3 \
     python3 \
     python3-pip \
     ssh \
     tree \
 && pip install \
     adafruit-nrfutil \
     pyserial \
 && apt-get clean \
 && apt-get purge \
 && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* \
"]

# Download/Install Arduino CLI
RUN ["dash", "-c", "\
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=${BINDIR} sh -s ${ARDUINO_CLI_VERSION} \
 && mkdir -p /etc/bash_completion.d/ \
 && arduino-cli completion bash > /etc/bash_completion.d/arduino-cli.sh \
 && echo >> /etc/bash.bashrc \
 && echo \"for bcfile in /etc/bash_completion.d/* ; do\" >> /etc/bash.bashrc \
 && echo \"    [ -f \\\"${ECHO_BC_FILE}\\\" ] && . \\\"${ECHO_BC_FILE}\\\"\" >> /etc/bash.bashrc \
 && echo \"done\" >> /etc/bash.bashrc \
 && echo \"if [ -f /etc/bash_completion ]; then\" >> /etc/bash.bashrc \
 && echo \"    . /etc/bash_completion\" >> /etc/bash.bashrc \
 && echo \"fi\" >> /etc/bash.bashrc \
"]

# Configure Arduino CLI
USER ${USER}
RUN ["dash", "-c", "\
    arduino-cli config init \
 && arduino-cli config add board_manager.additional_urls \
     https://raw.githubusercontent.com/stm32duino/BoardManagerFiles/main/package_stmicroelectronics_index.json \
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json \
     https://raw.githubusercontent.com/sparkfun/Arduino_Apollo3/main/package_sparkfun_apollo3_index.json \
     https://raw.githubusercontent.com/adafruit/arduino-board-index/gh-pages/package_adafruit_index.json \
     https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json \
 && arduino-cli core update-index \
 && arduino-cli core install adafruit:nrf52 \
 && arduino-cli core install adafruit:samd \
 && arduino-cli core install arduino:avr \
 && arduino-cli core install arduino:mbed_nano \
 && arduino-cli core install arduino:samd \
 && arduino-cli core install esp32:esp32 \
 && arduino-cli core install rp2040:rp2040 \
 && arduino-cli core install SparkFun:apollo3 \
 && arduino-cli core install STMicroelectronics:stm32 \
 && arduino-cli lib install \"Adafruit BME680 Library\" \
"]

# Set Execution Environment
USER root
WORKDIR /host-volume

ENTRYPOINT ["arduino-cli"]

CMD ["help"]
