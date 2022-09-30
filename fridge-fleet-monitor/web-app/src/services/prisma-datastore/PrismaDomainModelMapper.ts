import Prisma from "@prisma/client";
import * as DomainModel from "../DomainModel";
import IDBuilder from "../IDBuilder";

/**
 * Maps between prisma instances generated from the schema, and domain model instances.
 */
export interface PrismaDomainModelMapper {
  mapProjectHierarchy(
    prismaProject: Prisma.Project,
    gateways: Set<DomainModel.GatewayWithNodes>
  ): DomainModel.ProjectHierarchy;
  mapProject(data: Prisma.Project): DomainModel.Project;
  mapGateway(data: Prisma.Gateway): DomainModel.Gateway;
  mapGatewayWithNodes(
    data: Prisma.Gateway,
    nodes: DomainModel.Nodes
  ): DomainModel.GatewayWithNodes;
  mapNode(data: Prisma.Node): DomainModel.Node;
  mapReadingSchema(data: Prisma.ReadingSchema): DomainModel.SensorType;
  mapReading(data?: Prisma.Reading): DomainModel.Reading;
}

class DefaultPrismaDomainModelMapper {
  mapDateNull(date: Date | null): number | null {
    return date == null ? date : date.getTime() / 1000;
  }
  mapDate(date: Date): number {
    return date.getTime() / 1000;
  }

  mapJSONValue(data: Prisma.Prisma.JsonValue): DomainModel.JSONValue {
    return data as DomainModel.JSONValue; // assume there are no null values, since we never put any in.
  }

  mapProject(data: Prisma.Project) {
    return {
      id: IDBuilder.buildProjectID(data.projectUID),
      name: data.name,
      description: null,
    };
  }

  mapProjectHierarchy(
    data: Prisma.Project,
    gateways: Set<DomainModel.GatewayWithNodes>
  ) {
    return { ...this.mapProject(data), gateways };
  }

  mapGateway(data: Prisma.Gateway) {
    return {
      id: IDBuilder.buildGatewayID(data.deviceUID),
      name: data.name,
      lastSeen: this.mapDateNull(data.lastSeenAt),
      locationName: data.locationName,
      descriptionBig: null,
      descriptionSmall: null,
    };
  }

  mapGatewayWithNodes(data: Prisma.Gateway, nodes: DomainModel.Nodes) {
    return { ...this.mapGateway(data), nodes };
  }

  mapNode(data: Prisma.Node): DomainModel.Node {
    return {
      id: IDBuilder.buildNodeID(data.nodeEUI),
      name: data.name,
      descriptionSmall: data.label,
      descriptionBig: data.comment,
      lastSeen: this.mapDateNull(data.lastSeenAt),
      locationName: data.locationName,
    };
  }

  mapReadingSchema(data: Prisma.ReadingSchema): DomainModel.SensorType {
    return {
      id: IDBuilder.buildSensorTypeID(data.name),
      name: data.name,
      measure: data.measure,
      displayName: data.displayName || data.name,
      displayMeasure: data.displayMeasure || data.measure,
      spec: this.mapJSONValue(data.spec),
      unit: data.unit || "", // todo - should the data be non-null?
      unitSymbol: data.unitSymbol || "",
    };
  }

  // maydo(optimize) - make reading compatible in the db layer for less copying of values.
  mapReading(data: Prisma.Reading): DomainModel.Reading {
    return {
      when: this.mapDate(data.when),
      value: this.mapJSONValue(data.value),
    };
  }
}

const mapper = new DefaultPrismaDomainModelMapper();

export default mapper;
