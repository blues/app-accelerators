import Reading from "./Reading";
import VoltageSensorSchema from "./VoltageSensorSchema";

class VoltageSensorReading implements Reading<number> {
  schema: VoltageSensorSchema;

  value: number;

  captured: string;

  constructor(options: { value: number; captured: string }) {
    this.schema = VoltageSensorSchema;
    this.value = options.value;
    this.captured = options.captured;
  }
}

export default VoltageSensorReading;
