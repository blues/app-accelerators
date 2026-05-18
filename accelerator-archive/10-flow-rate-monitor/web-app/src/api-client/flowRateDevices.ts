/* eslint-disable import/prefer-default-export */
import axios, { AxiosResponse } from "axios";
import { FlowRateMonitorDevice } from "../services/AppModel";
import { services } from "../services/ServiceLocatorClient";

type GetFlowRateMonitorDeviceResponse = {
  flowRateMonitorDevices: FlowRateMonitorDevice[];
};

export async function getFlowRateMonitorDeviceData(): Promise<
  FlowRateMonitorDevice[]
> {
  const endpoint = services().getUrlManager().getFlowRateMonitorDeviceData();
  const response: AxiosResponse =
    await axios.get<GetFlowRateMonitorDeviceResponse>(endpoint);
  const devices = response.data
    .flowRateMonitorDevices as FlowRateMonitorDevice[];
  return devices.sort((a, b) => a.name.localeCompare(b.name));
}

async function updateDevice(deviceUID: string, deviceEnvVarToUpdate: object) {
  const endpoint = services()
    .getUrlManager()
    .updateFlowRateMonitorDevice(deviceUID);

  const response = await axios.post<object>(endpoint, deviceEnvVarToUpdate);

  return response.data;
}

export async function updateDeviceFlowRateMonitorConfig(
  deviceUID: string,
  flowRateDeviceEnvVarToUpdate: object
) {
  const response = await updateDevice(deviceUID, {
    flowRateMonitorConfig: flowRateDeviceEnvVarToUpdate,
  });

  return response;
}

export async function changeDeviceName(deviceUID: string, name: string) {
  const response = await updateDevice(deviceUID, {
    name,
  });

  return response;
}
