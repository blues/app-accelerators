/* eslint-disable import/prefer-default-export */
import { useQuery } from "react-query";
import { DeviceTracker } from "../services/AppModel";
import { getDeviceTrackerData } from "../api-client/devices";

export function useDeviceTrackerData(refetchInterval?: number) {
  return useQuery<DeviceTracker[], Error>(
    "getDeviceTrackerData",
    () => getDeviceTrackerData(),
    {
      refetchInterval,
    }
  );
}
