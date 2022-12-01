import { PrismaClient } from "@prisma/client";
import { ValveMonitorConfig } from "../AppModel";
import { AttributeStore } from "../AttributeStore";
import { DeviceID, FleetID } from "../DomainModel";
import { PrismaDataProvider } from "./PrismaDataProvider";

/**
 * This class wraps a store to also populate
 */
export default class PrismaAttributeStore implements AttributeStore {
  // todo we don't need the full prisma provider interface, just fetchDevice
  constructor(
    private prisma: PrismaClient,
    private dataProvider: PrismaDataProvider
  ) {}

  async updateDeviceName(deviceID: DeviceID, name: string): Promise<void> {
    await this.prisma.device.update({
      where: {
        deviceUID: deviceID.deviceUID,
      },
      data: {
        name,
      },
    });
  }

  updateDeviceValveMonitorConfig(
    deviceUID: string,
    valveMonitorConfig: ValveMonitorConfig
  ) {
    // No action necessary as we don’t store environment variables in the
    // database
    return Promise.resolve();
  }

  updateValveMonitorConfig(
    fleetUID: FleetID,
    valveMonitorConfig: ValveMonitorConfig
  ) {
    // No action necessary as we don’t store environment variables in the
    // database
    return Promise.resolve();
  }
}
