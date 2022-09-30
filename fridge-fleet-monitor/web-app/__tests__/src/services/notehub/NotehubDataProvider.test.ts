import { NotehubAccessor } from "../../../../src/services/notehub/NotehubAccessor";
import NotehubDataProvider, {
  notehubDeviceToSparrowGateway,
} from "../../../../src/services/notehub/NotehubDataProvider";
import sparrowData from "../__serviceMocks__/sparrowData.json"; // mocked data to do with Sparrow portion of app goes here (i.e. gateways and nodes)
import notehubData from "../__serviceMocks__/notehubData.json"; // mocked data to do with Notehub portion of app goes here (i.e. devices and events)
import NotehubDevice from "../../../../src/services/notehub/models/NotehubDevice";
import TemperatureSensorSchema from "../../../../src/services/alpha-models/readings/TemperatureSensorSchema";
import HumiditySensorSchema from "../../../../src/services/alpha-models/readings/HumiditySensorSchema";
import PressureSensorSchema from "../../../../src/services/alpha-models/readings/PressureSensorSchema";
import VoltageSensorSchema from "../../../../src/services/alpha-models/readings/VoltageSensorSchema";
import NotehubLatestEvents from "../../../../src/services/notehub/models/NotehubLatestEvents";
import NotehubSensorConfig from "../../../../src/services/notehub/models/NotehubNodeConfig";
import NotehubResponse from "../../../../src/services/notehub/models/NotehubResponse";
import Gateway from "../../../../src/services/alpha-models/Gateway";
import Node from "../../../../src/services/alpha-models/Node";
import IDBuilder from "../../../../src/services/IDBuilder";

const mockProjectID = IDBuilder.buildProjectID("app:mockID");

describe("Notehub data provider service functions", () => {
  const mockedGatewayJson =
    notehubData.successfulNotehubDeviceResponse as NotehubDevice;
  const mockedNotehubLatestEventsJson =
    notehubData.successfulNotehubLatestEventsResponse as unknown as NotehubLatestEvents;
  const mockedNotehubEventsJson = notehubData.successfulNotehubEventResponse
    .events as NotehubResponse;
  const mockedNotehubConfigJson =
    notehubData.successfulNotehubConfigResponse2 as NotehubSensorConfig;
  const mockedNotehubConfigJsonNoNodeDetails =
    notehubData.successfulNotehubConfigResponse3 as NotehubSensorConfig;

  let notehubAccessorMock: NotehubAccessor;
  let notehubDataProviderMock: NotehubDataProvider;

  beforeEach(() => {
    notehubAccessorMock = {
      getDevice: jest.fn().mockResolvedValueOnce(mockedGatewayJson),
      getDevices: jest.fn().mockResolvedValueOnce([mockedGatewayJson]),
      getLatestEvents: jest
        .fn()
        .mockResolvedValueOnce(mockedNotehubLatestEventsJson),
      getEvents: jest.fn().mockResolvedValueOnce(mockedNotehubEventsJson),
      getConfig: jest.fn().mockResolvedValueOnce(mockedNotehubConfigJson),
      setConfig: jest.fn().mockResolvedValueOnce({}),
      setEnvironmentVariables: jest.fn().mockResolvedValueOnce({}),
    };
    notehubDataProviderMock = new NotehubDataProvider(
      notehubAccessorMock,
      mockProjectID
    );
  });

  it("should convert a Notehub device to a Sparrow gateway", async () => {
    const mockedGatewaysSparrowData =
      sparrowData.successfulGatewaySparrowDataResponse as Gateway;
    const res = await notehubDataProviderMock.getGateway(mockedGatewayJson.uid);
    expect(res).toEqual(mockedGatewaysSparrowData);
  });

  it("should return a list sparrow gateway instances when getGateways is called", async () => {
    const mockedGatewaysSparrowData = [
      sparrowData.successfulGatewaySparrowDataResponse,
    ];

    const res = await notehubDataProviderMock.getGateways();
    expect(res).toEqual(mockedGatewaysSparrowData);
  });

  it("should return sparrow node data when a gatewayUID and node id is passed to getNode", async () => {
    const mockedNodeSparrowData =
      sparrowData.successfulNodeSparrowDataResponse as Node[];

    const res = await notehubDataProviderMock.getNode(
      mockedGatewayJson.uid,
      "456789b"
    );
    expect(res).toEqual(mockedNodeSparrowData[1]);
  });

  it("should return a list of sparrow node data when a list of gateway UIDs is passed in to getNodes", async () => {
    const mockedNodeSparrowData =
      sparrowData.successfulNodeSparrowDataResponse as Node[];

    const res = await notehubDataProviderMock.getNodes([mockedGatewayJson.uid]);
    expect(res).toEqual(mockedNodeSparrowData);
  });

  it("should return a list of sparrow sensor readings when a gateway UID, node id and optional start date is to getNodeData", async () => {
    const res = await notehubDataProviderMock.getNodeData(
      sparrowData.mockedGatewayUID2,
      sparrowData.mockedNodeId2,
      0
    );

    expect(JSON.stringify(res[0].schema)).toEqual(
      JSON.stringify(TemperatureSensorSchema)
    );
    expect(JSON.stringify(res[1].schema)).toEqual(
      JSON.stringify(HumiditySensorSchema)
    );
    expect(JSON.stringify(res[2].schema)).toEqual(
      JSON.stringify(PressureSensorSchema)
    );
    expect(JSON.stringify(res[3].schema)).toEqual(
      JSON.stringify(VoltageSensorSchema)
    );
  });

  it("should gracefully handle when there is no additional node details for sparrow sensor readings and remove undefined information from the returned Node object", async () => {
    jest.fn().mockResolvedValueOnce(mockedNotehubConfigJsonNoNodeDetails);

    const mockedNodeSparrowData =
      sparrowData.successfulNodeSparrowDataResponse as Node[];

    const res = await notehubDataProviderMock.getNode(
      mockedGatewayJson.uid,
      "456789b"
    );
    expect(res).toEqual(mockedNodeSparrowData[1]);
  });
});

describe("Location handling", () => {
  const getMockDevice = () =>
    ({ ...notehubData.successfulNotehubDeviceResponse } as NotehubDevice);

  it("should not produce a location property when none exist", () => {
    const mockDevice = getMockDevice();
    const res = notehubDeviceToSparrowGateway(mockDevice);
    expect(res.location).toBe(undefined);
  });

  it("should choose triangulated location over tower", () => {
    const mockDevice = getMockDevice();
    mockDevice.tower_location = notehubData.exampleNotehubLocation1;
    mockDevice.triangulated_location = notehubData.exampleNotehubLocation2;
    const res = notehubDeviceToSparrowGateway(mockDevice);
    expect(res.location).toBe(mockDevice.triangulated_location.name);
  });

  it("should use tower location if it's the only one available", () => {
    const mockDevice = getMockDevice();
    mockDevice.tower_location = notehubData.exampleNotehubLocation1;
    const res = notehubDeviceToSparrowGateway(mockDevice);
    expect(res.location).toBe(mockDevice.tower_location.name);
  });
});
