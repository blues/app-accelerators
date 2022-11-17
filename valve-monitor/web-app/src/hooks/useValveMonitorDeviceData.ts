import { useQuery } from "react-query";
import { ValveMonitorDevice } from "../services/AppModel";

export function useValveMonitorDeviceData(refetchInterval?: number) {
  return useQuery<ValveMonitorDevice[], Error>(
    "getValveMonitorDeviceData",
    () => getValveMonitorDeviceData(),
    {
      refetchInterval,
    }
  );
}
