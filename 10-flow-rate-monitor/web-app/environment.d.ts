// Declare process.ENV types
declare global {
  namespace NodeJS {
    interface ProcessEnv {
      HUB_AUTH_TOKEN: string;
      HUB_PROJECT_UID: string;
      HUB_BASE_URL: string;
      HUB_FLEET_UID: string;
      HUB_GUI_URL: string;
    }
  }
}

// Empty export statement to treat this file as a module
export {};
