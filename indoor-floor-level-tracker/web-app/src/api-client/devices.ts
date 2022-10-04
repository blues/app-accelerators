/* eslint-disable import/prefer-default-export */
import axios, { AxiosResponse } from "axios";
import { DeviceTracker } from "../services/AppModel";
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

export async function changeDeviceName(deviceUID: string, name: string) {
  const endpoint = services().getUrlManager().deviceNameUpdate(deviceUID);
  const postBody = { name };
  const response: AxiosResponse = await axios.post(endpoint, postBody);
  return response.status === 200;
}
