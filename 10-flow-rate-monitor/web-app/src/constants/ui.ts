import { ERROR_CODES } from "../services/Errors";

// Error messages when the project fails to display for some reason
const ERROR_MESSAGE = {
  FORBIDDEN:
    "User is unauthorized to access this project. Please contact the Notehub project owner to be invited to the project.",
  DEVICES_NOT_FOUND:
    "We were unable to locate any devices. Ensure your environment variables are configured correctly.",
  INTERNAL_ERROR:
    "An internal error occurred. If this problem persists, contact <a href='https://discuss.blues.com' target='_blank' rel='noreferrer'>Blues Support</a>.",
  UNAUTHORIZED:
    "Authentication failed. Please ensure you have a valid HUB_AUTH_TOKEN environment variable.",
  DEVICE_NAME_CHANGE_FAILED: "An error occurred changing the name.",
  DATABASE_NOT_RUNNING:
    "Can't reach the database server. Please make sure your database is properly connected.",
  NO_PROJECT_ID:
    "A project ID is required, but none was found. Ensure your environment variables are configured correctly for HUB_PROJECT_UID.",
  UPDATE_DEVICE_CONFIG_FAILED:
    "Failed to update the device's configuration. If this problem persists, contact Blues support.",
  UPDATE_FLEET_MONITOR_FREQUENCY_FAILED:
    "Failed to update the fleet’s flow monitoring frequency. If this problem persists, contact Blues support.",
  UPDATE_FLEET_ALARM_THRESHOLD_FAILED:
    "Failed to update the fleet’s alarm thresholds. If this problem persists, contact Blues support.",
  CLEAR_ALARMS_FAILED:
    "Failed to clear the project’s alarms. If this this problem persists, contact Blues support.",
};

const getErrorMessage = (errorCode: string) => {
  switch (errorCode) {
    case ERROR_CODES.UNAUTHORIZED:
      return ERROR_MESSAGE.UNAUTHORIZED;
    case ERROR_CODES.FORBIDDEN:
      return ERROR_MESSAGE.FORBIDDEN;
    case ERROR_CODES.INTERNAL_ERROR:
      return ERROR_MESSAGE.INTERNAL_ERROR;
    case ERROR_CODES.DATABASE_NOT_RUNNING:
      return ERROR_MESSAGE.DATABASE_NOT_RUNNING;
    case ERROR_CODES.NO_PROJECT_ID:
      return ERROR_MESSAGE.NO_PROJECT_ID;
    default:
      // eslint-disable-next-line no-console
      console.error(`Unknown error message code: ${errorCode}`);
      return ERROR_MESSAGE.INTERNAL_ERROR;
  }
};

const FLOW_RATE_STATE_ALARM_MESSAGES = {
  HIGH: "The flow rate is above the flow rate threshold for this device.",
  LOW: "The flow rate is below the flow rate threshold for this device.",
};

const getFlowRateStateAlarmMessage = (alarmReason: string) => {
  switch (alarmReason) {
    case "high":
      return FLOW_RATE_STATE_ALARM_MESSAGES.HIGH;
    case "low":
      return FLOW_RATE_STATE_ALARM_MESSAGES.LOW;
    default:
      // eslint-disable-next-line no-console
      console.error(`Unknown alarm cause: ${alarmReason}`);
      return "";
  }
};

export {
  ERROR_MESSAGE,
  getErrorMessage,
  FLOW_RATE_STATE_ALARM_MESSAGES,
  getFlowRateStateAlarmMessage,
};
