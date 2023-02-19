
const jsonata = require('jsonata');
const { readFile } = require('fs/promises');

const noMovement = {
  "body": {
      "motion": 1,
      "movements": "000000",
  }
};

const minimalMovement = {
  "body": {
      "motion": 1,
      "movements": "1111111",
  }
};

const normalMovement = {
  "body": {
      "motion": 1,
      "movements": "MNOPQZR341",
  }
};
const normalMovementValues = [22, 23, 24, 25, 26, 35, 27, 3, 4, 1];

const underMovement = {
  "body": {
      "motion": 1,
      "movements": "33333333",
  }
};

const overMovement = {
  "body": {
      "motion": 1,
      "movements": "ZWXZYZZ",
  }
};

const missingMovement = {
  "body": {
      "motion": 1,
      "orientation": "face-up",
  }
};


const startup = {
  body: {
    movements: "001155MM"
  }
};

const shutdown = {
  body: {
    movements: "MM551100"
  }
};

const vibration_vars = {
  vibration_off: "1",
  vibration_min: "10",
  vibration_max: "30"
};

/**
 * Maximum vibration expected when off.
 */
const vibration_off = "vibration_off";

/**
 * The minimum average number of vibrations per bucket that is expected when the motor is active.
 * Default is 0.
 */
const vibration_min = "vibration_min";

/**
 * The maximum average number of vibrations per bucket that is expected when the motor is active.
 */
const vibration_max = "vibration_max";


describe("route jsonata", () => {

  let expression;

  beforeAll(async () => {
    const content = await readFile('../route.jsonata', 'utf8');
    let error = null;
    try {
      expression = jsonata(content);
    }
    catch (e) {
      error = e;
    }
    // expectation failure shows the details of the error
    expect(error).toBe(null);
  });

  function evaluateEnvironment(data, bindings={}, active) {
    if (active!==undefined) {
      const gpio_state = active + ",off,off,off";
      bindings = Object.assign({}, bindings, { _aux_gpio_report: gpio_state });
    }
    // invariant: the transform preserves the original properties of the event
    const result = expression.evaluate(data, bindings);
    // expect(result).toEqual(expect.objectContaining(data));
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

  describe("the 'active' property", () => {
    function expectActive(state, active) {
      const result = evaluateEnvironment(normalMovement, {}, state)
      expect(result.body.active).toBe(active);
    }

    it("is set to true when GPIO is high", () => {
      return expectActive("high", true);
    });

    it("is set to false when GPIO is low", () => {
      return expectActive("low", false);
    });

    it("is not set when the environment variable is not present", () => {
      return expectActive(undefined, undefined);
    });
  });

  describe("vibration", () => {
    it("is not present when body.movements is missing", () => {
      expect(missingMovement.body.movements).toBe(undefined);

      const result = evaluateEnvironment(missingMovement);
      expect(result.body.vibration).toBe(undefined);
    });

    describe("is present", () => {
      let vibration;
      beforeEach(() => {
        const result = evaluateEnvironment(normalMovement);
        expect(result.body).toBeDefined();
        vibration = result.body.vibration;
        expect(vibration).toBeDefined();
      });

      it("and has the min property", () => {
        expect(vibration.min).toBe(1);
      });

      it("and has the max property", () => {
        expect(vibration.max).toBe(35);
      });

      it("and has the average property", () => {
        expect(vibration.average).toBe(19);
      });

      it("and has the values property", () => {
        expect(vibration.values).toStrictEqual(normalMovementValues);
      });
    });
  });

  /**
   * Transient vibration is when the device starts or stops during a motion track event.
   * It's characterized by a minimum of 0 and a maximum of 1.
   */
  describe("transient vibration", () => {
    
    it("on startup", () => {
      const result = evaluateEnvironment(startup);
      expect(result.body.transient).toBe(true);
      expect(result.body.reason).toBeUndefined();
    });

    it("on shutdown", () => {
      const result = evaluateEnvironment(shutdown);
      expect(result.body.transient).toBe(true);
      expect(result.body.reason).toBeUndefined();
    });

    it("on normal use is not present", () => {
      const result = evaluateEnvironment(normalMovement);
      expect(result.body.transient).toBe(false);
    });

    it("is not present with no motiion data", () => {
      const result = evaluateEnvironment(missingMovement);
      expect(result.body.transient).toBeUndefined();
    });
  });

  describe("alert reason", () => {
    describe("is not present", () => {
      it("when the environment variables are not defined", () => {
        const result = evaluateEnvironment(normalMovement, {}, "low");
        expect(result.body.reason).toBeUndefined();
      });

      it("when the active state is not defined", () => {
        const result = evaluateEnvironment(normalMovement, vibration_vars);
        expect(result.body.reason).toBeUndefined();
      });

      it("when inactive and little vibration is sensed", () => {
        const result = evaluateEnvironment(minimalMovement, vibration_vars, "low");
        expect(result.body.reason).toBeUndefined();
      });

      it("when inactive and zero vibration is sensed", () => {
        const result = evaluateEnvironment(noMovement, vibration_vars, "low");
        expect(result.body.reason).toBeUndefined();
      });

      it("when active and normal vibration is sensed", () => {
        const result = evaluateEnvironment(normalMovement, vibration_vars, "high");
        expect(result.body.reason).toBeUndefined();
      });


    });

    describe("is present", () => {
      it("when inactive and motiion is detected above the `vibration_off` value", () => {
        const result = evaluateEnvironment(normalMovement, vibration_vars, "low");
        expect(result.body.reason).toBe("inactive-over");
      });

      it("when active and minimal motiion is detected below the `vibration_min` value", () => {
        const result = evaluateEnvironment(minimalMovement, vibration_vars, "high");
        expect(result.body.reason).toBe("active-under");
      });

      it("when active and motiion is detected below the `vibration_min` value", () => {
        const result = evaluateEnvironment(underMovement, vibration_vars, "high");
        expect(result.body.reason).toBe("active-under");
      });

      it("when active and motiion is detected below the `vibration_max` value", () => {
        const result = evaluateEnvironment(overMovement, vibration_vars, "high");
        expect(result.body.reason).toBe("active-over");
      });

      it("when active and zero vibration is sensed", () => {
        const result = evaluateEnvironment(noMovement, vibration_vars, "high");
        expect(result.body.reason).toBe("active-under");
      });


    });
  });


});
