import { BulkImport, DataProvider } from "../DataProvider";
import NotehubDataProvider from "../notehub/NotehubDataProvider";
import { PrismaDataProvider } from "./PrismaDataProvider";
import { Device, DeviceID, Event, Project } from "../DomainModel";
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

  async doBulkImport(): Promise<BulkImport> {
    const b = await this.prismaDataProvider.doBulkImport(
      this.notehubAccessor,
      this.eventHandler
    );
    return b;
  }
}
