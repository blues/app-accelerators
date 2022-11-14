import Prisma from "@prisma/client";
import * as DomainModel from "../DomainModel";
import IDBuilder from "../IDBuilder";

/**
 * Maps between prisma instances generated from the schema, and domain model instances.
 */
export interface PrismaDomainModelMapper {
  mapProject(data: Prisma.Project): DomainModel.Project;
  mapDevice(data: Prisma.Device): DomainModel.Device;
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

  mapDevice(data: Prisma.Device) {
    return {
      id: IDBuilder.buildDeviceID(data.deviceUID),
      name: data.name,
      lastSeen: this.mapDateNull(data.lastSeenAt),
      locationName: data.locationName,
      descriptionBig: null,
      descriptionSmall: null,
    };
  }
}

const mapper = new DefaultPrismaDomainModelMapper();

export default mapper;
