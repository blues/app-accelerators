import { PrismaClient } from "@prisma/client";
import { AttributeStore } from "../AttributeStore";
import { Device, DeviceID } from "../DomainModel";
import { PrismaDataProvider } from "./PrismaDataProvider";

type HasPin = { pin: string | null };
export const MAX_PIN_LENGTH = 20;

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


  /**
   * Validates that the pin is correct. Either the device has a pin and the pin must match, or the pin is defined.
   * @param device
   * @param pin
   * @returns
   */
  // eslint-disable-next-line class-methods-use-this
  validatePin(device: HasPin, pin: string): boolean {
    return !!device && !!pin && (!device?.pin || device?.pin === pin);
  }

  async setDevicePin(deviceUID: string, pin: string) {
    await this.prisma.device.update({
      where: {
        deviceUID,
      },
      data: {
        pin,
      },
    });
  }

  async updateDevicePin(
    deviceID: DeviceID,
    pin: string
  ): Promise<Device | null> {
    if (!pin || pin.length > MAX_PIN_LENGTH) {
      return null;
    }

    const device = await this.dataProvider.fetchDevice(deviceID);
    if (device && this.validatePin(device, pin)) {
      if (!device.pin) {
        await this.setDevicePin(device.deviceUID, pin);
      }
      return this.dataProvider.deviceFromPrismaDevice(device);
    }
    return null;
  }
}
