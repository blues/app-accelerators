import axios, { AxiosResponse } from "axios";
import { NotehubAccessor } from "./NotehubAccessor";
import NotehubDevice from "./models/NotehubDevice";
import { HTTP_HEADER } from "../../constants/http";
import { getError, ERROR_CODES } from "../Errors";
import NotehubLatestEvents from "./models/NotehubLatestEvents";
import NotehubResponse from "./models/NotehubResponse";
import NotehubEnvVars from "./models/NotehubEnvVars";
import { serverLogInfo } from "../../pages/api/log";
import NotehubEnvVarsResponse from "./models/NotehubEnvVarsResponse";
import NotehubRoutedEvent from "./models/NotehubRoutedEvent";

// this class directly interacts with Notehub via HTTP calls
export default class AxiosHttpNotehubAccessor implements NotehubAccessor {
  hubBaseURL: string;

  hubProjectUID: string;

  hubFleetUID: string;

  commonHeaders;

  constructor(
    hubBaseURL: string,
    hubProjectUID: string,
    hubAuthToken: string,
    hubFleetUID: string
  ) {
    this.hubBaseURL = hubBaseURL;
    this.hubProjectUID = hubProjectUID;
    this.hubFleetUID = hubFleetUID;
    this.commonHeaders = {
      [HTTP_HEADER.CONTENT_TYPE]: HTTP_HEADER.CONTENT_TYPE_JSON,
      [HTTP_HEADER.SESSION_TOKEN]: hubAuthToken,
    };
  }

  async getDevicesByFleet() {
    const endpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/fleets/${this.hubFleetUID}/devices`;
    const resp = await axios.get<{
      devices: NotehubDevice[];
      has_more: boolean;
    }>(endpoint, { headers: this.commonHeaders });
    if (resp.data.has_more)
      throw new Error(
        `Response from ${endpoint} says has_more=${resp.data.has_more} but this function getDevicesByFleet() doesn't support fetching more yet.`
      );
    return resp.data.devices;
  }

  async getDevices() {
    const endpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/devices`;
    const resp = await axios.get<{
      devices: NotehubDevice[];
      has_more: boolean;
    }>(endpoint, { headers: this.commonHeaders });
    if (resp.data.has_more)
      throw new Error(
        `Response from ${endpoint} says has_more=${resp.data.has_more} but this function getDevices() doesn't support fetching more yet.`
      );
    return resp.data.devices;
  }

  async getDevice(hubDeviceUID: string) {
    const endpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/devices/${hubDeviceUID}`;
    try {
      const resp = await axios.get<NotehubDevice>(endpoint, {
        headers: this.commonHeaders,
      });
      return resp.data;
    } catch (e) {
      throw this.errorWithCode(e);
    }
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

  async getEvents(startDate?: string) {
    // Take the start date from the argument first, but fall back to the environment
    // variable.
    let events: NotehubRoutedEvent[] = [];
    const startDateQuery = startDate ? `?startDate=${startDate}` : "";
    const initialEndpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/events${startDateQuery}`;
    try {
      const resp: AxiosResponse<NotehubResponse> = await axios.get(
        initialEndpoint,
        { headers: this.commonHeaders }
      );
      if (resp.data.events) {
        events = resp.data.events;
      }
      while (resp.data.has_more && resp.data.through) {
        const recurringEndpoint = `${this.hubBaseURL}/v1/projects/${this.hubProjectUID}/events?since=${resp.data.through}`;
        const recurringResponse: AxiosResponse<NotehubResponse> =
          // eslint-disable-next-line no-await-in-loop
          await axios.get(recurringEndpoint, { headers: this.commonHeaders });
        if (recurringResponse.data.events) {
          events = [...events, ...recurringResponse.data.events];
        }
        if (recurringResponse.data.has_more) {
          serverLogInfo(
            `Extracted ${events.length} Events from Notehub through ${
              events.at(-1)?.received || ""
            }...`
          );
          resp.data.through = recurringResponse.data.through;
        } else {
          serverLogInfo(`Extracted ${events.length} Events from Notehub. Done`);
          resp.data.has_more = false;
        }
      }
      return events;
    } catch (e) {
      throw this.errorWithCode(e);
    }
  }

  async setEnvironmentVariables(hubDeviceUID: string, envVars: NotehubEnvVars) {
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
}
