// HTTP status constants
const HTTP_STATUS = {
  METHOD_NOT_ALLOWED: "HTTP method not allowed",
  INVALID_PROJECT_UID: "Invalid Project UID",
  UNAUTHORIZED: "Unauthorized to access this project",
  INVALID_REQUEST: "The request is not valid.",
  INVALID_DEVICE: "Invalid Device UID",
  INVALID_DEVICE_NAME: "Invalid Device name",
  INVALID_VALVE_MONITOR_CONFIG: "Invalid valve monitor configuration",
  INVALID_VALVE_STATE: "Invalid valve state",
};

// HTTP headers
const HTTP_HEADER = {
  CONTENT_TYPE: "Content-Type",
  CONTENT_TYPE_JSON: "application/json",
  SESSION_TOKEN: "X-SESSION-TOKEN",
};

export { HTTP_STATUS, HTTP_HEADER };
