/* eslint-disable import/prefer-default-export */
import axios, { AxiosResponse } from "axios";
import { useQuery } from "react-query";
import { DeviceTracker } from "../services/ClientModel";
import { services } from "../services/ServiceLocatorClient";

async function getDeviceTrackerData() {
  const endpoint = services().getUrlManager().getDeviceTrackerData();
  const response: AxiosResponse = await axios.get(endpoint);
  return response.data.deviceTrackers;
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
