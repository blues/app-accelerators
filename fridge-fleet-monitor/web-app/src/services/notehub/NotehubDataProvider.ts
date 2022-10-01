/* eslint-disable class-methods-use-this */
/* eslint-disable @typescript-eslint/no-explicit-any */
import { flattenDeep } from "lodash";
import GatewayDEPRECATED from "../alpha-models/Gateway";
import NodeDEPRECATED from "../alpha-models/Node";
import NotehubDevice from "./models/NotehubDevice";
import {
  DataProvider,
  ProjectHierarchyFilter,
  QueryHistoricalReadings,
  QueryResult,
} from "../DataProvider";
import { NotehubAccessor } from "./NotehubAccessor";
import NotehubRoutedEvent from "./models/NotehubRoutedEvent";
import ReadingDEPRECATED from "../alpha-models/readings/Reading";
import { ERROR_CODES, getError } from "../Errors";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import TemperatureSensorReading from "../alpha-models/readings/TemperatureSensorReading";
import HumiditySensorReading from "../alpha-models/readings/HumiditySensorReading";
import PressureSensorReading from "../alpha-models/readings/PressureSensorReading";
import VoltageSensorReading from "../alpha-models/readings/VoltageSensorReading";
import ContactSwitchSensorReading from "../alpha-models/readings/ContactSwitchSensorReading";
import {
  GatewayWithNodes,
  Node,
  Project,
  ProjectHierarchy,
  ProjectHistoricalData,
  ProjectID,
  ProjectReadingsSnapshot,
  Reading,
  SensorHost,
  SensorHostReadingsSnapshot,
  SensorType,
  SensorTypeNames,
  TimePeriod,
} from "../DomainModel";
import Config from "../../../config";
import { getEpochChartDataDate } from "../../components/presentation/uiHelpers";
import { SignalStrengths } from "../alpha-models/SignalStrengths";

interface HasNodeId {
  nodeId: string;
}

// N.B.: Noteub defines 'best' location with more nuance than we do here (e.g
// considering staleness). Also this algorthm is copy-pasted in a couple places.
export const getBestLocation = (object: NotehubLocationAlternatives) =>
  object.gps_location || object.triangulated_location || object.tower_location;

export function notehubDeviceToSparrowGateway(device: NotehubDevice) {
  return {
    lastActivity: device.last_activity,
    ...(getBestLocation(device) && {
      location: getBestLocation(device)?.name,
    }),
    name: device.serial_number,
    uid: device.uid,
    voltage: device.voltage,
    nodeList: [],
  };
}

export default class NotehubDataProvider implements DataProvider {
  constructor(
    private readonly notehubAccessor: NotehubAccessor,
    private readonly projectID: ProjectID
  ) {}

  async gatewayWithNode(nodeId: string): Promise<GatewayDEPRECATED | null> {
    const all = await this.getGateways();
    all.forEach((g) => {
      g.nodeList.forEach((n) => {
        if (n.nodeId === nodeId) {
          return n;
        }
      });
    });
    return null;
  }

  // eslint-disable-next-line class-methods-use-this
  doBulkImport(): Promise<never> {
    throw new Error("It's not possible to do bulk import of data to Notehub");
  }

  /**
   * We made the interface more general (accepting a projectID) but the implementation has the
   * ID fixed. This is a quick check to be sure the project ID is the one expected.
   * @param projectID
   */
  private checkProjectID(projectID: ProjectID) {
    if (projectID.projectUID !== this.projectID.projectUID) {
      throw new Error("Project ID does not match expected ID");
    }
  }

  async queryProjectLatestValues(
    projectID: ProjectID
  ): Promise<QueryResult<ProjectID, ProjectReadingsSnapshot>> {
    this.checkProjectID(projectID);

    const gateways = new Set<GatewayWithNodes>();

    const project: ProjectHierarchy = {
      id: projectID,
      name: Config.companyName,
      description: null,
      gateways,
    };

    const results: ProjectReadingsSnapshot = {
      when: Date.now(),
      project,
      hostReadings(sensorHost: SensorHost): SensorHostReadingsSnapshot {
        throw new Error("Function not implemented.");
      },
      hostReadingByName(
        sensorHost: SensorHost,
        readingName: SensorTypeNames
      ): Reading | undefined {
        throw new Error("Function not implemented.");
      },
    };

    return { request: projectID, results };
  }

