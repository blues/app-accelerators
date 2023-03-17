# Copyright 2022 Blues Inc.  All rights reserved.
# Use of this source code is governed by licenses granted by the
# copyright holder including that found in the LICENSE file.

# Build development environment
# docker build --tag sparrow-buildpack .

# Compile Sparrow firmware
# docker run --rm --tty --user blues --volume "$(pwd)":/host-volume/ sparrow-buildpack

# Launch development environment
# docker run --device /dev/bus/usb/ --interactive --rm --tty --user blues --volume "$(pwd)":/host-volume/ sparrow-buildpack bash

# _**NOTE:** In order to utilize DFU and debugging functionality, you must
# install (copy) the `.rules` file related to your debugging probe into the
# `/etc/udev/rules.d` directory of the host machine and restart the host._

# Global Argument(s)
ARG DEBIAN_FRONTEND="noninteractive"
ARG UID=1000
ARG USER="blues"

# POSIX Compatible (Linux/Unix) Base Image
FROM debian:stable-slim

# Import Global Argument(s)
ARG DEBIAN_FRONTEND
ARG UID
ARG USER

# Local Argument(s)
ARG STM32CUBEIDE_CHECKSUM="4b3a86b7b3946beb131419c6c8f3cc82"
ARG STM32CUBEIDE_DIRECTORY="en.st-stm32cubeide_1.11.2_14494_20230119_0724.unsigned_amd64.deb_bundle.sh"
ARG STM32CUBEIDE_DOWNLOAD_URL="https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/20/f5/df/2a/24/3a/40/49/stm32cubeide_deb/files/st-stm32cubeide_1.11.2_14494_20230119_0724.unsigned_amd64.deb_bundle.sh.zip/jcr:content/translations/en.st-stm32cubeide_1.11.2_14494_20230119_0724.unsigned_amd64.deb_bundle.sh.zip"
ARG STM32CUBEIDE_INSTALL_SCRIPT="st-stm32cubeide_1.11.2_14494_20230119_0724.unsigned_amd64.deb_bundle.sh"

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

# Establish Development Environment
RUN ["dash", "-c", "\
    apt-get update --quiet \
 && apt-get install --assume-yes --no-install-recommends --quiet \
     bzip2 \
     ca-certificates \
     cmake \
     curl \
     git \
     libglib2.0-0 \
     libncurses5 \
     libpython2.7 \
     libusb-1.0-0 \
     libwebkit2gtk-4.0-37 \
     make \
     nano \
     ssh \
     udev \
     unzip \
 && apt-get clean \
 && apt-get purge \
 && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* \
"]
WORKDIR /root

# Download/Install STM32CubeIDE (packages GNU ARM Compiler, STM32 Programmer CLI, and ST-LINK GDB Server)
RUN ["dash", "-c", "\
    curl -SLO# ${STM32CUBEIDE_DOWNLOAD_URL} \
 && md5sum ${STM32CUBEIDE_DIRECTORY}.zip \
 && echo \"${STM32CUBEIDE_CHECKSUM} ${STM32CUBEIDE_DIRECTORY}.zip\" | md5sum -c - \
 && unzip ${STM32CUBEIDE_DIRECTORY}.zip -d ${STM32CUBEIDE_DIRECTORY} \
 && chmod +x ${STM32CUBEIDE_DIRECTORY}/${STM32CUBEIDE_INSTALL_SCRIPT} \
 && yes | LICENSE_ALREADY_ACCEPTED=1 ${STM32CUBEIDE_DIRECTORY}/${STM32CUBEIDE_INSTALL_SCRIPT} \
 && rm -rf ${STM32CUBEIDE_DIRECTORY}.zip ${STM32CUBEIDE_DIRECTORY}/ \
"]
ENV PATH=/opt/st/stm32cubeide_1.11.2/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.0.500.202209151145/tools/bin:${PATH}
ENV PATH=/opt/st/stm32cubeide_1.11.2/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.10.3-2021.10.linux64_1.0.100.202210260954/tools/bin:${PATH}
ENV PATH=/opt/st/stm32cubeide_1.11.2/plugins/com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.linux64_2.0.400.202209281104/tools/bin:${PATH}
ENV LD_LIBRARY_PATH=/opt/st/stm32cubeide_1.11.2/plugins/com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.linux64_2.0.400.202209281104/tools/bin/native/linux_x64:${LD_LIBRARY_PATH}

# Set Execution Environment
WORKDIR /host-volume

# Build On Invocation (default)
CMD ["dash", "-c", "\
    rm -rf build/ \
 && mkdir build \
 && cd build/ \
 && cmake .. \
 && make -j \
"]
