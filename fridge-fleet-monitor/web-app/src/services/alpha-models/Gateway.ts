import { SignalStrengths } from "./SignalStrengths";
import Node from "./Node";

interface Gateway {
  /**
   * The unique identifier for this gateway
   */
  uid: string;

  /**
   * The name for this gateway.
   */
  name: string;
  lastActivity: string;
  location?: string;
  /**
   * The signal strength for this gateway - either cell bars or wifi bars.
   */
  cellBars?: SignalStrengths;
  cellIconPath?: string | null;
  cellTooltip?: string | null;
  wifiBars?: SignalStrengths;
  wifiIconPath?: string | null;
  wifiTooltip?: string | null;

  nodeList: Node[];
}

export default Gateway;
