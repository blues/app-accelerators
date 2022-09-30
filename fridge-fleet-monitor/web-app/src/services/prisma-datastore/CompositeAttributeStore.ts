import { AttributeStore, GatewayOrNode } from "../AttributeStore";

// would be nice to generify this and implement as a generic proxy
export default class CompositeAttributeStore implements AttributeStore {
  constructor(private stores: AttributeStore[]) {}

  // Retrieves a promise that is resolved when all delegates have completed
  private apply<T>(fn: (store: AttributeStore) => Promise<T>): Promise<T> {
    const all = this.stores.map((store) => fn(store));
    return Promise.all(all).then();
  }

  updateGatewayName(gatewayUID: string, name: string): Promise<void> {
    return this.apply((store) => store.updateGatewayName(gatewayUID, name));
  }

  updateNodeName(
    gatewayUID: string,
    nodeId: string,
    name: string
  ): Promise<void> {
    return this.apply((store) =>
      store.updateNodeName(gatewayUID, nodeId, name)
    );
  }

  updateNodeLocation(
    gatewayUID: string,
    nodeId: string,
    location: string
  ): Promise<void> {
    return this.apply((store) =>
      store.updateNodeLocation(gatewayUID, nodeId, location)
    );
  }

  updateDevicePin(
    gatewayUID: string,
    sensorUID: string,
    pin: string
  ): Promise<GatewayOrNode | null> {
    return this.apply((store) =>
      store.updateDevicePin(gatewayUID, sensorUID, pin)
    );
  }
}
