import Config from "./Config";

const env = {
  DATABASE_URL: process.env.DATABASE_URL,
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
  get databaseUrl() {
    return requiredEnvVar("DATABASE_URL");
  },
};

describe("mandatory envvars are defined", () => {
  it("DATABASE_URL", () => {
    expect(TestConfig.databaseUrl).toBeTruthy;
  });
});

export default TestConfig;
