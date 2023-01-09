import axios from "axios";
import MockAdapter from "axios-mock-adapter";
import { ERROR_CODES } from "../Errors";
import AxiosHttpNotehubAccessor from "./AxiosHttpNotehubAccessor";
import NotehubEnvVars from "./models/NotehubEnvVars";
import notehubData from "./models/notehubData.test.json";

let mock: MockAdapter;
const mockBaseURL = "http://example.io";
const mockProjectUID = "app:1234";
const mockDeviceUID = "dev:1234";
const API_ENV_VAR_URL = `${mockBaseURL}/v1/projects/${mockProjectUID}/devices/${mockDeviceUID}/environment_variables`;

const axiosHttpNotehubAccessorMock = new AxiosHttpNotehubAccessor(
  mockBaseURL,
  mockProjectUID,
  ""
);

describe("Environment variable handling", () => {
  beforeEach(() => {
    mock = new MockAdapter(axios);
  });

  type NotehubEnvVarResponse = { environment_variables: NotehubEnvVars };
  const mockEnvVarResponse =
    notehubData.notehubEnvVarResponse as NotehubEnvVarResponse;

  it("should true if the request succeeds", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(200, mockEnvVarResponse);

    const res =
      await axiosHttpNotehubAccessorMock.setEnvironmentVariablesByDevice(
        mockDeviceUID,
        { _sn: "TEST" }
      );
    expect(res).toEqual(true);
  });

  it("Should give an unauthorized error for 401s", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(401, mockEnvVarResponse);

    await expect(
      axiosHttpNotehubAccessorMock.setEnvironmentVariablesByDevice(
        mockDeviceUID,
        {
          _sn: "TEST",
        }
      )
    ).rejects.toThrow(ERROR_CODES.UNAUTHORIZED);
  });

  it("Should give a forbidden error for 403s", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(403, mockEnvVarResponse);

    await expect(
      axiosHttpNotehubAccessorMock.setEnvironmentVariablesByDevice(
        mockDeviceUID,
        {
          _sn: "TEST",
        }
      )
    ).rejects.toThrow(ERROR_CODES.FORBIDDEN);
  });

  it("Should give a device-not-found error for 404s", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(404, mockEnvVarResponse);

    await expect(
      axiosHttpNotehubAccessorMock.setEnvironmentVariablesByDevice(
        mockDeviceUID,
        {
          _sn: "TEST",
        }
      )
    ).rejects.toThrow(ERROR_CODES.DEVICE_NOT_FOUND);
  });

  it("Should give an internal error for 500s", async () => {
    mock.onPut(API_ENV_VAR_URL).reply(500, mockEnvVarResponse);

    await expect(
      axiosHttpNotehubAccessorMock.setEnvironmentVariablesByDevice(
        mockDeviceUID,
        {
          _sn: "TEST",
        }
      )
    ).rejects.toThrow(ERROR_CODES.INTERNAL_ERROR);
  });
});
