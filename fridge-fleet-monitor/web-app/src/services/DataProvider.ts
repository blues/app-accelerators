import GatewayDEPRECATED from "./alpha-models/Gateway";
import NodeDEPRECATED from "./alpha-models/Node";
import ReadingDEPRECATED from "./alpha-models/readings/Reading";
import {
  ProjectID,
  ProjectReadingsSnapshot,
  GatewayID,
  NodeID,
  SensorTypeID,
  TimePeriod,
  ProjectHistoricalData,
} from "./DomainModel";

export type ProjectHierarchyFilter = {
  projectID: ProjectID;
  gatewayID?: GatewayID;
  nodeID?: NodeID;
};

export type TimePeriodFilter = TimePeriod;

export type QueryHistoricalReadings = {
  projectFilter: ProjectHierarchyFilter;
  timeFilter: TimePeriodFilter;
};

export interface BulkImport {
  itemCount: number;
  errorCount: number;
}

// this interface shows gateway or node data - nothing more, nothing less
export interface DataProvider {
  doBulkImport: () => Promise<BulkImport>;

  getGateways: () => Promise<GatewayDEPRECATED[]>;

  getGateway: (gatewayUID: string) => Promise<GatewayDEPRECATED>;

  getNodes: (gatewayUIDs: string[]) => Promise<NodeDEPRECATED[]>;

  /**
   * Some implementations require the gatewayUID, others don't.
   */
  getNode: (
    gatewayUID: string | null,
    nodeId: string
  ) => Promise<NodeDEPRECATED>;

  getNodeData: (
    gatewayUID: string,
    nodeId: string,
    minutesBeforeNow: number
  ) => Promise<ReadingDEPRECATED<unknown>[]>;

  gatewayWithNode(nodeId: string): Promise<GatewayDEPRECATED | null>;

  // queryProject?(f: SimpleFilter): Query<SimpleFilter, Project>;

  /**
   * Retrieves the hierarchy of gateways and nodes, and the latest sensor readings for these.
   * @param projectID The project to retrieve the latest values for
   */
  queryProjectLatestValues(
    projectID: ProjectID
  ): Promise<QueryResult<ProjectID, ProjectReadingsSnapshot>>;

  queryProjectReadingCount(
    projectID: ProjectID
  ): Promise<QueryResult<ProjectID, number>>;

  queryProjectReadingSeries(
    query: QueryHistoricalReadings
  ): Promise<QueryResult<QueryHistoricalReadings, ProjectHistoricalData>>;
}

// Query Interface - not using this for now.  May need it when we retrieve just a single gateway or single sensor data

export type DateRange = { from: Date; to: Date };

export const all = Symbol("All");
export type All = typeof all;

export const latest = Symbol("Latest");
export type Latest = typeof latest;

export type Nothing = undefined;

export type SimpleFilter = {
  gateways?: GatewayID | All | Nothing;
  nodes?: NodeID | All | Nothing;
  SensorTypes?: SensorTypeID | All | Nothing;
  readingTimeframe?: DateRange | All | Nothing | Latest;
};

export interface QueryResult<R, P> {
  request: R;
  results: P;
}
