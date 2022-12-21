/* eslint-disable @typescript-eslint/no-unsafe-assignment */
import Node from "../../services/alpha-models/Node";
import Gateway from "../../services/alpha-models/Gateway";
import {
  calculateCellSignalStrength,
  calculateSignalTooltip,
  calculateWifiSignalStrength,
} from "./uiHelpers";

// eslint-disable-next-line import/prefer-default-export
export function getCombinedGatewayNodeInfo(
  latestNodeDataList: Node[],
  gateways: Gateway[]
): Gateway[] {
  const gatewayNodeInfo = gateways.map((gateway) => {
    const filterNodesByGateway = latestNodeDataList.filter(
      (node) => node.gatewayUID === gateway.uid
    );
    const updatedNodeList = {
      nodeList: filterNodesByGateway,
    };

    if (gateway.cellBars) {
      const gatewaySignalStrengthIconPath = calculateCellSignalStrength(
        gateway.cellBars
      );
      const gatewaySignalTooltip = calculateSignalTooltip(gateway.cellBars);

      // eslint-disable-next-line no-param-reassign
      gateway = {
        ...gateway,
        cellIconPath: gatewaySignalStrengthIconPath,
        cellTooltip: gatewaySignalTooltip,
      };
    } else if (gateway.wifiBars) {
      const gatewaySignalStrengthIconPath = calculateWifiSignalStrength(
        gateway.wifiBars
      );
      const gatewaySignalTooltip = calculateSignalTooltip(gateway.wifiBars);

      // eslint-disable-next-line no-param-reassign
      gateway = {
        ...gateway,
        wifiIconPath: gatewaySignalStrengthIconPath,
        wifiTooltip: gatewaySignalTooltip,
      };
    }
    const updatedGatewayObject = {
      ...gateway,
      ...updatedNodeList,
    };
    return updatedGatewayObject;
  });
  return gatewayNodeInfo;
}
