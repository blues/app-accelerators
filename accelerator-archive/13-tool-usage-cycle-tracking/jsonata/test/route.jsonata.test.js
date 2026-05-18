
const jsonata = require('jsonata');
const { readFile } = require('fs/promises');

// Various type of test events
const power_monitoring_event = {
  "body": {
    "current": 0.2846,      // Line RMS current (A)
    "frequency": 59.8125,   // Line AC frequency (Hz)
    "power": 7.9,           // Line Power (Watts)
    "voltage": 118.6,       // Line RMS voltage (V)
    "instance": 1,          // first instance
  }
};

const overcurrent_alert = {
  "device": "dev:1234",
  "sn": "machine-1",
  "body": {
    "current": 5,           // Line RMS current (A)
    "frequency": 59.8125,   // Line AC frequency (Hz)
    "power": 600,           // Line Power (Watts)
    "voltage": 120,         // Line RMS voltage (V)
    "alert": "overcurrent,power",  // alert reasons
    "instance": 1
  }
};

const overcurrent_alert_with_normal_vibration = {
  "device": "dev:1234",
  "sn": "machine-1",
  "body": {
    "current": 5,           // Line RMS current (A)
    "frequency": 59.8125,   // Line AC frequency (Hz)
    "power": 600,           // Line Power (Watts)
    "voltage": 120,         // Line RMS voltage (V)
    "alert": "overcurrent,power",  // alert reasons
    "instance": 1,
    "vibration_raw": 123.45,
    "vibration": "normal"
  }
};


