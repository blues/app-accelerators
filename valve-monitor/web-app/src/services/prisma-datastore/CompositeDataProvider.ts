/* eslint-disable @typescript-eslint/no-unsafe-assignment */
/* eslint-disable @typescript-eslint/no-unsafe-member-access */
import { uniqBy } from "lodash";
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

    // fetch any env vars for each device
    const deviceEnvVars = await Promise.all(
      valveMonitorDevices.map((device) =>
        this.getDeviceEnvVars(device.deviceID)
      )
    );

    // filter out any devices not assigned to fleets
    const devicesWithFleets = valveMonitorDevices.filter(
      (device) => device.deviceFleetID !== undefined
    );

    // fetch fleet env vars
    let fleetEnvVars: FleetEnvVars[];
    if (devicesWithFleets.length > 0) {
      fleetEnvVars = await Promise.all(
        devicesWithFleets.map((device) =>
          this.getFleetEnvVars(device.deviceFleetID)
        )
      );
    }

    // combine devices with any env vars
    const normalizedDeviceData = valveMonitorDevices.map((device) => {
      const filteredDeviceEnvVars = deviceEnvVars.filter(
        (deviceEnvVar) => deviceEnvVar.deviceID === device.deviceID
      );

      // extract any device env vars
      const deviceMonitorInterval =
        filteredDeviceEnvVars[0].environment_variables.monitor_interval;

      const deviceMinFlowThreshold =
        filteredDeviceEnvVars[0].environment_variables
          .flow_rate_alarm_threshold_min;

      const deviceMaxFlowThreshold =
        filteredDeviceEnvVars[0].environment_variables
          .flow_rate_alarm_threshold_max;

      const filteredFleetEnvVars = uniqBy(fleetEnvVars, "fleetUID").filter(
        (fleetEnvVar) => fleetEnvVar.fleetUID === device.deviceFleetID
      );

      // extract any fleet env vars
      const fleetMonitorInterval =
        filteredFleetEnvVars[0] &&
        filteredFleetEnvVars[0].environment_variables.monitor_interval
          ? filteredFleetEnvVars[0].environment_variables.monitor_interval
          : undefined;

      const fleetMinFlowThreshold =
        filteredFleetEnvVars[0] &&
        filteredFleetEnvVars[0].environment_variables
          .flow_rate_alarm_threshold_min
          ? filteredFleetEnvVars[0].environment_variables
              .flow_rate_alarm_threshold_min
          : undefined;

      const fleetMaxFlowThreshold =
        filteredFleetEnvVars[0] &&
        filteredFleetEnvVars[0].environment_variables
          .flow_rate_alarm_threshold_max
          ? filteredFleetEnvVars[0].environment_variables
              .flow_rate_alarm_threshold_max
          : undefined;

      // device env vars trump fleet env vars
      const monitorFrequency =
        deviceMonitorInterval || fleetMonitorInterval || "x";

      const minFlowThreshold =
        deviceMinFlowThreshold || fleetMinFlowThreshold || "xx.x";

      const maxFlowThreshold =
        deviceMaxFlowThreshold || fleetMaxFlowThreshold || "xx.x";

      const formattedDeviceObj = {
        ...device,
        deviceAlarm: device.deviceAlarm ? "!" : "-",
        monitorFrequency,
        minFlowThreshold,
        maxFlowThreshold,
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
