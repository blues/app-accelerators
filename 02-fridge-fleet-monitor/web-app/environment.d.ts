// Declare process.ENV types
declare global {
  namespace NodeJS {
    interface ProcessEnv {
      HUB_AUTH_TOKEN: string;
      HUB_BASE_URL: string;
      HUB_GUI_URL: string;
      HUB_PROJECTUID: string;
      TEST_NODE_ID: string;
    }
  }
}

// Empty export statement to treat this file as a module
export {};
