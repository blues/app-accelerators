/* eslint-disable import/prefer-default-export */
import { useQuery } from "react-query";
import { FlowRateMonitorDevice } from "../services/AppModel";
import { getFlowRateMonitorDeviceData } from "../api-client/flowRateDevices";

export function useFlowRateMonitorDeviceData(refetchInterval?: number) {
  return useQuery<FlowRateMonitorDevice[], Error>(
    "getFlowRateMonitorDeviceData",
    () => getFlowRateMonitorDeviceData(),
    {
      refetchInterval,
    }
  );
}
