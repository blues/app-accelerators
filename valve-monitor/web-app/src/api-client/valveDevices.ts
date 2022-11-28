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
  return response.data;
}