describe("route jsonata", () => {

  let expression;
  let doNotRoute;

  beforeAll(async () => {
    const content = await readFile('../route.jsonata', 'utf8');
    let error = null;
    try {
      // parse the JSONata transform
      expression = jsonata(content);
    }
    catch (e) {
      error = e;
    }
    // expectation failure shows the details of the error
    expect(error).toBe(null);
  });

  beforeEach(async () => {
    doNotRoute = jest.fn(); // set up the mock function for each test
  });

  /**
   * Evaluate the jsonata expression with the standard environment and environment overrides.
   * @param data The JSON object to transform
   * @param {*} bindings Environment bindings
   * @returns The transformed JSON object
   */
  function evaluateEnvironment(data, bindings={}) {
    bindings.doNotRoute = doNotRoute;
    const result = expression.evaluate(data, bindings);
    return result;
  }

  it("handles $env vars", () => {
    var expression = jsonata("$$testvar");
    var result = expression.evaluate({}, { "$testvar":"42"});
    expect(result).toBe("42");
  });

  it("jsonata example test", () => {
      var data = {
          example: [
            {value: 4},
            {value: 7},
            {value: 13}
          ]
        };
        var expression = jsonata("$sum(example.value)");
        var result = expression.evaluate(data);
        expect(result).toBe(24);
  });

  describe("power monitoring events", () => {
    it("are not routed", () => {
      expect(doNotRoute).toBeCalledTimes(0);
      const result = evaluateEnvironment(power_monitoring_event);
      expect(doNotRoute).toBeCalledTimes(1);
    });

    it("have no custom message", () => {
      const result = evaluateEnvironment(power_monitoring_event);
      expect(result.body.customMessage).toBe(undefined);
    });
  });

  describe("alerts", () => {
    it('has a custom message', () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(typeof result.body.customMessage).toBe("string");
    });

    it("are routed", () => {
      expect(doNotRoute).toBeCalledTimes(0);
      const result = evaluateEnvironment(overcurrent_alert);
      expect(doNotRoute).toBeCalledTimes(0);
    });

    it("mentions power alert", () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(result.body.customMessage).toMatch("Power alert");
    });

    it("mentions the device serial number when present", () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(result.body.customMessage).toMatch(`Power alert from ${overcurrent_alert.sn}`);
    });

    it("mentions the device id when serial number is not present", () => {
      let event = Object.assign({}, overcurrent_alert);
      delete event.sn;
      const result = evaluateEnvironment(event);
      expect(result.body.customMessage).toMatch(`Power alert from ${overcurrent_alert.device}`);
    });

    it("includes the reasons for the alert", () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(result.body.customMessage).toMatch(overcurrent_alert.body.alert);
    });

    it("includes the voltage measurement", () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(result.body.customMessage).toMatch(overcurrent_alert.body.voltage + "V,");
    });

    it("includes the current measurement", () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(result.body.customMessage).toMatch(overcurrent_alert.body.current + "A,");
    });

    it("includes the voltage measurement", () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(result.body.customMessage).toMatch(overcurrent_alert.body.power + "W.");
    });

    it("includes the instance name", () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(result.body.customMessage).toMatch("tool-1: ");
    });

    describe("instance", () => {
      const instance_not_present = {
        "device": "dev:1234",
        "sn": "machine-1",
        "body": {
          "current": 5,           // Line RMS current (A)
          "frequency": 59.8125,   // Line AC frequency (Hz)
          "power": 600,           // Line Power (Watts)
          "voltage": 120,         // Line RMS voltage (V)
          "alert": "overcurrent,power"  // alert reasons
        }
      };

      const instance_1_present = {
        "device": "dev:1234",
        "sn": "machine-1",
        "body": {
          "current": 5,           // Line RMS current (A)
          "frequency": 59.8125,   // Line AC frequency (Hz)
          "power": 600,           // Line Power (Watts)
          "voltage": 120,         // Line RMS voltage (V)
          "alert": "overcurrent,power",  // alert reasons,
          "instance": 1
        }
      };

      const instance_2_present = {
        "device": "dev:1234",
        "sn": "machine-1",
        "body": {
          "current": 5,           // Line RMS current (A)
          "frequency": 59.8125,   // Line AC frequency (Hz)
          "power": 600,           // Line Power (Watts)
          "voltage": 120,         // Line RMS voltage (V)
          "alert": "overcurrent,power",  // alert reasons,
          "instance": 2
        }
      };

      const instance_3_present = {
        "device": "dev:1234",
        "sn": "machine-1",
        "body": {
          "current": 5,           // Line RMS current (A)
          "frequency": 59.8125,   // Line AC frequency (Hz)
          "power": 600,           // Line Power (Watts)
          "voltage": 120,         // Line RMS voltage (V)
          "alert": "overcurrent,power",  // alert reasons,
          "instance": 3
        }
      };

      const instance_4_present = {
        "device": "dev:1234",
        "sn": "machine-1",
        "body": {
          "current": 5,           // Line RMS current (A)
          "frequency": 59.8125,   // Line AC frequency (Hz)
          "power": 600,           // Line Power (Watts)
          "voltage": 120,         // Line RMS voltage (V)
          "alert": "overcurrent,power",  // alert reasons,
          "instance": 4
        }
      };


      it("is not present", () => {
        const result = evaluateEnvironment(instance_not_present);
        expect(result.body.customMessage).toMatch("unknown: ");
      });

      it("1", () => {
        const result = evaluateEnvironment(instance_1_present);
        expect(result.body.customMessage).toMatch("tool-1: ");
      });

      it("2", () => {
        const result = evaluateEnvironment(instance_2_present);
        expect(result.body.customMessage).toMatch("tool-2: ");
      });

      it("3", () => {
        const result = evaluateEnvironment(instance_3_present);
        expect(result.body.customMessage).toMatch("tool-3: ");
      });

      it("4", () => {
        const result = evaluateEnvironment(instance_4_present);
        expect(result.body.customMessage).toMatch("tool-4: ");
      });
    });

    describe("activity", () => {
      const instance_2_active = {
        "device": "dev:1234",
        "sn": "machine-1",
        "body": {
          "current": 5,           // Line RMS current (A)
          "frequency": 59.8125,   // Line AC frequency (Hz)
          "power": 600,           // Line Power (Watts)
          "voltage": 120,         // Line RMS voltage (V)
          "alert": "overcurrent,power",  // alert reasons,
          "instance": 2,
          "active": true,
        }
      };

      const instance_2_inactive = {
        "device": "dev:1234",
        "sn": "machine-1",
        "body": {
          "current": 5,           // Line RMS current (A)
          "frequency": 59.8125,   // Line AC frequency (Hz)
          "power": 600,           // Line Power (Watts)
          "voltage": 120,         // Line RMS voltage (V)
          "alert": "overcurrent,power",  // alert reasons,
          "instance": 2,
          "active": false,
        }
      };

      it("shows the device active when active property is true", () => {
        const result = evaluateEnvironment(instance_2_active);
        expect(result.body.customMessage).toMatch(" active: yes:");
      });

      it("shows the device inactive when active property is false", () => {
        const result = evaluateEnvironment(instance_2_inactive);
        expect(result.body.customMessage).toMatch(" active: no:");
      });

      it("doesn't mention active when the property is not present", () => {
        const result = evaluateEnvironment(overcurrent_alert);
        expect(result.body.customMessage).not.toMatch("active:");
      });

      it("makes the complete message", () => {
        const result = evaluateEnvironment(instance_2_active);
        expect(result.body.customMessage).toBe("Power alert from machine-1 tool-2 active: yes: overcurrent,power. 120V, 5A, 600W.");
      });
    });
  });

  describe("vibration", () => {
    it('does not include vibration in the message when there is no vibration alert', () => {
      const result = evaluateEnvironment(overcurrent_alert);
      expect(result.body.customMessage).not.toMatch("vibration");
    });

    it('includes the vibration state when that is present', () => {
      const result = evaluateEnvironment(overcurrent_alert_with_normal_vibration);
      expect(result.body.customMessage).not.toMatch("vib.: normal,123.45");
    });

    it('is included in the complete message', () => {
      const result = evaluateEnvironment(overcurrent_alert_with_normal_vibration);
      expect(result.body.customMessage).toBe("Power alert from machine-1 tool-1: overcurrent,power. 120V, 5A, 600W. vib.: normal");
    });
  });
});