  async queryProjectReadingSeries(
    request: QueryHistoricalReadings
  ): Promise<QueryResult<QueryHistoricalReadings, ProjectHistoricalData>> {
    const results: ProjectHistoricalData = {
      period: request.timeFilter,
      hostReadings: new Map(),
      project: await this.buildProjectHierarchy(request.projectFilter),
    };
    return { request, results };
  }

  queryProjectReadingCount(
    projectID: ProjectID
  ): Promise<QueryResult<ProjectID, number>> {
    throw new Error("Method not implemented.");
  }

  private async buildProjectHierarchy(
    projectFilter: ProjectHierarchyFilter
  ): Promise<ProjectHierarchy> {
    throw new Error("Method not implemented.");
  }

  async getGateways(): Promise<GatewayDEPRECATED[]> {
    const gateways: GatewayDEPRECATED[] = [];
    const rawDevices = await this.notehubAccessor.getDevices();
    rawDevices.forEach((device) => {
      gateways.push(notehubDeviceToSparrowGateway(device));
    });
    return gateways;
  }

  async getGateway(gatewayUID: string): Promise<GatewayDEPRECATED> {
    const singleGatewayJson = await this.notehubAccessor.getDevice(gatewayUID);

    const singleGateway = notehubDeviceToSparrowGateway(singleGatewayJson);

    return singleGateway;
  }

  async getNodes(gatewayUIDs: string[]) {
    // get latest node data from API
    const getLatestNodeDataByGateway = async (gatewayUID: string) => {
      const latestNodeEvents = await this.notehubAccessor.getLatestEvents(
        gatewayUID
      );

      // filter out all latest_events that are not `switch.qo` or `air.qo` files - those indicate they are node files
      const filteredNodeData = latestNodeEvents.latest_events.filter(
        (event: NotehubRoutedEvent) => {
          if (
            event.file.includes("#switch.qo") ||
            event.file.includes("#air.qo")
          ) {
            return true;
          }
          return false;
        }
      );

      const latestNodeData = filteredNodeData.map((event) => ({
        gatewayUID,
        nodeId: event.file,
        humidity: event.body.humidity,
        // Convert from Pa to kPa
        pressure: event.body.pressure ? event.body.pressure / 1000 : undefined,
        temperature: event.body.temperature,
        voltage: event.body.voltage,
        contactSwitch: event.body.contactSwitch,
        lastActivity: new Date(event.when * 1000).toISOString(),
      }));
      return latestNodeData;
    };

    // If we have more than one gateway to get events for,
    // loop through all the gateway UIDs and collect the events back
    const getAllLatestNodeEvents = async () =>
      Promise.all(gatewayUIDs.map(getLatestNodeDataByGateway));

    const latestNodeEvents = await getAllLatestNodeEvents();

    const simplifiedNodeEvents = flattenDeep(latestNodeEvents).map(
      (nodeEvent) => ({
        name: undefined,
        gatewayUID: nodeEvent.gatewayUID,
        nodeId: nodeEvent.nodeId.split("#")[0],
        humidity: nodeEvent.humidity,
        pressure: nodeEvent.pressure,
        temperature: nodeEvent.temperature,
        voltage: nodeEvent.voltage,
        contactSwitch: nodeEvent.contactSwitch,
        lastActivity: nodeEvent.lastActivity,
      })
    );

    // merge objects with different defined and undefined properties into a single obj
    const mergeObject = <CombinedEventObj>(
      A: any,
      B: any
    ): CombinedEventObj => {
      const res: any = {};
      // eslint-disable-next-line @typescript-eslint/no-unsafe-argument, array-callback-return, @typescript-eslint/no-unsafe-assignment
      Object.keys({ ...A, ...B }).map((key) => {
        // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-member-access
        res[key] = B[key] || A[key];
      });
      return res as CombinedEventObj;
    };

    // merge latest event objects with the same nodeId
    // these are different readings from the same node
    const reducer = <CombinedEventObj extends HasNodeId>(
      groups: Map<string, CombinedEventObj>,
      event: CombinedEventObj
    ) => {
      // make nodeId the map's key
      const key = event.nodeId;
      // fetch previous map values associated with that key
      const previous = groups.get(key);
      // combine the previous map event with new map event
      const merged: CombinedEventObj = mergeObject(previous || {}, event);
      // set the key and newly merged object as the value
      groups.set(key, merged);
      return groups;
    };

    // run the node events through the reducer and then pull only their values into a new Map iterator obj
    const reducedEventsIterator = simplifiedNodeEvents
      .reduce(reducer, new Map())
      .values();

    // transform the Map iterator obj into plain array
    const reducedEvents = Array.from(reducedEventsIterator);

    // get the names and locations of the nodes from the API via config.db
    const getExtraNodeDetails = async (gatewayNodeInfo: NodeDEPRECATED) => {
      const nodeDetailsInfo = await this.notehubAccessor.getConfig(
        gatewayNodeInfo.gatewayUID,
        gatewayNodeInfo.nodeId
      );

      // put it all together in one object
      return {
        nodeId: gatewayNodeInfo.nodeId,
        gatewayUID: gatewayNodeInfo.gatewayUID,
        ...(nodeDetailsInfo?.body?.name && {
          name: nodeDetailsInfo.body.name,
        }),
        ...(nodeDetailsInfo?.body?.loc && {
          location: nodeDetailsInfo.body.loc,
        }),
        ...(gatewayNodeInfo.voltage && {
          voltage: gatewayNodeInfo.voltage,
        }),
        lastActivity: gatewayNodeInfo.lastActivity,
        ...(gatewayNodeInfo.humidity && {
          humidity: gatewayNodeInfo.humidity,
        }),
        ...(gatewayNodeInfo.pressure && {
          pressure: gatewayNodeInfo.pressure,
        }),
        ...(gatewayNodeInfo.temperature && {
          temperature: gatewayNodeInfo.temperature,
        }),
        ...(gatewayNodeInfo.doorStatus && {
          doorStatus: gatewayNodeInfo.doorStatus,
        }),
        // todo replace this with real bars data once calculation to turn notecard data into bars is implemented
        bars: "N/A" as SignalStrengths,
      };
    };

    const getAllNodeData = async (gatewayNodeInfo: NodeDEPRECATED[]) =>
      Promise.all(gatewayNodeInfo.map(getExtraNodeDetails));
    // eslint-disable-next-line @typescript-eslint/no-unsafe-argument
    const allLatestNodeData = await getAllNodeData(reducedEvents);

    return allLatestNodeData;
  }

