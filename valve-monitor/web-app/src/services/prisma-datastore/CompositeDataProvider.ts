import { DataProvider } from "../DataProvider";
import NotehubDataProvider from "../notehub/NotehubDataProvider";
import { PrismaDataProvider } from "./PrismaDataProvider";
import {
  Device,
  DeviceEnvVars,
  DeviceID,
  Event,
  FleetEnvVars,
  Fleets,
  Project,
} from "../DomainModel";
import { ValveMonitorDevice } from "../AppModel";
import { NotehubAccessor } from "../notehub/NotehubAccessor";
import { AppEventHandler } from "../AppEvent";

export default class CompositeDataProvider implements DataProvider {
  constructor(
    private eventHandler: AppEventHandler,
    private notehubAccessor: NotehubAccessor,
    private notehubProvider: NotehubDataProvider,
    private prismaDataProvider: PrismaDataProvider
  ) {}

  async getProject(): Promise<Project> {
    return this.prismaDataProvider.getProject();
  }

  getDevices(): Promise<Device[]> {
    return this.prismaDataProvider.getDevices();
  }

  getDevice(deviceID: DeviceID): Promise<Device | null> {
    return this.prismaDataProvider.getDevice(deviceID);
  }

  async getValveMonitorDeviceData(): Promise<ValveMonitorDevice[]> {
    // get as much device data as is available in Prisma
    const valveMonitorDevices =
      await this.prismaDataProvider.getValveMonitorDeviceData();

    // fetch env vars for each device
    const deviceEnvVars = await Promise.all(
      valveMonitorDevices.map((device) =>
        this.getDeviceEnvVars(device.deviceID)
      )
    );

    // combine devices with any env vars
    const normalizedDeviceData = valveMonitorDevices.map((device) => {
      const filteredDeviceEnvVars = deviceEnvVars.filter(
        (deviceEnvVar) => deviceEnvVar.deviceID === device.deviceID
      );

      // todo clean this up
      const formattedDeviceObj = {
        ...device,
        deviceAlarm: !device.deviceAlarm ? `-` : `!`,
        deviceFlowRateFrequency: filteredDeviceEnvVars[0].environment_variables
          .monitor_interval
          ? filteredDeviceEnvVars[0].environment_variables.monitor_interval
          : `-`,
        deviceAlarmThreshold: filteredDeviceEnvVars[0].environment_variables
          .flow_rate_alarm_threshold
          ? filteredDeviceEnvVars[0].environment_variables
              .flow_rate_alarm_threshold
          : `-`,
      };

      return formattedDeviceObj;
    });

    return normalizedDeviceData;
  }

  getDeviceEvents(deviceIDs: string[]): Promise<Event[]> {
    return this.prismaDataProvider.getDeviceEvents(deviceIDs);
  }

  getFleetsByProject(): Promise<Fleets> {
    return this.notehubProvider.getFleetsByProject();
  }

  getDevicesByFleet(fleetUID: string): Promise<Device[]> {
    return this.notehubProvider.getDevicesByFleet(fleetUID);
  }

  getFleetsByDevice(deviceID: string): Promise<Fleets> {
    return this.notehubProvider.getFleetsByDevice(deviceID);
  }

  getDeviceEnvVars(deviceID: string): Promise<DeviceEnvVars> {
    return this.notehubProvider.getDeviceEnvVars(deviceID);
  }

  getFleetEnvVars(fleetUID: string): Promise<FleetEnvVars> {
    return this.notehubProvider.getFleetEnvVars(fleetUID);
  }

  getValveMonitorConfig(fleetUID: string) {
    return this.notehubProvider.getValveMonitorConfig(fleetUID);
  }
}
