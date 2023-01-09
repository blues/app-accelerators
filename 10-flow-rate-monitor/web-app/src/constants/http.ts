// HTTP status constants
const HTTP_STATUS = {
  METHOD_NOT_ALLOWED: "HTTP method not allowed",
  INVALID_PROJECT_UID: "Invalid Project UID",
  UNAUTHORIZED: "Unauthorized to access this project",
  INVALID_REQUEST: "The request is not valid.",
  INVALID_DEVICE: "Invalid Device UID",
  INVALID_DEVICE_NAME: "Invalid Device name",
  INVALID_FLOW_RATE_MONITOR_CONFIG: "Invalid flow rate monitor configuration",
};

// HTTP headers
const HTTP_HEADER = {
  CONTENT_TYPE: "Content-Type",
  CONTENT_TYPE_JSON: "application/json",
  SESSION_TOKEN: "X-SESSION-TOKEN",
};

export { HTTP_STATUS, HTTP_HEADER };
