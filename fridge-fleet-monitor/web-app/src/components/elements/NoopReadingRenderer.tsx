import * as Renderer from "../renderers/registry";

const NoOpReadingRendererComponent = (
  props: Renderer.RenderSensorReadingProps
) => {
  return null;
};

export default NoOpReadingRendererComponent;
