// this attempts to render readings based on their intrinsic data

import {
  Reading,
  SensorType,
  SensorTypeCurrentReading,
} from "../../services/AppModel";
import * as Renderer from "../renderers/registry";

interface TextReadingRenderProps {
  sensorType: SensorType;
  reading: Reading | null;
  noValuePlaceholder?: string;
  fixedPointPrecision?: number;
}

// todo - just a short circuit for now
function formatReadingValue(
  sensorType: SensorType,
  fixedPointPrecision: number,
  value: number
) {
  return `${
    Number.isInteger(value) ? value : value.toFixed(fixedPointPrecision)
  }${sensorType.unitSymbol}`;
}

export function formatReadingRenderProps(
  props: TextReadingRenderProps
): string {
  const fixedPointPrecision =
    props.fixedPointPrecision === undefined ? 2 : props.fixedPointPrecision;
  const noValuePlaceholder =
    props.noValuePlaceholder === undefined ? "-" : props.noValuePlaceholder;
  const value = props.reading?.value;
  const valueToShow = typeof value === "number" ? value : null;
  const formattedValue =
    valueToShow !== null
      ? formatReadingValue(props.sensorType, fixedPointPrecision, valueToShow)
      : noValuePlaceholder;
  return formattedValue;
}

const TextReadingRendererComponent = (
  props: Renderer.RenderSensorReadingProps
) => {
  const value = formatReadingRenderProps(props);

  const render = (
    <>
      <span>{props.sensorType.displayMeasure}</span>
      <br />
      <span className="dataNumber">{value}</span>
    </>
  );
  return render;
};

export default TextReadingRendererComponent;
