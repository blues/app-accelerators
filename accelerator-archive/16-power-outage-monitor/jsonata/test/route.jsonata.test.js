
const jsonata = require('jsonata');
const { readFile } = require('fs/promises');

const device_sn = "Refrigerator";

const power_off_event = { "best_id": device_sn, "body": { "text": "USB power OFF {text here not used}" }};
const power_on_event = { "best_id": device_sn, "body": { "text": "USB power ON {text here not used}" }};
const power_lost_event = { "best_id": device_sn, "body": { "text": "boot (brown-out & hard reset" }};
const reset_event = { "best_id": device_sn, "body": { "text": "boot ([15620] [4.0] G:P)" }};

// group the events with their name an expected message for data-driven tests
const power_events = [
  [power_off_event,"power off","ALERT! Power has failed to device ", "ALERT! Power has failed to device Refrigerator."],
  [power_on_event,"power on", "Power restored to device ", "Power restored to device Refrigerator."],
  [power_lost_event,"brown-out reset", "Power restored (LiPo battery discharged) to device ", "Power restored (LiPo battery discharged) to device Refrigerator."]
];
const non_power_events = [ [ reset_event, "reset" ]];

describe("route jsonata", () => {

  let expression;
  let doNotRoute;

  /**
   * Load and parse the jsonata expression.
   */
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

  describe.each(non_power_events)("non power events", (event, name) => {
    it(name+" event is not routed", () => {
      expect(doNotRoute).toBeCalledTimes(0);
      const result = evaluateEnvironment(event);
      expect(doNotRoute).toBeCalledTimes(1);
    });

    it("have no custom message", () => {
      const result = evaluateEnvironment(reset_event);
      expect(result).toBe(undefined);
    });
  });

  describe.each(power_events)("power status event", (event, name, msg, fullMsg) => {
    describe(name, () => {
      let result;
      beforeEach(() => {
        result = evaluateEnvironment(event);
      });

      it("is routed", () => {
        expect(doNotRoute).toBeCalledTimes(0);
      });

      it("has a custom message", () => {
        expect(result).toHaveProperty("body.customMessage");
      });

      it("includes the device best_id", () => {
        expect(result.body.customMessage).toMatch(event.best_id);
      });

      it("starts with the message text", () => {
        expect(result.body.customMessage).toMatch(msg);
      });

      it("builds a complete message", () => {
        expect(result.body.customMessage).toBe(fullMsg);
      })
    });
  });
});