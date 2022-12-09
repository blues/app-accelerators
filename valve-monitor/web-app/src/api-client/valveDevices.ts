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
  const devices = response.data.valveMonitorDevices as ValveMonitorDevice[];
  return devices.sort((a, b) => a.name.localeCompare(b.name));
}

async function updateDevice(deviceUID: string, deviceEnvVarToUpdate: object) {
  const endpoint = services()
    .getUrlManager()
    .updateValveMonitorDevice(deviceUID);

  const response = await axios.post<object>(endpoint, deviceEnvVarToUpdate);

  return response.data;
}

export async function updateDeviceValveMonitorConfig(
  deviceUID: string,
  valveDeviceEnvVarToUpdate: object
) {
  const response = await updateDevice(deviceUID, {
    valveMonitorConfig: valveDeviceEnvVarToUpdate,
  });

  return response;
}

export async function changeDeviceName(deviceUID: string, name: string) {
  const response = await updateDevice(deviceUID, {
    name,
  });

  return response;
}

export async function updateValveControl(deviceUID: string, state: string) {
  const response = await updateDevice(deviceUID, {
    state,
  });

  return response;
}
