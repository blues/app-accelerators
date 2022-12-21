import Config from "../../config";

const env = {
  TEST_NODE_ID: process.env.TEST_NODE_ID,
};

const optionalEnvVar = (varName: keyof typeof env, defaultValue: string) => {
  const val = env[varName];
  if (val === undefined) {
    return defaultValue;
  }
  return val;
};

const requiredEnvVar = (varName: keyof typeof env) => {
  const val = env[varName];
  if (val === undefined) {
    throw new Error(
      `${varName} is not set in the environment. See .env.example for help.`
    );
  }
  return val;
};

const TestConfig = {
  get testNodeId() {
    return requiredEnvVar("TEST_NODE_ID");
  },
};

describe("mandatory envvars are defined", () => {
  it("TEST_NODE_ID", () => {
    expect(TestConfig.testNodeId).toBeTruthy;
  });
});

export default TestConfig;
