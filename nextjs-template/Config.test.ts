const env = {
  HUB_PROJECTUID: process.env.HUB_PROJECTUID,
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
  get hubProjectUID() {
    return requiredEnvVar("HUB_PROJECTUID");
  },
};

describe("mandatory envvars are defined", () => {
  it("HUB_PROJECTUID", () => {
    expect(TestConfig.hubProjectUID).toBeTruthy;
  });
});

export default TestConfig;
