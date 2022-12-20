import Reading from "./Reading";
import TemperatureSensorSchema from "./TemperatureSensorSchema";

class TemperatureSensorReading implements Reading<number> {
  schema: TemperatureSensorSchema;

  value: number;

  captured: string;

  constructor(options: { value: number; captured: string }) {
    this.schema = TemperatureSensorSchema;
    this.value = options.value;
    this.captured = options.captured;
  }
}

export default TemperatureSensorReading;
