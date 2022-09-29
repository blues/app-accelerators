import { DataProvider } from "./DataProvider";
import { AttributeStore } from "./AttributeStore";
import { AppEvent, AppEventHandler } from "./AppEvent";
import { DeviceTracker, TrackerConfig } from "./ClientModel";
import { Project, Device, ProjectID, BulkDataImportStatus } from "./AppModel";
import { IDBuilder } from "./IDBuilder";
import { NotificationsStore, Notification } from "./NotificationsStore";
import { serverLogError } from "../pages/api/log";
import { AppNotification } from "../components/presentation/notifications";
import { FleetID } from "./DomainModel";

// this class / interface combo passes data and functions to the service locator file
interface AppServiceInterface {
  getEventCount: () => Promise<number>;
  getProject: () => Promise<Project>;
  getDevices: () => Promise<Device[]>;
  getDevice: (deviceUID: string) => Promise<Device | null>;
  setDeviceName: (deviceUID: string, name: string) => Promise<void>;

  getLastAlarmClear: () => string;
  setLastAlarmClear: (when: string) => void;

  handleEvent(event: AppEvent): Promise<void>;

  performBulkDataImport(): Promise<BulkDataImportStatus>;

  getAppNotifications(): Promise<AppNotification[]>;

  getDeviceTrackerData: () => Promise<DeviceTracker[]>;
  getTrackerConfig: () => Promise<TrackerConfig>;
  setTrackerConfig: (trackerConfig: TrackerConfig) => Promise<void>;
}

export type { AppServiceInterface };

export default class AppService implements AppServiceInterface {
  private projectID: ProjectID;

  private fleetID: FleetID;

  constructor(
    projectUID: string,
    fleetUID: string,
    private readonly idBuilder: IDBuilder,
    private dataProvider: DataProvider,
    private appEventHandler: AppEventHandler,
    private attributeStore: AttributeStore,
    private notificationStore: NotificationsStore
  ) {
    this.projectID = this.idBuilder.buildProjectID(projectUID);
    this.fleetID = this.idBuilder.buildFleetID(fleetUID);
  }

  // eslint-disable-next-line class-methods-use-this
  getLastAlarmClear() {
    return localStorage.getItem("LAST_ALARM_CLEAR") || "";
  }

  // eslint-disable-next-line class-methods-use-this
  setLastAlarmClear(when: string) {
    return localStorage.setItem("LAST_ALARM_CLEAR", when);
  }

  async getEventCount(): Promise<number> {
    return 0;
  }

  async getProject(): Promise<Project> {
    const project = await this.dataProvider.getProject();
    return {
      devices: null,
      ...project,
    };
  }

  async setDeviceName(deviceUID: string, name: string): Promise<void> {
    return this.attributeStore.updateDeviceName(
      this.idBuilder.buildDeviceID(deviceUID),
      name
    );
  }

  async getDevices() {
    return this.dataProvider.getDevices();
  }

  async getDevice(deviceUID: string) {
    return this.dataProvider.getDevice(this.idBuilder.buildDeviceID(deviceUID));
  }

  async handleEvent(event: AppEvent) {
    return this.appEventHandler.handleEvent(event);
  }

  private currentProjectID() {
    return this.projectID;
  }

  async performBulkDataImport(): Promise<BulkDataImportStatus> {
    const startTime = performance.now();
    try {
      const b = await this.dataProvider.doBulkImport();
      const finishTime = performance.now();
      return {
        elapsedTimeMs: finishTime - startTime,
        erroredItemCount: b.errorCount,
        importedItemCount: b.itemCount,
        state: "done",
      };
    } catch (cause) {
      const finishTime = performance.now();
      return {
        elapsedTimeMs: finishTime - startTime,
        // eslint-disable-next-line @typescript-eslint/restrict-template-expressions
        err: `Please Try Again. Cause: ${cause}`,
        erroredItemCount: 0,
        importedItemCount: 0,
        state: "failed",
      };
    }
  }

  async getAppNotifications(): Promise<AppNotification[]> {
    const notifications: Notification[] =
      await this.notificationStore.getNotifications();
    const result = (
      await Promise.all(notifications.map(async (n) => this.appNotification(n)))
    ).filter((e): e is AppNotification => e !== null);
    return result;
  }

  private async appNotification(
    notification: Notification
  ): Promise<AppNotification | null> {
    switch (true) {
    }
    serverLogError(`unknown notification ${notification}`);
    return null;
  }

  async getDeviceTrackerData() {
    return this.dataProvider.getDeviceTrackerData();
  }

  async getTrackerConfig(): Promise<TrackerConfig> {
    return this.dataProvider.getTrackerConfig();
  }

  async setTrackerConfig(trackerConfig: TrackerConfig) {
    const { fleetUID } = this.fleetID;
    return this.attributeStore.updateTrackerConfig(
      this.idBuilder.buildFleetID(fleetUID),
      trackerConfig
    );
  }
}
