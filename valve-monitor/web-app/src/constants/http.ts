// HTTP status constants
const HTTP_STATUS = {
  METHOD_NOT_ALLOWED: "HTTP method not allowed",
  INVALID_PROJECTUID: "Invalid ProjectUID",
  UNAUTHORIZED: "Unauthorized to access this project",
  INVALID_REQUEST: "The request is not valid.",
  INVALID_DEVICE: "Invalid Device UID",
  INVALID_DEVICE_NAME: "Invalid Device name",

};

// HTTP headers
const HTTP_HEADER = {
  CONTENT_TYPE: "Content-Type",
  CONTENT_TYPE_JSON: "application/json",
  SESSION_TOKEN: "X-SESSION-TOKEN",
};

export { HTTP_STATUS, HTTP_HEADER };
