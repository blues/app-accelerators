
const jsonata = require('jsonata');
const { readFile } = require('fs/promises');

const node_id = "2231234";
const node_name = "Area 51";
const aqi_filename = `${node_id}#aqi.qo`;
const node_names = `${node_id}:${node_name}`;

// Various type of test events
const system_event = {
  "file" : "_session.qo",
  "body": {
    "blah": "foo"
  }
};

const air_quality_event = {
  "device": "dev:1234-notused",
  "sn": "not-used",
  "file": aqi_filename,
  "body": {
    "eco2":2345
  }
};

const air_quality_alert_1 = {
  "device": "dev:1234-notused",
  "sn": "not-used",
  "file": aqi_filename,
  "body": {
    "aqi":3,
    "eco2":1234,
    "tvoc":2345,
    "alert":1
  }
}

const air_quality_alert_2 = {
  "device": "dev:1234-notused",
  "sn": "not-used",
  "file": aqi_filename,
  "body": {
    "aqi":3,
    "eco2":234,
    "tvoc":345,
    "alert":2
  }
}

const air_quality_alert_3 = {
  "device": "dev:1234-notused",
  "sn": "not-used",
  "file": aqi_filename,
  "body": {
    "aqi":1,
    "eco2":34,
    "tvoc":45,
    "alert":3
  }
}


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

  describe("air quality events", () => {
    it("are not routed", () => {
      expect(doNotRoute).toBeCalledTimes(0);
      const result = evaluateEnvironment(air_quality_event);
      expect(doNotRoute).toBeCalledTimes(1);
    });

    it("have no custom message", () => {
      const result = evaluateEnvironment(air_quality_event);
      expect(result.body.customMessage).toBe(undefined);
    });
  });

  describe("alerts", () => {
    describe("filtering", () => {
      beforeEach(() => {
        expect(doNotRoute).toBeCalledTimes(0);
      });

      describe("does not route events", () => {
        afterEach(() => {
          expect(doNotRoute).toBeCalledTimes(1);
        });

        it("without a node ID", () => {
          const result = evaluateEnvironment({alert:1, file:"#aqi.qo"});
        });

        it("without aqi.qo", () => {
          const result = evaluateEnvironment({alert:1, file:"1234#button.qo"});
        });

        it("from system notefiles", () => {
          const result = evaluateEnvironment(system_event);
        });

        describe("with NodeID#air.qo file", () => {
          it("and alert=2", () => {
            const result = evaluateEnvironment(air_quality_alert_2);
          });

          it("and no alert", () => {
            const result = evaluateEnvironment(air_quality_event);
          });
        });
      });

      describe("routes events", () => {
        afterEach(() => {
          expect(doNotRoute).toBeCalledTimes(0);
        });

        describe("with NodeID#air.qo file", () => {
          it("and alert=1", () => {
            const result = evaluateEnvironment(air_quality_alert_1);
          });

          it("and alert=3", () => {
            const result = evaluateEnvironment(air_quality_alert_3);
          });
        });
      });
    });

    describe("node name", () => {
      it("uses the environment variable node name when that is defined", () => {
        const result = evaluateEnvironment(air_quality_alert_1, { node_names  });
        expect(result.body.customMessage).toMatch(node_name);
      });

      it("uses the nodeID when the environment variable is not defined", () => {
        const result = evaluateEnvironment(air_quality_alert_1);
        expect(result.body.customMessage).toMatch(node_id);
      });
    });
  });

  describe("first alert", () => {
    let result;
    beforeEach(() => {
      result = evaluateEnvironment(air_quality_alert_1, { node_names });
      expect(doNotRoute).toBeCalledTimes(0);
    });

    it("does not mention the device serial number", () => {
      expect(result.body.customMessage).not.toMatch(air_quality_alert_1.sn);
    });

    it("does not mention the device ID", () => {
      expect(result.body.customMessage).not.toMatch(air_quality_alert_1.device);
    });

    it("includes the AQI measurement", () => {
      expect(result.body.customMessage).toMatch("AQI: "+result.body.aqi);
    });

    it("includes the eco2 measurement", () => {
      expect(result.body.customMessage).toMatch("CO2: "+result.body.eco2+"ppm");
    });

    it("includes the tvoc measurement", () => {
      expect(result.body.customMessage).toMatch("TVOC: "+result.body.tvoc+"ppb");
    });

    it("includes the phrase ALERT! Air Quality", () => {
      expect(result.body.customMessage).toMatch("ALERT! Air quality");
    });

    it("includes the node name", () => {
      expect(result.body.customMessage).toMatch(node_name);
    });

    it("makes the complete message", () => {
      expect(result.body.customMessage).toMatch("ALERT! Air quality alert in Area 51. AQI: 3, CO2: 1234ppm, TVOC: 2345ppb.");
    });
  });

  describe("standown", () => {
    let result;
    beforeEach(() => {
      result = evaluateEnvironment(air_quality_alert_3, { node_names });
      expect(doNotRoute).toBeCalledTimes(0);
    });

    it("does not mention the device serial number", () => {
      expect(result.body.customMessage).not.toMatch(air_quality_alert_1.sn);
    });

    it("does not mention the device ID", () => {
      expect(result.body.customMessage).not.toMatch(air_quality_alert_1.device);
    });

    it("includes the AQI measurement", () => {
      expect(result.body.customMessage).toMatch("AQI: "+result.body.aqi);
    });

    it("includes the eco2 measurement", () => {
      expect(result.body.customMessage).toMatch("CO2: "+result.body.eco2+"ppm");
    });

    it("includes the tvoc measurement", () => {
      expect(result.body.customMessage).toMatch("TVOC: "+result.body.tvoc+"ppb");
    });

    it("includes the phrase 'Air quality normal'", () => {
      expect(result.body.customMessage).toMatch("Air quality normal");
    });

    it("includes the node name", () => {
      expect(result.body.customMessage).toMatch(node_name);
    });

    it("makes the complete message", () => {
      expect(result.body.customMessage).toMatch("Air quality normal in Area 51. AQI: 1, CO2: 34ppm, TVOC: 45ppb.");
    });
  });
});