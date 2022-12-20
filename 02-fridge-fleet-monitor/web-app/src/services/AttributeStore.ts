export type GatewayOrNode = {
  gatewayUID: string;
  nodeID?: string;
};

export interface AttributeStore {
  updateGatewayName: (gatewayUID: string, name: string) => Promise<void>;
  updateNodeName: (
    gatewayUID: string,
    nodeId: string,
    name: string
  ) => Promise<void>;
  updateNodeLocation: (
    gatewayUID: string,
    nodeId: string,
    location: string
  ) => Promise<void>;

  /**
   * Update the pin of the device identified by the given deviceUID.
   * @returns `null` if the device is not found, or the pin is incorrect, otherwise returns the
   * the deviceID. For nodes, this includes the corresponding gatewayID.
   */
  updateDevicePin: (
    gatewayUID: string,
    sensorUID: string,
    pin: string
  ) => Promise<GatewayOrNode | null>;
}
