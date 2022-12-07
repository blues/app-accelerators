import { PrismaClient } from "@prisma/client";
import { ValveMonitorConfig } from "../AppModel";
import { AttributeStore } from "../AttributeStore";
import { DeviceID, FleetID } from "../DomainModel";
import { PrismaDataProvider } from "./PrismaDataProvider";

// Generate a UUID
// https://stackoverflow.com/questions/105034/how-do-i-create-a-guid-uuid
function uuidv4() {
  return "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".replace(/[xy]/g, function (c) {
    var r = (Math.random() * 16) | 0,
      v = c == "x" ? r : (r & 0x3) | 0x8;
    return v.toString(16);
  });
}

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

  async updateValveState(deviceUID: string, state: string) {
    // Store a data.qi in the DB so we know that we requested a valve
    // state change.
    await this.prisma.event.create({
      data: {
        eventName: "data.qi",
        eventUID: uuidv4(),
        deviceUID,
        value: { state },
        when: new Date(),
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
