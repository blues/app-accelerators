[![Coverage Status](https://coveralls.io/repos/github/blues/note-arduino/badge.svg?branch=master)](https://coveralls.io/github/blues/note-arduino?branch=master)

# note-arduino

The note-arduino Arduino library for communicating with the
[Blues Wireless][blues] Notecard via serial or I²C. Includes example sketches in
the [examples directory](examples).

This library allows you to control a Notecard by writing an Arduino sketch in C
or C++. Your sketch may programmatically configure Notecard and send Notes to
[Notehub.io][notehub].

This library is a wrapper around the [note-c library][note-c], which it includes
as a git submodule.

## Installation

1. Open the Arduino IDE and click **Tools > Manage Libraries...**.
2. Search for "Blues" in the input box and click the "Install" button next
   to the "Blues Wireless Notecard" result.

   ![](https://wireless.dev/images/guides/first-sensor/arduino/install-library.gif)

3. Create a new sketch and select the Sketch > Include Library > Contributed
Libraries > Blues Wireless Notecard menu option, to add the following include to
your sketch:

   ```
   #include <Notecard.h>
   ```

## Usage

```cpp
#include <Notecard.h>

// Create an instance of the Notecard class.
Notecard notecard;
```

Both configuration methods use the `begin()` method, though the parameters
differ depending on your approach.

### Serial Configuration

For Serial, pass in the Serial object and baud rate.

```cpp
notecard.begin(Serial1, 9600);
```

### I2C Configuration

For I2C, you may simply call Notecard `begin()` with no parameters.

```cpp
notecard.begin();
```

### Sending Notecard Requests

Notecard requests use bundled `J` json package to allocate a `req`, is a JSON
object for the request to which we will then add Request arguments. The
function allocates a `"req` request structure using malloc() and initializes its
"req" field with the type of request.

```cpp
J *req = notecard.newRequest("service.set");
JAddStringToObject(req, "product", "com.[mycompany].[myproduct]");
JAddStringToObject(req, "mode", "continuous");
notecard.sendRequest(req);
```

### Reading Notecard Responses

If you need to read a response from the Notecard, use the `requestAndResponse()`
method and `JGet*` helper methods to read numbers, strings, etc. from the JSON
response.

```cpp
J *rsp = notecard.requestAndResponse(notecard.newRequest("card.temp"));
if (rsp != NULL) {
   temperature = JGetNumber(rsp, "value");
   notecard.deleteResponse(rsp);
}
```

## Keeping up to date with note-c repo

This library depends on the blues [note-c repo][note-c] and utilizes
git subtrees to include those files in the src/note-c folder. To
update this repo with the latest from note-c:

```none
rm -rf src/note-c
git commit -am 'remove note-c before re-add'
git subtree add --prefix=src/note-c --squash https://github.com/blues/note-c.git master
```

## Documentation

The documentation for this library can be found
[here](https://dev.blues.io/tools-and-sdks/arduino-library/).

## Examples

The [examples](examples/) directory contains examples for using this library
with:

- [Notecard Basics](examples/Example1_NotecardBasics/Example1_NotecardBasics.ino)
- [Performing Periodic Communications](examples/Example2_PeriodicCommunications/Example2_PeriodicCommunications.ino)
- [Handling inbound requests with polling](examples/Example3_InboundPolling/Example3_InboundPolling.ino)
- [Handling inbound interrupts](examples/Example4_InboundInterrupts/Example4_InboundInterrupts.ino)
- [Using Note templates](examples/Example5_UsingTemplates/Example5_UsingTemplates.ino)
- [Sensor tutorial](examples/Example6_SensorTutorial/Example6_SensorTutorial.ino)
- [Power control](examples/Example7_PowerControl/Example7_PowerControl.ino)

Before running an example, you will need to set the Product Identifier, either in code or on your connected Notecard. Steps on how to do this can be found at [https://dev.blues.io/tools-and-sdks/samples/product-uid](https://dev.blues.io/tools-and-sdks/samples/product-uid).


## Contributing

We love issues, fixes, and pull requests from everyone. Please run the
unit-tests ([as described in the following section](#note-arduino-tests)), prior
to submitting your PR. By participating in this project, you agree to abide by
the Blues Inc [code of conduct].

For details on contributions we accept and the process for contributing, see our
[contribution guide](CONTRIBUTING.md).

## Note Arduino Tests

### Dependencies

The tests are designed to be executed using Docker, and the environment required
by the tests is defined in .github/actions/run-tests-in-container/Dockerfile.
The following directions are provided to aid in executing the tests in
a container.

> _**NOTE:** If you are uncomfortable using Docker, then you may wish to use
the Dockerfile as a guide to install the necessary dependencies and execute
`run_all_tests.sh` locally._

### Invocation

1. From the `note-arduino` folder, build the container with the
following command:

```none
docker build .github/actions/run-tests-in-container/ --tag note-arduino-test
```

1. Execute the tests inside the container using the following command:

```none
docker run --rm --volume $(pwd):/note-arduino/ --workdir /note-arduino/ note-arduino-test
```

1. Similar test results should print to your terminal for review.

### Success

Upon success, you will see a message similar to the following:

```none
...
[passed] test_noteserial_arduino_transmit_invokes_hardware_serial_flush_when_flush_parameter_is_true
[passed] test_noteserial_arduino_transmit_does_not_invoke_hardware_serial_flush_when_flush_parameter_is_false
[passed] test_noteserial_arduino_transmit_does_not_modify_hardware_serial_write_result_value_before_returning_to_caller
==13467==
==13467== HEAP SUMMARY:
==13467==     in use at exit: 0 bytes in 0 blocks
==13467==   total heap usage: 2 allocs, 2 frees, 73,728 bytes allocated
==13467==
==13467== All heap blocks were freed -- no leaks are possible
==13467==
==13467== For lists of detected and suppressed errors, rerun with: -s
==13467== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
NoteSerial_Arduino tests passed!

All tests have passed!

gcov (Alpine 10.3.1_git20210424) 10.3.1 20210424
Copyright (C) 2020 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.
There is NO warranty; not even for MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.

lcov: LCOV version 20200626-2688-g6cc6292bbc
Capturing coverage data from .
Found gcov version: 10.3.1
Using intermediate gcov format
Scanning . for .gcda files ...
Found 4 data files in .
Processing NoteLog_Arduino.gcda
Processing NoteI2c_Arduino.gcda
Processing NoteSerial_Arduino.gcda
Processing Notecard.gcda
Finished .info-file creation
Reading tracefile ./coverage/lcov.info
Summary coverage rate:
  lines......: 100.0% (215 of 215 lines)
  functions..: 92.9% (39 of 42 functions)
  branches...: no data found
```

### Failure

When a test fails, you will see a message similar to the following:

```none
...
[passed] test_notei2c_arduino_constructor_invokes_twowire_parameter_begin_method
[passed] test_notei2c_arduino_receive_requests_response_data_from_notecard
[FAILED] NoteI2c_Arduino.test.cpp:120
        twoWireBeginTransmission_Parameters.invoked == 2, EXPECTED: > 1
        twoWireWriteByte_Parameters.invoked == 4, EXPECTED: > 2
        twoWireEndTransmission_Parameters.invoked == 2, EXPECTED: > 1
[FAILED] test_notei2c_arduino_receive_will_retry_transmission_on_i2c_failure
==14566==
==14566== HEAP SUMMARY:
==14566==     in use at exit: 0 bytes in 0 blocks
==14566==   total heap usage: 7 allocs, 7 frees, 73,738 bytes allocated
==14566==
==14566== All heap blocks were freed -- no leaks are possible
==14566==
==14566== For lists of detected and suppressed errors, rerun with: -s
==14566== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
NoteI2c_Arduino tests failed!

TESTS FAILED!!!
```

Here, the failed test is the last reported test,
`test_notei2c_arduino_receive_will_retry_transmission_on_i2c_failure`. The first
line marked with `[FAILED]` indicates the file and line number related to the
failed test: file `NoteI2c_Arduino.test.cpp` near line `120`. The return value
of each test is a non-zero identifier related to the test suite.

### Adding a New Test

The signature of a test function is `int(*test_fn)(void)`, so to say, a test
function takes NO parameters and returns an integer.

To add a new test to a test suite, create your new test and add the test
to the test runner at the bottom of the file. You may find it easiest to copy
an existing test and adapt the **Arrange**, **Action**, and **Assert** sections.
Please create an expressive error message that can assist in understanding a
failed test.

To add a test to the runner, copy the test's name and use it to create an entry
in the `tests` array in the `main` function. The entry will occupy it's own line
at the end of the array, and syntax should be as follows,
`{test_name, "test_name"},`.

## More Information

For additional Notecard SDKs and Libraries, see:

* [note-c][note-c] for Standard C support
* [note-go][note-go] for Go
* [note-python][note-python] for Python

## To learn more about Blues Wireless, the Notecard and Notehub, see:

* [blues.com](https://blues.io)
* [notehub.io][Notehub]
* [wireless.dev](https://wireless.dev)

## License

Copyright (c) 2019 Blues Inc. Released under the MIT license. See
[LICENSE](LICENSE) for details.

[blues]: https://blues.com
[notehub]: https://notehub.io
[note-c]: https://github.com/blues/note-c
[note-go]: https://github.com/blues/note-go
[note-python]: https://github.com/blues/note-python
[archive]: https://github.com/blues/note-arduino/archive/master.zip
[code of conduct]: https://blues.github.io/opensource/code-of-conduct
[Notehub]: https://notehub.io
