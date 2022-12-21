import { PrismaClient } from "@prisma/client";
import { AttributeStore, GatewayOrNode } from "../AttributeStore";
import { PrismaDataProvider } from "./PrismaDataProvider";

type HasPin = { pin: string | null };
export const MAX_PIN_LENGTH = 20;

/**
 * This class wraps a store to also populate
 */
export default class PrismaAttributeStore implements AttributeStore {
  // todo we don't need the full prisma provider interface, just fetchNode/fetch
  constructor(
    private prisma: PrismaClient,
    private dataProvider: PrismaDataProvider
  ) {}

  // todo - we should probably verify against the project ID?
  async updateGatewayName(gatewayUID: string, name: string): Promise<void> {
    await this.prisma.gateway.update({
      where: {
        deviceUID: gatewayUID,
      },
      data: {
        name,
      },
    });
  }

  async updateNodeName(
    gatewayUID: string,
    nodeId: string,
    name: string
  ): Promise<void> {
    await this.prisma.node.update({
      where: {
        nodeEUI: nodeId,
      },
      data: {
        name,
      },
    });
  }

  async updateNodeLocation(
    gatewayUID: string,
    nodeId: string,
    location: string
  ): Promise<void> {
    await this.prisma.node.update({
      where: {
        nodeEUI: nodeId,
      },
      data: {
        locationName: location,
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

  async updateNodePin(nodeEUI: string, pin: string) {
    await this.prisma.node.update({
      where: {
        nodeEUI,
      },
      data: {
        pin,
      },
    });
  }

  async updateGatewayPin(deviceUID: string, pin: string) {
    await this.prisma.gateway.update({
      where: {
        deviceUID,
      },
      data: {
        pin,
      },
    });
  }

  async updateDevicePin(
    gatewayUID: string,
    sensorUID: string,
    pin: string
  ): Promise<GatewayOrNode | null> {
    if (!pin || pin.length > MAX_PIN_LENGTH) {
      return null;
    }

    if (sensorUID) {
      const node = await this.dataProvider.fetchNode(sensorUID);
      if (node && this.validatePin(node, pin)) {
        // update the pin if it is needed
        if (!node.pin) {
          await this.updateNodePin(node.nodeEUI, pin);
        }
        return { gatewayUID: node.gateway.deviceUID, nodeID: node.nodeEUI };
      }
    } else {
      const gateway = await this.dataProvider.fetchGateway(gatewayUID);
      if (gateway && this.validatePin(gateway, pin)) {
        if (!gateway.pin) {
          await this.updateGatewayPin(gateway.deviceUID, pin);
        }
        return { gatewayUID: gateway.deviceUID };
      }
    }
    return null;
  }
}
