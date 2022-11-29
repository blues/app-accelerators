import { Mock } from "moq.ts";
import AppService from "./AppService";
import { DataProvider } from "./DataProvider";
import { AttributeStore } from "./AttributeStore";
import { IDBuilder } from "./IDBuilder";
import * as AppModel from "./AppModel";
import { NotificationsStore } from "./NotificationsStore";
import appServiceData from "./AppService.test.json";

const mockProjectUID = "app:123456";
const mockFleetUID = "fleet:abcde";

const { mockedDeviceUID } = appServiceData;

const mockedDeviceData = appServiceData.successfulDeviceDataResponse;

describe("App Service", () => {
  let dataProviderMock: DataProvider;
  let attributeStoreMock: AttributeStore;
  let appServiceMock: AppService;

  beforeEach(() => {
    dataProviderMock = {
      getProject: jest.fn(),
      getDevice: jest.fn().mockReturnValueOnce(mockedDeviceData),
      getDevices: jest.fn(),
      getDeviceEnvVars: jest.fn(),
      getDeviceEvents: jest.fn(),
      getDevicesByFleet: jest.fn(),
      getFleetEnvVars: jest.fn(),
      getFleetsByDevice: jest.fn(),
      getFleetsByProject: jest.fn(),
      getValveMonitorConfig: jest.fn(),
      getValveMonitorDeviceData: jest.fn(),
    };
    const mockEventHandler = {
      handleEvent: jest.fn(),
    };
    const mockIDBuilder: IDBuilder = {
      buildProjectID: (projectUID: string): AppModel.ProjectID => ({
        projectUID,
        type: "ProjectID",
      }),
      buildDeviceID(deviceUID: string): AppModel.DeviceID {
        return { deviceUID, type: "DeviceID" };
      },
      buildFleetID: (fleetUID: string): AppModel.FleetID => ({
        fleetUID,
        type: "FleetID",
      }),
      buildEventID: (eventUID: string): AppModel.EventID => ({
        eventUID,
        type: "EventID",
      }),
    };
    const notificationsStoreMock = new Mock<NotificationsStore>().object();
    appServiceMock = new AppService(
      mockProjectUID,
      mockFleetUID,
      mockIDBuilder,
      dataProviderMock,
      mockEventHandler,
      attributeStoreMock,
      notificationsStoreMock
    );
  });

  it("should return a single device when getDevice is called", async () => {
    const res = await appServiceMock.getDevice(mockedDeviceUID);
    expect(res).toEqual(mockedDeviceData);
  });
});
