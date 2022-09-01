import AppService from "./AppService";
import { DataProvider } from "./DataProvider";
import { AttributeStore } from "./AttributeStore";
import { IDBuilder } from "./IDBuilder";
import * as AppModel from "./AppModel";
import { NotificationsStore } from "./NotificationsStore";
import { Mock } from "moq.ts"
import appServiceData from "./AppService.test.json";
import { Device } from "./AppModel";

const mockProjectUID = "app:123456";

const { mockedDeviceUID } = appServiceData;

const mockedDeviceData = appServiceData.successfulDeviceDataResponse as Device;


describe("App Service", () => {
  let dataProviderMock: DataProvider;
  let attributeStoreMock: AttributeStore;
  let appServiceMock: AppService;

  beforeEach(() => {
    dataProviderMock = {
      doBulkImport: jest.fn(),
      getProject: jest.fn(),
      getDevice: jest.fn().mockReturnValueOnce(mockedDeviceData),
      getDevices: jest.fn()
    };
    const mockEventHandler = {
      handleEvent: jest.fn(),
    };
    const mockIDBuilder: IDBuilder = {
      buildProjectID: (projectUID: string): AppModel.ProjectID => {
        return { projectUID, type: "ProjectID" };
      },
      buildDeviceID: function (deviceUID: string): AppModel.DeviceID {
        return { deviceUID, type: "DeviceID" };
      },
    };
    const notificationsStoreMock = new Mock<NotificationsStore>().object();
    appServiceMock = new AppService(
      mockProjectUID,
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
