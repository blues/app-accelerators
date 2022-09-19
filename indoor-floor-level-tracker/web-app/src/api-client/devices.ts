/* eslint-disable import/prefer-default-export */
import axios, { AxiosResponse } from "axios";
import { DeviceTracker } from "../services/ClientModel";
import { services } from "../services/ServiceLocatorClient";

type GetDeviceTrackerResponse = {
  deviceTrackers: DeviceTracker[];
};

export async function getDeviceTrackerData(): Promise<DeviceTracker[]> {
  const endpoint = services().getUrlManager().getDeviceTrackerData();
  const response: AxiosResponse = await axios.get<GetDeviceTrackerResponse>(
    endpoint
  );
  return response.data.deviceTrackers;
}
