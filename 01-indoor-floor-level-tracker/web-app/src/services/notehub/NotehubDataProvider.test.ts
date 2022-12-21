import {
  environmentVariablesToTrackerConfig,
  trackerConfigToEnvironmentVariables,
} from "./NotehubDataProvider";

describe("environment variable parsing", () => {
  it("should convert tracker config values to strings", () => {
    const res = trackerConfigToEnvironmentVariables({
      live: false,
      baseFloor: 7,
      floorHeight: 3.1234,
      noMovementThreshold: 6,
    });
    expect(res).toEqual({
      live: "false",
      baseline_floor: "7",
      floor_height: "3.1234",
      no_movement_threshold: "6",
    });
  });

  it("should ignore tracker config variables that are not set", () => {
    let res = trackerConfigToEnvironmentVariables({});
    expect(res).toEqual({});

    res = trackerConfigToEnvironmentVariables({ live: false });
    expect(res).toEqual({ live: "false" });
  });

  it("should convert environment variables to valid types", () => {
    const res = environmentVariablesToTrackerConfig({
      live: "false",
      baseline_floor: "3",
      floor_height: "42",
      no_movement_threshold: "9",
    });
    expect(res).toEqual({
      live: false,
      baseFloor: 3,
      floorHeight: 42,
      noMovementThreshold: 9,
    });
  });

  it("create defaults for variables that do not exist", () => {
    const res = environmentVariablesToTrackerConfig({});
    expect(res).toEqual({
      live: false,
      baseFloor: 1,
      floorHeight: 4.2672,
      noMovementThreshold: 5,
    });
  });
});
