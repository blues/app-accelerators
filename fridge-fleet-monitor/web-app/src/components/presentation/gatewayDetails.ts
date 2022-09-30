/* eslint-disable @typescript-eslint/no-unsafe-assignment */
import {
  calculateCellSignalStrength,
  calculateSignalTooltip,
  calculateWifiSignalStrength,
  getFormattedLastSeen,
} from "./uiHelpers";
import { GATEWAY_MESSAGE } from "../../constants/ui";
import Gateway from "../../services/alpha-models/Gateway";
import GatewayDetailViewModel from "../../models/GatewayDetailViewModel";
import Node from "../../services/alpha-models/Node";
import Config from "../../../config";

// eslint-disable-next-line import/prefer-default-export
export function getGatewayDetailsPresentation(
  gateway?: Gateway,
  nodes?: Node[]
): GatewayDetailViewModel {
  return {
    gateway: gateway
      ? {
          uid: gateway.uid || "",
          lastActivity: getFormattedLastSeen(gateway.lastActivity || ""),
          location: gateway.location || GATEWAY_MESSAGE.NO_LOCATION,
          name: gateway.name || GATEWAY_MESSAGE.NO_NAME,
          ...(gateway.cellBars && { cellBars: gateway.cellBars }),
          ...(gateway.cellBars
            ? {
                cellBarsIconPath: calculateCellSignalStrength(gateway.cellBars),
              }
            : {
                cellBarsIconPath: calculateCellSignalStrength("N/A"),
              }),
          ...(gateway.cellBars && {
            cellBarsTooltip: calculateSignalTooltip(gateway.cellBars),
          }),

          ...(gateway.wifiBars && { wifiBars: gateway.wifiBars }),
          ...(gateway.wifiBars
            ? {
                wifiBarsIconPath: calculateWifiSignalStrength(gateway.wifiBars),
              }
            : { wifiBarsIconPath: calculateCellSignalStrength("N/A") }),
          ...(gateway.wifiBars && {
            wifiBarsTooltip: calculateSignalTooltip(gateway.wifiBars),
          }),
        }
      : undefined,
    nodes,
    readOnly: Config.readOnly,
  };
}
