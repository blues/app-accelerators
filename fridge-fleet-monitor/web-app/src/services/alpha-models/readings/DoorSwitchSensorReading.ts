import Reading from "./Reading";
import DoorSwitchSensorSchema from "./TemperatureSensorSchema";

class DoorSwitchSensorReading implements Reading<string> {
  schema: DoorSwitchSensorSchema;

  value: string;

  captured: string;

  constructor(options: { value: string; captured: string }) {
    this.schema = DoorSwitchSensorSchema;
    this.value = options.value;
    this.captured = options.captured;
  }
}

export default DoorSwitchSensorReading;
