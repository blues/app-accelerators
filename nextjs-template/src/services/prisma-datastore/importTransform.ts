import { Prisma, Project } from "@prisma/client";
import { Device } from "../DomainModel";

export function deviceTransformUpsert(
  { id, name, locationName, lastSeenAt: lastSeen }: Device,
  project: Project
): Prisma.DeviceUpsertArgs {
  return {
    where: {
      deviceUID: id.deviceUID,
    },
    create: {
      name,
      deviceUID: id.deviceUID,
      locationName,
      project: {
        connect: {
          id: project.id,
        },
      },
      lastSeenAt: lastSeen,
    },
    update: {
      // No-op. This device is already in the database so don't change it.
    },
  };
}

