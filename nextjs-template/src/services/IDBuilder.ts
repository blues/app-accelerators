import { ProjectID, DeviceID } from "./DomainModel";

export interface IDBuilder {
  buildProjectID(projectUID: string): ProjectID;
  buildDeviceID(deviceUID: string): DeviceID;
}

class SimpleProjectID implements ProjectID {
  constructor(
    public readonly projectUID: string,
    public readonly type: "ProjectID" = "ProjectID"
  ) {}
}

class SimpleDeviceID implements DeviceID {
  constructor(
    public readonly deviceUID: string,
    public readonly type: "DeviceID" = "DeviceID"
    ) {}
}

export class SimpleIDBuilder implements IDBuilder {

  buildDeviceID(deviceUID: string): DeviceID {
    return { ...new SimpleDeviceID(deviceUID) };
  }

  buildProjectID(projectUID: string): ProjectID {
    return { ...new SimpleProjectID(projectUID) };
  }
}

const builder = new SimpleIDBuilder();

export default builder;
