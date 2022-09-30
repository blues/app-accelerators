import { ProjectID, GatewayID, NodeID, SensorTypeID } from "./DomainModel";

export interface IDBuilder {
  buildProjectID(projectUID: string): ProjectID;
  buildGatewayID(gatewayDeviceUID: string): GatewayID;
  buildNodeID(nodeID: string): NodeID;
  buildSensorTypeID(readingSchemaName: string): SensorTypeID;
}

class SimpleProjectID implements ProjectID {
  constructor(
    public readonly projectUID: string,
    public readonly type: "ProjectID" = "ProjectID"
  ) {}
}

class SimpleGatewayID implements GatewayID {
  constructor(
    public readonly gatewayDeviceUID: string,
    public readonly type: "GatewayID" = "GatewayID"
  ) {}
}

class SimpleNodeID implements NodeID {
  constructor(
    public readonly nodeID: string,
    public readonly type: "NodeID" = "NodeID"
  ) {}
}

class SimpleSensorTypeID implements SensorTypeID {
  constructor(
    public readonly uuid: string,
    public readonly type: "SensorTypeID" = "SensorTypeID"
  ) {}
}

export class SimpleIDBuilder implements IDBuilder {
  buildSensorTypeID(uuid: string): SensorTypeID {
    return { ...new SimpleSensorTypeID(uuid) };
  }

  buildNodeID(nodeEUI: string): NodeID {
    return { ...new SimpleNodeID(nodeEUI) };
  }

  buildGatewayID(gatewayDeviceUID: string): GatewayID {
    return { ...new SimpleGatewayID(gatewayDeviceUID) };
  }

  buildProjectID(projectUID: string): ProjectID {
    return { ...new SimpleProjectID(projectUID) };
  }
}

const builder = new SimpleIDBuilder();

export default builder;
