const debugLog = console.log; // eslint-disable-line no-console

/*
  Note: In order to keep server-only secrets safe, Next.js replaces process.env.foo with the correct
  value at build time. This means that process.env is not a standard JavaScript object, so you’re
  not able to use object destructuring. Environment variables must be referenced as e.g.
  process.env.NEXT_PUBLIC_PUBLISHABLE_KEY, not const { NEXT_PUBLIC_PUBLISHABLE_KEY } = process.env.
*/
const env = {
  DEBUG_CONFIG: process.env.DEBUG_CONFIG,
  HUB_AUTH_TOKEN: process.env.HUB_AUTH_TOKEN,
  HUB_BASE_URL: process.env.HUB_BASE_URL,
  HUB_GUI_URL: process.env.HUB_GUI_URL,
  HUB_PROJECTUID: process.env.HUB_PROJECTUID,
  HUB_FLEET_UID: process.env.HUB_FLEET_UID,
  NEXT_PUBLIC_BUILD_VERSION: process.env.NEXT_PUBLIC_BUILD_VERSION,
  NEXT_PUBLIC_COMPANY_NAME: process.env.NEXT_PUBLIC_COMPANY_NAME,
  POSTGRES_USERNAME: process.env.POSTGRES_USERNAME,
  POSTGRES_PASSWORD: process.env.POSTGRES_PASSWORD,
  POSTGRES_PORT: process.env.POSTGRES_PORT,
  POSTGRES_HOST: process.env.POSTGRES_HOST,
  POSTGRES_DATABASE: process.env.POSTGRES_DATABASE,
  READ_ONLY: process.env.READ_ONLY,
  NOTEHUB_PROVIDER: process.env.NOTEHUB_PROVIDER,
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
  if (!val) {
    throw new Error(
      `${varName} is not set in the environment. See .env.example for help.`
    );
  }
  return val;
};

const Config = {
  isBuildVersionSet() {
    return !!optionalEnvVar("NEXT_PUBLIC_BUILD_VERSION", "");
  },

  // These are getters so undefined required variables do not throw errors at build time.
  get buildVersion() {
    return optionalEnvVar("NEXT_PUBLIC_BUILD_VERSION", "ver n/a");
  },
  get companyName() {
    return optionalEnvVar("NEXT_PUBLIC_COMPANY_NAME", "Nada Company");
  },
  get debugConfig() {
    return Boolean(optionalEnvVar("DEBUG_CONFIG", ""));
  },
  get hubProjectUID() {
    return requiredEnvVar("HUB_PROJECTUID");
  },
  get hubFleetUID() {
    return requiredEnvVar("HUB_FLEET_UID");
  },
  get hubAuthToken() {
    return requiredEnvVar("HUB_AUTH_TOKEN");
  },
  get hubBaseURL() {
    return optionalEnvVar("HUB_BASE_URL", "https://api.notefile.net");
  },
  get hubGuiURL() {
    return optionalEnvVar("HUB_GUI_URL", "https://notehub.io");
  },
  get databaseURL() {
    const getVar = this.notehubProvider ? optionalEnvVar : requiredEnvVar;
    const postgresUsername = getVar("POSTGRES_USERNAME", "");
    const postgresPassword = getVar("POSTGRES_PASSWORD", "");
    const postgresHost = getVar("POSTGRES_HOST", "");
    const postgresPort = getVar("POSTGRES_PORT", "");
    const postgresDatabase = getVar("POSTGRES_DATABASE", "");
    return `postgres://${postgresUsername}:${postgresPassword}@${postgresHost}:${postgresPort}/${postgresDatabase}`;
  },
  get readOnly() {
    return Boolean(optionalEnvVar("READ_ONLY", ""));
  },
  get notehubProvider() {
    return Boolean(optionalEnvVar("NOTEHUB_PROVIDER", ""));
  },
};

const toString = (c: typeof Config | typeof env) => {
  const indent = 2;
  return JSON.stringify(c, undefined, indent);
};

if (Config.debugConfig) {
  try {
    debugLog(`Environment: ${toString(env)}`);
    debugLog(`Derived config: ${toString(Config)}`);
  } catch (error) {
    debugLog(error);
    debugLog(
      `Program isn't configured fully and likely won't work until it is.`
    );
  }
}

export default Config;
