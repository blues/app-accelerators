import * as DomainModel from "./DomainModel";

export type ProjectID = DomainModel.ProjectID;
export type GatewayID = DomainModel.GatewayID;
export type NodeID = DomainModel.NodeID;
export type SensorTypeID = DomainModel.SensorTypeID;

/**
 * The presentation models here are json-serializable representations of the domain model.
 */

export interface Project {
  readonly id: ProjectID;

  // Attributes
  name: string;
  description: string | null;

  gateways: Gateway[] | null; // null when the gateways are not required
}

/**
 * Dates are in UTC time, expressed as seconds since the epoch.
 */
export type AppDate = number;

// export type ReadingsKeyedBySensorTypeName = { [key in string]: Reading };

export type SensorTypeCurrentReading = {
  sensorType: SensorType;
  reading: Reading | null;
};

export type ReadingSeries = {
  readings: Reading[];
};

// export type ReadingSeriesKeyedBySensorTypeName = { [key in string]: ReadingSeries };

/**
 * Common elements of Sparrow devices with sensors (Gateways and Nodes)
 */
export interface SensorHost {
  name: string | null;
  descriptionBig: string | null;
  descriptionSmall: string | null;
  lastSeen: AppDate | null;
  locationName: string | null;

  currentReadings: SensorTypeCurrentReading[] | null;

  historicalReadings: null; // todo - define type
}

export interface Gateway extends SensorHost {
  readonly id: GatewayID;

  nodes: Node[] | null; // null when the nodes were not fetched.
}

export interface Node extends SensorHost {
  readonly id: NodeID;

  /**
   * Nodes may be unnamed.
   */
  name: string | null;
}

/**
 * Presently the DomainModel is json serializable.
 */
export type SensorType = DomainModel.SensorType;

export type Reading = DomainModel.Reading;

export interface ProjectReadingsSnapshot {
  when: AppDate;
  project: Project;
}

export { NodeSensorTypeNames, GatewaySensorTypeNames } from "./DomainModel";
