import Reading from "./Reading";
import PressureSensorSchema from "./PressureSensorSchema";

class PressureSensorReading implements Reading<number> {
  schema: PressureSensorSchema;

  value: number;

  captured: string;

  constructor(options: { value: number; captured: string }) {
    this.schema = PressureSensorSchema;
    this.value = options.value;
    this.captured = options.captured;
  }
}

export default PressureSensorReading;
