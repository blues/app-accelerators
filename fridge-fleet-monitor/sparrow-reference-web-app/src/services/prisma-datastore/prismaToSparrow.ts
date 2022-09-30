import Prisma from "@prisma/client";
import Gateway from "../alpha-models/Gateway";
import Node from "../alpha-models/Node";
import { NodeSensorTypeNames } from "../DomainModel";
import { LoraSignalMetricsToSignalStrengths } from "../alpha-models/SignalStrengths";

export type SensorLatestReadings = (Prisma.Sensor & {
  latest: Prisma.Reading | null;
  schema: Prisma.ReadingSchema;
})[];

export type LatestReadingSourceReadings = {
  readingSource: Prisma.ReadingSource & {
    sensors: SensorLatestReadings;
  };
};

export type GatewayWithLatestReadings = Prisma.Gateway &
  LatestReadingSourceReadings;
export type NodeWithLatestReadings = Prisma.Node & LatestReadingSourceReadings;

export type NodeWithGateway = Prisma.Node & { gateway: Prisma.Gateway };

/**
 * Converts a prisma gateway to the old domain model.
 * @param gw
 * @returns
 */
export function sparrowGatewayFromPrismaGateway(
  pGateway: Prisma.Gateway
): Gateway {
  return {
    uid: pGateway.deviceUID,
    name: pGateway.name || "", // todo - we will be reworking the Gateway/Sensor(Node) models. name should be optional
    location: pGateway.locationName || "",
    lastActivity: pGateway.lastSeenAt?.toString() || "", // todo - ideally this is simply cached
    nodeList: [],
  };
}

type LatestReadingsMap = Map<string, Prisma.Reading>;

function sensorReadingsByName(
  sensors: SensorLatestReadings
): LatestReadingsMap {
  const result = new Map<string, Prisma.Reading>();
  sensors.forEach((sensor) => {
    if (sensor.latest) {
      result.set(sensor.schema.name, sensor.latest);
    }
  });
  return result;
}

function findReading(
  readings: LatestReadingsMap,
  name: string
): Prisma.Reading | undefined {
  const result = readings.get(name);
  return result;
}

function asNumber(reading?: Prisma.Reading): number | undefined {
  let result;
  if (typeof reading?.value === "number") {
    result = reading.value;
  }
  return result;
}

function asString(reading?: Prisma.Reading): string | undefined {
  let result;
  if (typeof reading?.value === "string") {
    result = reading.value;
  }
  return result;
}

function downscale(value: number | undefined, scale: number) {
  if (value !== undefined) return value / scale;
}

export function sparrowNodeFromPrismaNode(
  gatewayUID: string,
  prismaNode: NodeWithLatestReadings
): Node {
  const map = sensorReadingsByName(prismaNode.readingSource.sensors);

  const node = {
    nodeId: prismaNode.nodeEUI,
    name: prismaNode.name || "",
    location: prismaNode.locationName || "",
    gatewayUID,
    lastActivity: prismaNode.lastSeenAt?.toString() || "",
    bars: LoraSignalMetricsToSignalStrengths(),
    temperature: asNumber(findReading(map, NodeSensorTypeNames.TEMPERATURE)),
    humidity: asNumber(findReading(map, NodeSensorTypeNames.HUMIDITY)),
    // todo - scaling should be driven by the sensor reading schema, but for now hard-coding to 100
    pressure: downscale(
      asNumber(findReading(map, NodeSensorTypeNames.AIR_PRESSURE)),
      100
    ),
    voltage: asNumber(findReading(map, NodeSensorTypeNames.VOLTAGE)),
    total: asNumber(findReading(map, NodeSensorTypeNames.PIR_MOTION_TOTAL)),
    count: asNumber(findReading(map, NodeSensorTypeNames.PIR_MOTION)),
    doorStatus:
      asString(findReading(map, NodeSensorTypeNames.DOOR_STATUS)) || "",
  };

  const rssi = asNumber(
    findReading(map, NodeSensorTypeNames.LORA_SIGNAL_STRENGTH)
  );
  if (rssi !== undefined) {
    node.bars = LoraSignalMetricsToSignalStrengths({ rssi });
  }

  if (node.temperature === undefined) delete node.temperature;
  if (node.humidity === undefined) delete node.humidity;
  if (node.pressure === undefined) delete node.pressure;
  if (node.voltage === undefined) delete node.voltage;
  if (node.total === undefined) delete node.total;
  if (node.count === undefined) delete node.count;

  return node as Node;
}

export const DEFAULT = {
  sparrowGatewayFromPrismaGateway,
  sparrowNodeFromPrismaNode,
};
export default DEFAULT;