  /**
   * This implementation requires the gatewayUID
   */
  async getNode(gatewayUID: string | null, nodeId: string) {
    let match;
    if (gatewayUID) {
      const nodes = await this.getNodes([gatewayUID]);
      match = nodes.filter((node) => node.nodeId === nodeId)[0];
    }
    if (!match) {
      throw getError(ERROR_CODES.NODE_NOT_FOUND);
    }
    return match;
  }

  async getNodeData(
    gatewayUID: string,
    nodeId: string,
    minutesBeforeNow: number
  ) {
    let nodeEvents: NotehubRoutedEvent[];
    if (minutesBeforeNow) {
      const epochDateString: string = getEpochChartDataDate(minutesBeforeNow);
      nodeEvents = await this.notehubAccessor.getEvents(epochDateString);
    } else {
      nodeEvents = await this.notehubAccessor.getEvents();
    }

    // filter for a specific node ID
    const filteredEvents: NotehubRoutedEvent[] = nodeEvents.filter(
      (event: NotehubRoutedEvent) =>
        event.file &&
        event.file.includes(`${nodeId}`) &&
        (event.file.includes("#air.qo") || event.file.includes("#switch.qo")) &&
        event.device === gatewayUID
    );
    const readingsToReturn: ReadingDEPRECATED<unknown>[] = [];
    filteredEvents.forEach((event: NotehubRoutedEvent) => {
      const captured = new Date(event.when * 1000).toISOString();
      if (event.body.temperature) {
        readingsToReturn.push(
          new TemperatureSensorReading({
            value: event.body.temperature,
            captured,
          })
        );
      }
      if (event.body.humidity) {
        readingsToReturn.push(
          new HumiditySensorReading({
            value: event.body.humidity,
            captured,
          })
        );
      }
      if (event.body.pressure) {
        readingsToReturn.push(
          new PressureSensorReading({
            // Convert from Pa to kPa
            value: event.body.pressure / 1000,
            captured,
          })
        );
      }
      if (event.body.voltage) {
        readingsToReturn.push(
          new VoltageSensorReading({
            value: event.body.voltage,
            captured,
          })
        );
      }
      if (event.body.contactSwitch) {
        readingsToReturn.push(
          new ContactSwitchSensorReading({
            value: event.body.contactSwitch,
            captured,
          })
        );
      }
    });

    return readingsToReturn;
  }
}
