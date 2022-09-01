import * as DomainModel from "./DomainModel";

export type ProjectID = DomainModel.ProjectID;
export type DeviceID = DomainModel.DeviceID;

/**
 * The presentation models here are json-serializable representations of the domain model.
 */

export interface Project extends DomainModel.Project {

  devices: Device[] | null; // null when devices are not required
}

/**
 * Dates are in UTC time, expressed as seconds since the epoch.
 */
export type AppDate = number;

export interface Device extends DomainModel.Device {
}

export interface ProjectDataSnapshot {
  when: AppDate;
  project: Project;
}

export interface BulkDataImportStatus {
  err?: string;
  importedItemCount: number;
  erroredItemCount: number;
  elapsedTimeMs: number;
  state: "unstarted" | "ongoing" | "done" | "failed";
}
