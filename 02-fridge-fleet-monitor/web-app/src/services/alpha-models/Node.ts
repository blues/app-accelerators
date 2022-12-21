import { SignalStrengths } from "./SignalStrengths";

interface Node {
  name?: string;
  nodeId: string;
  location?: string;
  humidity?: number;
  pressure?: number;
  temperature?: number;
  voltage?: number;
  lastActivity: string;
  doorStatus?: string;
  gatewayUID: string;
  /**
   * The signal strength for this node - lora bars.
   */
  bars: SignalStrengths;
}

export default Node;
