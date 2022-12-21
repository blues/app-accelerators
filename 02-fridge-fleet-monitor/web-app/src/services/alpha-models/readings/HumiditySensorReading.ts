import Reading from "./Reading";
import HumiditySensorSchema from "./HumiditySensorSchema";

class HumiditySensorReading implements Reading<number> {
  schema: HumiditySensorSchema;

  value: number;

  captured: string;

  constructor(options: { value: number; captured: string }) {
    this.schema = HumiditySensorSchema;
    this.value = options.value;
    this.captured = options.captured;
  }
}

export default HumiditySensorReading;
