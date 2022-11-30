const env = {
  HUB_PROJECT_UID: process.env.HUB_PROJECT_UID,
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
    return requiredEnvVar("HUB_PROJECT_UID");
  },
};

describe("mandatory envvars are defined", () => {
  it("HUB_PROJECT_UID", () => {
    expect(TestConfig.hubProjectUID).toBeTruthy;
  });
});

export default TestConfig;
