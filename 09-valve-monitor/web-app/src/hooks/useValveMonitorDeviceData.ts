/* eslint-disable import/prefer-default-export */
import { useQuery } from "react-query";
import { ValveMonitorDevice } from "../services/AppModel";
import { getValveMonitorDeviceData } from "../api-client/valveDevices";

export function useValveMonitorDeviceData(refetchInterval?: number) {
  return useQuery<ValveMonitorDevice[], Error>(
    "getValveMonitorDeviceData",
    () => getValveMonitorDeviceData(),
    {
      refetchInterval,
    }
  );
}
