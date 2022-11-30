/* eslint-disable import/prefer-default-export */
import axios, { AxiosResponse } from "axios";
import { ValveMonitorDevice } from "../services/AppModel";
import { services } from "../services/ServiceLocatorClient";

type GetValveMonitorDeviceResponse = {
  valveMonitorDevices: ValveMonitorDevice[];
};

export async function getValveMonitorDeviceData(): Promise<
  ValveMonitorDevice[]
> {
  const endpoint = services().getUrlManager().getValveMonitorDeviceData();
  const response: AxiosResponse =
    await axios.get<GetValveMonitorDeviceResponse>(endpoint);
  return response.data.valveMonitorDevices;
}

export async function changeDeviceName(deviceUID: string, name: string) {
  const endpoint = services().getUrlManager().deviceNameUpdate(deviceUID);
  const postBody = { name };
  const response: AxiosResponse = await axios.post(endpoint, postBody);
  return response.status === 200;
}
