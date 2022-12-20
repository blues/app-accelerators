// eslint-disable-next-line import/no-cycle
import ReadingSchema from "./ReadingSchema";

interface Reading<ReadingType> {
  value: ReadingType;
  captured: string;
  schema: ReadingSchema<ReadingType>;
}

export default Reading;
