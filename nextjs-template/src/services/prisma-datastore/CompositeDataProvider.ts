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
}
