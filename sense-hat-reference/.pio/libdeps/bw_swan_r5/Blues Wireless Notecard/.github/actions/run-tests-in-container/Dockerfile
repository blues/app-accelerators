# Container with dependencies necessary to run unit-tests and code coverage

# Build development environment
# $ docker build . --tag note-arduino-test

# Launch development environment (mount source root as /note-arduino/)
# $ docker run --rm --volume $(pwd)/../../../:/note-arduino/ --workdir /note-arduino/ note-arduino-test

# Base Image
FROM alpine:3.14


# Testing Dependencies
RUN ["ash", "-c", "\
    apk add --no-cache \
      g++ \
      gdb \
      valgrind \
"]

RUN ["ash", "-c", "\
    apk add --no-cache --repository=http://dl-cdn.alpinelinux.org/alpine/edge/testing \
      lcov \
"]

ENTRYPOINT ["./test/run_all_tests.sh"]
