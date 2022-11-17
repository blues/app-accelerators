import axios from "axios";
import { sub } from "date-fns";
import MockAdapter from "axios-mock-adapter";
import { ERROR_CODES } from "../Errors";
import AxiosHttpNotehubAccessor from "./AxiosHttpNotehubAccessor";
import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import NotehubLatestEvents from "./models/NotehubLatestEvents";
import NotehubDeviceConfig from "./models/NotehubDeviceConfig";
import notehubData from "./models/notehubData.test.json";

let mock: MockAdapter;
const mockBaseURL = "http://example.io";
const mockProjectUID = "app:1234";
const mockDeviceUID = "dev:1234";
const mockHubHistoricalDataRecentMinutes = 1440;
const mockedStartDate = sub(new Date(), {
  minutes: mockHubHistoricalDataRecentMinutes,
});
const mockedEpochTimeValue = Math.round(mockedStartDate.getTime() / 1000);

const API_DEVICE_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/devices/${mockDeviceUID}`;
const API_DEVICES_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/devices`;
const API_CONFIG_URL = `${mockBaseURL}/req?project=${mockProjectUID}&device=${mockDeviceUID}`;
const API_ENV_VAR_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/devices/${mockDeviceUID}/environment_variables`;
const API_LATEST_EVENTS_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/devices/${mockDeviceUID}/latest`;
const API_INITIAL_ALL_EVENTS_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/events`;

const axiosHttpNotehubAccessorMock = new AxiosHttpNotehubAccessor(
  mockBaseURL,
  mockProjectUID,
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

  const mockNotehubLatestEventData =
    notehubData.successfulNotehubLatestEventsResponse as NotehubLatestEvents;
  const mockNotehubEventData = notehubData.successfulNotehubEventResponse;

  it("should return a list of latest events when getLatestEvents is called with a valid hub device UID", async () => {
    mock.onGet(API_LATEST_EVENTS_URL).reply(200, mockNotehubLatestEventData);

    const res = await axiosHttpNotehubAccessorMock.getLatestEvents(
      mockDeviceUID
    );
    expect(res).toEqual(mockNotehubLatestEventData);
  });

  it("should throw a device-not-found error when an invalid device UID is passed in", async () => {
    mock.onGet(API_LATEST_EVENTS_URL).reply(404, mockNotehubLatestEventData);

    await expect(
      axiosHttpNotehubAccessorMock.getLatestEvents(mockDeviceUID)
    ).rejects.toThrow(ERROR_CODES.DEVICE_NOT_FOUND);
  });

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

describe("Config handling", () => {
  beforeEach(() => {
    mock = new MockAdapter(axios);
  });

  const mockNotehubDeviceConfig =
    notehubData.successfulNotehubConfigResponse as NotehubDeviceConfig;
  const mockMacAddress = mockNotehubDeviceConfig.note;

  it("should return valid config", async () => {
    mock.onPost(API_CONFIG_URL).reply(200, mockNotehubDeviceConfig);

    const res = await axiosHttpNotehubAccessorMock.getConfig(
      mockDeviceUID,
      mockMacAddress
    );
    expect(res).toEqual(mockNotehubDeviceConfig);
  });

  it("should pass through the response (and not throw an error) with a bad config", async () => {
    mock
      .onPost(API_CONFIG_URL)
      .reply(200, notehubData.notehubConfigNoteNotFound);
    const res = await axiosHttpNotehubAccessorMock.getConfig(
      mockDeviceUID,
      mockMacAddress
    );
    expect(res).toEqual(notehubData.notehubConfigNoteNotFound);
  });

  it("should throw a device-not-found error if the device does not exist", async () => {
    mock
      .onPost(API_CONFIG_URL)
      .reply(200, notehubData.notehubConfigDeviceNotFound);
    await expect(
      axiosHttpNotehubAccessorMock.getConfig(mockDeviceUID, mockMacAddress)
    ).rejects.toThrow(ERROR_CODES.DEVICE_NOT_FOUND);
  });

  it("should throw a forbidden error if the API returns insufficient permissions", async () => {
    mock
      .onPost(API_CONFIG_URL)
      .reply(200, notehubData.notehubConfigBadPermissions);
    await expect(
      axiosHttpNotehubAccessorMock.getConfig(mockDeviceUID, mockMacAddress)
    ).rejects.toThrow(ERROR_CODES.FORBIDDEN);
  });

  it("should throw an internal error if the API returns a 500", async () => {
    mock.onPost(API_CONFIG_URL).reply(500, {});

    await expect(
      axiosHttpNotehubAccessorMock.getConfig(mockDeviceUID, mockMacAddress)
    ).rejects.toThrow(ERROR_CODES.INTERNAL_ERROR);
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
