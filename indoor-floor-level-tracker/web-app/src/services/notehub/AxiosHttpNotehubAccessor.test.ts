import axios from "axios";
import MockAdapter from "axios-mock-adapter";
import { ERROR_CODES } from "../Errors";
import AxiosHttpNotehubAccessor from "./AxiosHttpNotehubAccessor";
import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import notehubData from "./models/notehubData.test.json";

let mock: MockAdapter;
const mockBaseURL = "http://example.io";
const mockProjectUID = "app:1234";
const mockDeviceUID = "dev:1234";
const mockHubFleetUID = "fleet:1234";

const API_DEVICE_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/devices/${mockDeviceUID}`;
const API_DEVICES_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/devices`;
const API_ENV_VAR_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/devices/${mockDeviceUID}/environment_variables`;
const API_INITIAL_ALL_EVENTS_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/events`;

const axiosHttpNotehubAccessorMock = new AxiosHttpNotehubAccessor(
  mockBaseURL,
  mockProjectUID,
  mockHubFleetUID,
  ""
);

describe("Device handling", () => {
  beforeEach(() => {
    mock = new MockAdapter(axios);
  });

  const mockNotehubDeviceData =
    notehubData.successfulNotehubDeviceResponse as NotehubDevice;

  it("should return a valid response when a device UID is passed to the getDevice endpoint", async () => {
    mock.onGet(API_DEVICE_URL).reply(200, mockNotehubDeviceData);

    const res = await axiosHttpNotehubAccessorMock.getDevice(mockDeviceUID);
    expect(res).toEqual(mockNotehubDeviceData);
  });

  it("Should give an unauthorized error for 401s", async () => {
    mock.onGet(API_DEVICE_URL).reply(401, mockNotehubDeviceData);

    await expect(
      axiosHttpNotehubAccessorMock.getDevice(mockDeviceUID)
    ).rejects.toThrow(ERROR_CODES.UNAUTHORIZED);
  });

  it("Should give a forbidden error for 403s", async () => {
    mock.onGet(API_DEVICE_URL).reply(403, mockNotehubDeviceData);

    await expect(
      axiosHttpNotehubAccessorMock.getDevice(mockDeviceUID)
    ).rejects.toThrow(ERROR_CODES.FORBIDDEN);
  });

  it("Should give a device-not-found error for 404s", async () => {
    mock.onGet(API_DEVICE_URL).reply(404, mockNotehubDeviceData);

    await expect(
      axiosHttpNotehubAccessorMock.getDevice(mockDeviceUID)
    ).rejects.toThrow(ERROR_CODES.DEVICE_NOT_FOUND);
  });

  it("Should give an internal error for 500s", async () => {
    mock.onGet(API_DEVICE_URL).reply(500, mockNotehubDeviceData);

    await expect(
      axiosHttpNotehubAccessorMock.getDevice(mockDeviceUID)
    ).rejects.toThrow(ERROR_CODES.INTERNAL_ERROR);
  });

  it("should return a valid response when getting all devices", async () => {
    mock
      .onGet(API_DEVICES_URL)
      .reply(200, { devices: [mockNotehubDeviceData] });

    const res = await axiosHttpNotehubAccessorMock.getDevices();
    expect(res).toEqual([mockNotehubDeviceData]);
  });
});

describe("Event handling", () => {
  beforeEach(() => {
    mock = new MockAdapter(axios);
  });

  const mockNotehubEventData = notehubData.successfulNotehubEventResponse;

  it("should return a list of events when getEvents is called with a valid hub app UID and date range", async () => {
    mock.onGet(API_INITIAL_ALL_EVENTS_URL).reply(200, mockNotehubEventData);
    const res = await axiosHttpNotehubAccessorMock.getEvents();

    expect(res).toEqual(mockNotehubEventData.events);
  });

  it("should throw a device-not-found error when an invalid hub app UID is used", async () => {
    mock.onGet(API_INITIAL_ALL_EVENTS_URL).reply(404, mockNotehubEventData);

    await expect(axiosHttpNotehubAccessorMock.getEvents()).rejects.toThrow(
      ERROR_CODES.DEVICE_NOT_FOUND
    );
  });
});

describe("Environment variable handling", () => {
  beforeEach(() => {
    mock = new MockAdapter(axios);
  });

  type NotehubEnvVarResponse = { environment_variables: NotehubEnvVars };
  const mockEnvVarResponse =
    notehubData.notehubEnvVarResponse as NotehubEnvVarResponse;

  it("should true if the request succeeds", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(200, mockEnvVarResponse);

    const res = await axiosHttpNotehubAccessorMock.setEnvironmentVariables(
      mockDeviceUID,
      { _sn: "TEST" }
    );
    expect(res).toEqual(true);
  });

  it("Should give an unauthorized error for 401s", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(401, mockEnvVarResponse);

    await expect(
      axiosHttpNotehubAccessorMock.setEnvironmentVariables(mockDeviceUID, {
        _sn: "TEST",
      })
    ).rejects.toThrow(ERROR_CODES.UNAUTHORIZED);
  });

  it("Should give a forbidden error for 403s", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(403, mockEnvVarResponse);

    await expect(
      axiosHttpNotehubAccessorMock.setEnvironmentVariables(mockDeviceUID, {
        _sn: "TEST",
      })
    ).rejects.toThrow(ERROR_CODES.FORBIDDEN);
  });

  it("Should give a device-not-found error for 404s", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(404, mockEnvVarResponse);

    await expect(
      axiosHttpNotehubAccessorMock.setEnvironmentVariables(mockDeviceUID, {
        _sn: "TEST",
      })
    ).rejects.toThrow(ERROR_CODES.DEVICE_NOT_FOUND);
  });

  it("Should give an internal error for 500s", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(500, mockEnvVarResponse);

    await expect(
      axiosHttpNotehubAccessorMock.setEnvironmentVariables(mockDeviceUID, {
        _sn: "TEST",
      })
    ).rejects.toThrow(ERROR_CODES.INTERNAL_ERROR);
  });
});
