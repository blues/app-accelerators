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

async function updateDeviceEnvVar(
  deviceUID: string,
  deviceEnvVarToUpdate: object
) {
  const endpoint = services()
    .getUrlManager()
    .setDeviceValveMonitorConfig(deviceUID);

  const response = await axios.post<object>(endpoint, deviceEnvVarToUpdate);

  return response.data;
}

export async function updateDeviceValveMonitorFrequency(
  deviceUID: string,
  monitorFrequency: number
) {
  const response = await updateDeviceEnvVar(deviceUID, {
    monitor_frequency: monitorFrequency,
  });
  return response;
}
