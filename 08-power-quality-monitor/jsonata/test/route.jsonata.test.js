
const jsonata = require('jsonata');
const { readFile } = require('fs/promises');

// Various type of test events
const power_monitoring_event = {
  "body": {
    "current": 0.2846,      // Line RMS current (A)
    "frequency": 59.8125,   // Line AC frequency (Hz)
    "power": 7.9,           // Line Power (Watts)
    "voltage": 118.6,       // Line RMS voltage (V)
  }
};

const overcurrent_alert = {
  "device": "dev:1234",
  "sn": "flumzel-extruder",
  "body": {
    "current": 5,           // Line RMS current (A)
    "frequency": 59.8125,   // Line AC frequency (Hz)
    "power": 600,           // Line Power (Watts)
    "voltage": 120,         // Line RMS voltage (V)
    "alert": "overcurrent,power"  // alert reasons
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
  });
});
