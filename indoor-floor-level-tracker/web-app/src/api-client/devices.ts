import axios, { AxiosResponse } from "axios";
import { useQuery } from "react-query";
import { DeviceTracker } from "../services/ClientModel";
import { services } from "../services/ServiceLocatorClient";

async function getDeviceTrackerData() {
  const endpoint = services().getUrlManager().getDeviceTrackerData();
  const response: AxiosResponse = await axios.get(endpoint);
  console.log("Fetching tracker data", response.data);
  return response.data as DeviceTracker;
}

export function useDeviceTrackerData(refetchInterval?: number) {
  return useQuery<DeviceTracker, Error>(
    "getDeviceTrackerData",
    () => getDeviceTrackerData(),
    {
      refetchInterval,
    }
  );
}
