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
  count?: number;
  total?: number;
  gatewayUID: string;
  doorStatus?: string;
  /**
   * The signal strength for this node - lora bars.
   */
  bars: SignalStrengths;
}

export default Node;
