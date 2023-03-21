// HTTP status constants
const HTTP_STATUS = {
  METHOD_NOT_ALLOWED: "HTTP method not allowed",
  INVALID_PROJECTUID: "Invalid ProjectUID",
  UNAUTHORIZED: "Unauthorized to access this project",
  INVALID_REQUEST: "The request is not valid.",
  INVALID_DEVICE: "Invalid Device UID",
  INVALID_DEVICE_NAME: "Invalid Device name",
  INVALID_TRACKER_CONFIG: "Invalid tracker configuration",
};

// HTTP headers
const HTTP_AUTH = {
  GRANT_TYPE: "client_credentials",
};

export { HTTP_STATUS, HTTP_AUTH };
