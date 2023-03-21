// Declare process.ENV types
declare global {
  namespace NodeJS {
    interface ProcessEnv {
      HUB_CLIENT_ID: string;
      HUB_CLIENT_SECRET: string;
      HUB_BASE_URL: string;
      HUB_GUI_URL: string;
      HUB_PROJECT_UID: string;
      HUB_FLEET_UID: string;
    }
  }
}

// Empty export statement to treat this file as a module
export {};
