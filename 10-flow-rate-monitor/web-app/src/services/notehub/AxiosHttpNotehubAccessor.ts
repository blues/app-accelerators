import axios from "axios";
import { NotehubAccessor } from "./NotehubAccessor";
import { HTTP_HEADER } from "../../constants/http";
import { getError, ERROR_CODES } from "../Errors";
import NotehubEnvVars from "./models/NotehubEnvVars";
import NotehubFleets from "./models/NotehubFleets";
import NotehubEnvVarsResponse from "./models/NotehubEnvVarsResponse";

// this class directly interacts with Notehub via HTTP calls
export default class AxiosHttpNotehubAccessor implements NotehubAccessor {
  hubBaseURL: string;

  hubProjectUID: string;

  commonHeaders;

  constructor(hubBaseURL: string, hubProjectUID: string, hubAuthToken: string) {
    this.hubBaseURL = hubBaseURL;
    this.hubProjectUID = hubProjectUID;
    this.commonHeaders = {
      [HTTP_HEADER.CONTENT_TYPE]: HTTP_HEADER.CONTENT_TYPE_JSON,
      [HTTP_HEADER.SESSION_TOKEN]: hubAuthToken,
    };
  }

  async getFleetsByDevice(hubDeviceUID: string) {
    const endpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/devices/${hubDeviceUID}/fleets`;
    try {
      const resp = await axios.get<NotehubFleets>(endpoint, {
        headers: this.commonHeaders,
      });
      return resp.data;
    } catch (e) {
      throw this.errorWithCode(e);
    }
  }

  async getDeviceEnvVars(hubDeviceUID: string) {
    const endpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/devices/${hubDeviceUID}/environment_variables`;
    try {
      const resp = await axios.get<NotehubEnvVars>(endpoint, {
        headers: this.commonHeaders,
      });
      return resp.data;
    } catch (e) {
      throw this.errorWithCode(e);
    }
  }

  async getEnvironmentVariablesByFleet(fleetUID: string) {
    const endpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/fleets/${fleetUID}/environment_variables`;
    try {
      const resp = await axios.get(endpoint, { headers: this.commonHeaders });
      return resp.data as NotehubEnvVarsResponse;
    } catch (e) {
      throw this.errorWithCode(e);
    }
  }

  async setEnvironmentVariablesByFleet(
    fleetUID: string,
    envVars: NotehubEnvVars
  ) {
    const endpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/fleets/${fleetUID}/environment_variables`;
    try {
      await axios.put(
        endpoint,
        { environment_variables: envVars },
        { headers: this.commonHeaders }
      );
    } catch (e) {
      throw this.errorWithCode(e);
    }
    return true;
  }

  // eslint-disable-next-line class-methods-use-this
  httpErrorToErrorCode(e: unknown): ERROR_CODES {
    let errorCode = ERROR_CODES.INTERNAL_ERROR;
    if (axios.isAxiosError(e)) {
      if (e.response?.status === 401) {
        errorCode = ERROR_CODES.UNAUTHORIZED;
      }
      if (e.response?.status === 403) {
        errorCode = ERROR_CODES.FORBIDDEN;
      }
      if (e.response?.status === 404) {
        errorCode = ERROR_CODES.DEVICE_NOT_FOUND;
      }
    }
    return errorCode;
  }

  errorWithCode(e: unknown): Error {
    const errorCode = this.httpErrorToErrorCode(e);
    return getError(errorCode, { cause: e as Error });
  }

  async setEnvironmentVariablesByDevice(
    hubDeviceUID: string,
    envVars: NotehubEnvVars
  ) {
    const endpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/devices/${hubDeviceUID}/environment_variables`;
    try {
      await axios.put(
        endpoint,
        { environment_variables: envVars },
        { headers: this.commonHeaders }
      );
    } catch (e) {
      throw this.errorWithCode(e);
    }
    return true;
  }
}
